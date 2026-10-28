#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# Host side of renode/models/SocketBridge.cs (Phase 5c): a tiny user-mode
# "host" on the simulated Ethernet, with no TAP device and no administrator
# rights. Frames travel as UDP datagrams on 127.0.0.1 (one frame, without
# FCS, per datagram). Python 3 standard library only.
#
#   python renode/host_bridge.py [--ping 192.168.10.10 ...] [--duration S]
#                                [--report FILE]
#
# As 192.168.10.1 (MAC 02:4d:50:46:53:fe) it:
#   - answers ARP requests for its address,
#   - answers ICMP echo requests (a board pinging the host),
#   - echoes UDP datagrams sent to port 7,
#   - with --ping, pings the given boards every second (ARP first) and
#     reports the first reply from each.
# Every event is printed, and with --report also appended to FILE (the
# 'make sim-host' check reads it). Stops after --duration seconds (default:
# never), after --exit-idle seconds without a frame once frames have flowed
# (CTest runs it next to Renode and relies on this), or on Ctrl+C.

import argparse
import socket
import struct
import sys
import time

ETH_ARP = 0x0806
ETH_IP = 0x0800
BROADCAST = b"\xff" * 6


def parse_mac(text):
    return bytes(int(x, 16) for x in text.split(":"))


def fmt_mac(mac):
    return ":".join("%02x" % b for b in mac)


def checksum(data):
    if len(data) % 2:
        data += b"\0"
    total = sum(struct.unpack("!%dH" % (len(data) // 2), data))
    while total >> 16:
        total = (total & 0xFFFF) + (total >> 16)
    return (~total) & 0xFFFF


class Host:
    def __init__(self, args):
        self.ip = socket.inet_aton(args.ip)
        self.mac = parse_mac(args.mac)
        self.peer = ("127.0.0.1", args.send_port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", args.listen_port))
        self.sock.settimeout(0.2)
        self.report = open(args.report, "a") if args.report else None
        self.quiet = args.quiet
        self.arp = {}                       # ip bytes -> mac bytes
        self.targets = [socket.inet_aton(t) for t in args.ping]
        self.answered = set()               # targets that replied to our ping
        self.seq = 0

    def log(self, text):
        line = "host: " + text
        if not self.quiet:
            print(line, flush=True)
        if self.report:
            self.report.write(line + "\n")
            self.report.flush()

    def send(self, dst_mac, ethertype, payload):
        frame = dst_mac + self.mac + struct.pack("!H", ethertype) + payload
        frame += b"\0" * max(0, 60 - len(frame))
        self.sock.sendto(frame, self.peer)

    # ARP --------------------------------------------------------------------
    def arp_packet(self, op, target_mac, target_ip):
        return struct.pack("!HHBBH6s4s6s4s", 1, ETH_IP, 6, 4, op,
                           self.mac, self.ip, target_mac, target_ip)

    def on_arp(self, payload):
        if len(payload) < 28:
            return
        _, _, _, _, op, sha, spa, _, tpa = struct.unpack("!HHBBH6s4s6s4s", payload[:28])
        if spa not in self.arp:
            self.log("ARP: %s is at %s" % (socket.inet_ntoa(spa), fmt_mac(sha)))
        self.arp[spa] = sha
        if op == 1 and tpa == self.ip:
            self.send(sha, ETH_ARP, self.arp_packet(2, sha, spa))

    # IP ---------------------------------------------------------------------
    def ip_packet(self, proto, dst_ip, body):
        header = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(body), self.seq & 0xFFFF, 0,
                             64, proto, 0, self.ip, dst_ip)
        header = header[:10] + struct.pack("!H", checksum(header)) + header[12:]
        return header + body

    def on_ip(self, src_mac, payload):
        if len(payload) < 20 or payload[0] >> 4 != 4:
            return
        ihl = (payload[0] & 0x0F) * 4
        total = struct.unpack("!H", payload[2:4])[0]
        proto = payload[9]
        src, dst = payload[12:16], payload[16:20]
        body = payload[ihl:total]                 # drop Ethernet padding
        self.arp.setdefault(src, src_mac)
        if dst != self.ip:
            return
        if proto == 1 and len(body) >= 8:
            self.on_icmp(src, body)
        elif proto == 17 and len(body) >= 8:
            self.on_udp(src, body)

    def on_icmp(self, src, body):
        icmp_type = body[0]
        if icmp_type == 8:                        # echo request: reply
            reply = b"\0\0\0\0" + body[4:]
            reply = reply[:2] + struct.pack("!H", checksum(reply)) + reply[4:]
            self.send(self.arp[src], ETH_IP, self.ip_packet(1, src, reply))
            self.log("ping from %s answered" % socket.inet_ntoa(src))
        elif icmp_type == 0 and src in self.targets and src not in self.answered:
            self.answered.add(src)
            self.log("ping %s: PASS" % socket.inet_ntoa(src))

    def on_udp(self, src, body):
        sport, dport, length = struct.unpack("!HHH", body[:6])
        data = body[8:length]
        if dport != 7:
            return
        udp = struct.pack("!HHHH", 7, sport, 8 + len(data), 0) + data
        self.send(self.arp[src], ETH_IP, self.ip_packet(17, src, udp))
        self.log("UDP echo: %d bytes from %s" % (len(data), socket.inet_ntoa(src)))

    # Active pings -------------------------------------------------------------
    def ping_targets(self):
        for target in self.targets:
            if target in self.answered:
                continue
            if target not in self.arp:
                self.send(BROADCAST, ETH_ARP, self.arp_packet(1, b"\0" * 6, target))
                continue
            self.seq += 1
            icmp = struct.pack("!BBHHH", 8, 0, 0, 0x4D50, self.seq) + b"host_bridge ping"
            icmp = icmp[:2] + struct.pack("!H", checksum(icmp)) + icmp[4:]
            self.send(self.arp[target], ETH_IP, self.ip_packet(1, target, icmp))

    def run(self, duration, exit_idle):
        end = time.monotonic() + duration if duration > 0 else None
        last_frame = None
        next_ping = 0.0
        self.log("listening on 127.0.0.1:%d as %s (%s)"
                 % (self.sock.getsockname()[1], socket.inet_ntoa(self.ip), fmt_mac(self.mac)))
        while end is None or time.monotonic() < end:
            now = time.monotonic()
            if exit_idle > 0 and last_frame is not None and now - last_frame > exit_idle:
                self.log("no frames for %g s: stopping" % exit_idle)
                break
            if self.targets and now >= next_ping:
                self.ping_targets()
                next_ping = now + 1.0
            try:
                frame, _ = self.sock.recvfrom(2048)
            except socket.timeout:
                continue
            except ConnectionResetError:
                continue                          # Windows: nobody listening yet
            last_frame = time.monotonic()
            if len(frame) < 14:
                continue
            dst, src = frame[:6], frame[6:12]
            ethertype = struct.unpack("!H", frame[12:14])[0]
            if dst not in (self.mac, BROADCAST):
                continue
            if ethertype == ETH_ARP:
                self.on_arp(frame[14:])
            elif ethertype == ETH_IP:
                self.on_ip(src, frame[14:])


def main():
    parser = argparse.ArgumentParser(
        description="User-mode host for the Renode SocketBridge (renode/models/SocketBridge.cs)")
    parser.add_argument("--ip", default="192.168.10.1")
    parser.add_argument("--mac", default="02:4d:50:46:53:fe")
    parser.add_argument("--listen-port", type=int, default=37000,
                        help="UDP port the bridge sends to (SocketBridge remotePort)")
    parser.add_argument("--send-port", type=int, default=37001,
                        help="UDP port the bridge listens on (SocketBridge localPort)")
    parser.add_argument("--ping", action="append", default=[], metavar="IP",
                        help="ping this board until it answers (repeatable)")
    parser.add_argument("--duration", type=float, default=0, help="seconds, 0 = run forever")
    parser.add_argument("--exit-idle", type=float, default=0, metavar="S",
                        help="once frames have arrived, stop after S seconds without one "
                             "(0 = never)")
    parser.add_argument("--report", help="append every event to this file")
    parser.add_argument("--quiet", action="store_true",
                        help="print nothing (with --report: events go only to the file)")
    args = parser.parse_args()
    try:
        Host(args).run(args.duration, args.exit_idle)
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
