//
// SPDX-License-Identifier: MIT
//
// User-mode bridge between a Renode Ethernet switch and the host, for the
// mpfs-freertos-smode project (Phase 5c). No TAP device, so no administrator
// rights: every Ethernet frame the switch delivers to the bridge is sent, as
// one UDP datagram (the frame without FCS), to 127.0.0.1:RemotePort, and
// every datagram arriving on 127.0.0.1:LocalPort goes onto the switch as a
// frame. renode/host_bridge.py is the host side: it answers ARP, ICMP echo
// and UDP echo for a host address on the simulated network.
//
// It is a peripheral only so that a repl can create it and 'connector
// Connect' can attach it; the two registers are counters.
//
// Written against the Renode 1.13 API (SoftConsole v2022.2):
//   include @renode/models/SocketBridge.cs
//   machine LoadPlatformDescription @renode/host-bridge.repl
//   connector Connect sysbus.bridge sw
//
// Monitor (sysbus.bridge): FramesToHost, FramesFromHost, LocalPort,
// RemotePort.
//
using System;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using Antmicro.Renode.Core.Structure;
using Antmicro.Renode.Logging;
using Antmicro.Renode.Network;
using Antmicro.Renode.Peripherals.Bus;

namespace Antmicro.Renode.Peripherals.Network
{
    public class SocketBridge : IMACInterface, IDoubleWordPeripheral, IKnownSize, IDisposable
    {
        public SocketBridge(int localPort = 37001, int remotePort = 37000)
        {
            LocalPort = localPort;
            RemotePort = remotePort;
            MAC = MACAddress.Parse("02:4d:50:46:53:fe");
            remote = new IPEndPoint(IPAddress.Loopback, remotePort);
            socket = new UdpClient(new IPEndPoint(IPAddress.Loopback, localPort));
            socket.Client.ReceiveTimeout = 200;
            receiver = new Thread(ReceiveLoop) { IsBackground = true, Name = "SocketBridge" };
            receiver.Start();
        }

        // From the switch: to the host, without the FCS.
        public void ReceiveFrame(EthernetFrame frame)
        {
            var bytes = frame.Bytes;
            var length = EthernetFrame.CheckCRC(bytes) ? bytes.Length - EthernetFrame.CRCLength : bytes.Length;
            try
            {
                socket.Send(bytes, length, remote);
                FramesToHost++;
            }
            catch(SocketException e)
            {
                this.Log(LogLevel.Warning, "Frame to host dropped: {0}", e.Message);
            }
        }

        public uint ReadDoubleWord(long offset)
        {
            switch(offset)
            {
                case 0x0:
                    return (uint)FramesToHost;
                case 0x4:
                    return (uint)FramesFromHost;
                default:
                    return 0;
            }
        }

        public void WriteDoubleWord(long offset, uint value)
        {
        }

        public void Reset()
        {
        }

        public void Dispose()
        {
            running = false;
            receiver.Join(1000);
            socket.Close();
        }

        public MACAddress MAC { get; set; }
        public event Action<EthernetFrame> FrameReady;
        public long Size => 0x100;
        public int LocalPort { get; private set; }
        public int RemotePort { get; private set; }
        public ulong FramesToHost { get; private set; }
        public ulong FramesFromHost { get; private set; }

        // From the host: onto the switch (FCS added).
        private void ReceiveLoop()
        {
            while(running)
            {
                byte[] data;
                try
                {
                    var from = new IPEndPoint(IPAddress.Any, 0);
                    data = socket.Receive(ref from);
                }
                catch(SocketException)
                {
                    continue;   // timeout: check 'running' again
                }
                catch(ObjectDisposedException)
                {
                    break;
                }

                if(!EthernetFrame.TryCreateEthernetFrame(data, true, out var frame))
                {
                    this.Log(LogLevel.Warning, "Datagram of {0} bytes from the host is not a frame", data.Length);
                    continue;
                }
                FramesFromHost++;
                FrameReady?.Invoke(frame);
            }
        }

        private readonly UdpClient socket;
        private readonly IPEndPoint remote;
        private readonly Thread receiver;
        private volatile bool running = true;
    }
}
