//
// SPDX-License-Identifier: MIT
//
// Example-protocol SPI device for the mpfs-freertos-smode project (Phase 4),
// attached to an MSS SPI controller (MPFS_SPI).
//
// Protocol (CRC-8: polynomial 0x07, init 0x00, i.e. CRC-8/SMBUS):
//   Request, in one transfer:
//     0xA5 | cmd | len | payload[len] | crc8(cmd, len, payload)    len <= 32
//   Response, read in later transfers (the cursor survives chip select):
//     the master clocks filler (0xFF); the device answers 0x00 until the
//     response is ready (ResponseDelay bytes), then 0x5A, then
//     status | len | payload[len] | crc8(status, len, payload)
//   A transfer whose first byte is 0xA5 starts a new request and drops any
//   response not yet read. Any other input is filler. Bytes after the
//   request in its own transfer are ignored (answered 0x00).
//
// Commands: 0x01 PING (payload C5 01: ID, version), 0x02 ECHO, 0x03
// READ_REG(addr) -> value, 0x04 WRITE_REG(addr, value), 0x05 GET_EVENTS (up to
// 16 queued event bytes; removes them).
// Registers: 16 x 8 bit; 0 (ID 0xC5) and 1 (version 0x01) are read-only.
// Status: 0x00 OK; NAK 0x81 BAD_CRC, 0x82 UNKNOWN_CMD, 0x83 BAD_LEN,
// 0x84 BAD_ADDR, 0x85 READ_ONLY.
//
// DataReady (GPIO output) is high while events are queued; wire it to an
// interrupt (the repl uses PLIC 119, F2M_1).
//
// End of a transfer: MPFS_SPI (Renode 1.13) calls FinishTransmission() when
// the frame count is reached and again when slave select is released; both
// only mark "next byte starts a transfer". A request cut short by the end of
// a transfer is dropped (AbortedRequests).
//
// Written against the Renode 1.13 API (SoftConsole v2022.2). Load it before
// the platform description that attaches it:
//   include @renode/models/ProtoSpiDevice.cs
//   machine LoadPlatformDescription @renode/mpfs-sim-peripherals.repl
//
// Monitor (spi1.proto): InjectEvent <byte>, SetRegister <addr> <value>,
// GetRegister <addr>, ResponseDelay (settable), Requests, Naks,
// AbortedRequests, EventsQueued, EventsDropped.
//
using System.Collections.Generic;
using Antmicro.Renode.Core;
using Antmicro.Renode.Logging;

namespace Antmicro.Renode.Peripherals.SPI
{
    public class ProtoSpiDevice : ISPIPeripheral
    {
        public ProtoSpiDevice()
        {
            DataReady = new GPIO();
            registers = new byte[RegisterCount];
            events = new Queue<byte>();
            request = new List<byte>();
            response = new List<byte>();
            ResponseDelay = 2;
            Reset();
        }

        public void Reset()
        {
            for(var i = 0; i < registers.Length; i++)
            {
                registers[i] = 0;
            }
            registers[0] = DeviceId;
            registers[1] = DeviceVersion;
            events.Clear();
            state = State.Idle;
            startOfTransfer = true;
            requestTransfer = false;
            request.Clear();
            response.Clear();
            DataReady.Set(false);
        }

        public byte Transmit(byte data)
        {
            var first = startOfTransfer;
            startOfTransfer = false;
            if(first)
            {
                requestTransfer = false;
            }

            if(first && data == RequestStart)
            {
                if(state == State.Request)
                {
                    AbortRequest();
                }
                response.Clear();
                request.Clear();
                state = State.Request;
                requestTransfer = true;
                return 0x00;
            }

            switch(state)
            {
                case State.Request:
                    ReceiveRequestByte(data);
                    return 0x00;

                case State.Response:
                    // The response is read in the transfers after the
                    // request; the rest of the request's own transfer
                    // (e.g. after a bad length) is ignored.
                    return requestTransfer ? (byte)0x00 : NextResponseByte();

                default:
                    return 0x00;
            }
        }

        public void FinishTransmission()
        {
            startOfTransfer = true;
            if(state == State.Request)
            {
                AbortRequest();
            }
        }

        public void InjectEvent(byte code)
        {
            if(events.Count >= MaxEvents)
            {
                EventsDropped++;
                this.Log(LogLevel.Warning, "Event 0x{0:X2} dropped: queue full", code);
                return;
            }
            events.Enqueue(code);
            DataReady.Set(true);
        }

        public void SetRegister(int address, byte value)
        {
            registers[address & (RegisterCount - 1)] = value;
        }

        public byte GetRegister(int address)
        {
            return registers[address & (RegisterCount - 1)];
        }

        public GPIO DataReady { get; private set; }
        // Bytes answered 0x00 before the 0x5A marker of each response.
        public int ResponseDelay { get; set; }
        public ulong Requests { get; private set; }
        public ulong Naks { get; private set; }
        public ulong AbortedRequests { get; private set; }
        public ulong EventsDropped { get; private set; }
        public int EventsQueued => events.Count;

        private void ReceiveRequestByte(byte data)
        {
            request.Add(data);
            if(request.Count < 2)
            {
                return; // cmd
            }
            var length = request[1];
            if(length > MaxPayload)
            {
                // Without a valid length the end of the frame is unknown:
                // answer now and ignore the rest of the transfer.
                Requests++;
                Respond(StatusBadLength, null);
                return;
            }
            if(request.Count < 3 + length)
            {
                return; // payload, then crc
            }

            Requests++;
            var crc = Crc8(request, 0, 2 + length);
            if(crc != request[2 + length])
            {
                Respond(StatusBadCrc, null);
                return;
            }
            Execute(request[0], request.GetRange(2, length).ToArray());
        }

        private void Execute(byte command, byte[] payload)
        {
            switch(command)
            {
                case CommandPing:
                    Respond(StatusOk, new byte[] { DeviceId, DeviceVersion });
                    break;

                case CommandEcho:
                    Respond(StatusOk, payload);
                    break;

                case CommandReadRegister:
                    if(payload.Length != 1)
                    {
                        Respond(StatusBadLength, null);
                    }
                    else if(payload[0] >= RegisterCount)
                    {
                        Respond(StatusBadAddress, null);
                    }
                    else
                    {
                        Respond(StatusOk, new byte[] { registers[payload[0]] });
                    }
                    break;

                case CommandWriteRegister:
                    if(payload.Length != 2)
                    {
                        Respond(StatusBadLength, null);
                    }
                    else if(payload[0] >= RegisterCount)
                    {
                        Respond(StatusBadAddress, null);
                    }
                    else if(payload[0] < FirstWritableRegister)
                    {
                        Respond(StatusReadOnly, null);
                    }
                    else
                    {
                        registers[payload[0]] = payload[1];
                        Respond(StatusOk, null);
                    }
                    break;

                case CommandGetEvents:
                {
                    var taken = new List<byte>();
                    while(events.Count > 0 && taken.Count < MaxEvents)
                    {
                        taken.Add(events.Dequeue());
                    }
                    if(events.Count == 0)
                    {
                        DataReady.Set(false);
                    }
                    Respond(StatusOk, taken.ToArray());
                    break;
                }

                default:
                    Respond(StatusUnknownCommand, null);
                    break;
            }
        }

        private void Respond(byte status, byte[] payload)
        {
            if(status != StatusOk)
            {
                Naks++;
                this.Log(LogLevel.Debug, "NAK 0x{0:X2}", status);
            }
            response.Clear();
            response.Add(status);
            response.Add((byte)(payload == null ? 0 : payload.Length));
            if(payload != null)
            {
                response.AddRange(payload);
            }
            response.Add(Crc8(response, 0, response.Count));
            delayLeft = ResponseDelay;
            markerSent = false;
            responseIndex = 0;
            request.Clear();
            state = State.Response;
        }

        private byte NextResponseByte()
        {
            if(delayLeft > 0)
            {
                delayLeft--;
                return 0x00;
            }
            if(!markerSent)
            {
                markerSent = true;
                return ResponseStart;
            }
            var value = response[responseIndex++];
            if(responseIndex >= response.Count)
            {
                response.Clear();
                state = State.Idle;
            }
            return value;
        }

        private void AbortRequest()
        {
            AbortedRequests++;
            this.Log(LogLevel.Warning, "Request dropped: transfer ended after {0} byte(s)", request.Count + 1);
            request.Clear();
            state = State.Idle;
        }

        private static byte Crc8(List<byte> data, int start, int count)
        {
            var crc = 0;
            for(var i = start; i < start + count; i++)
            {
                crc ^= data[i];
                for(var bit = 0; bit < 8; bit++)
                {
                    crc = ((crc & 0x80) != 0) ? ((crc << 1) ^ 0x07) : (crc << 1);
                    crc &= 0xFF;
                }
            }
            return (byte)crc;
        }

        private const byte RequestStart = 0xA5;
        private const byte ResponseStart = 0x5A;
        private const int MaxPayload = 32;
        private const int MaxEvents = 16;
        private const int RegisterCount = 16;
        private const int FirstWritableRegister = 2;
        private const byte DeviceId = 0xC5;
        private const byte DeviceVersion = 0x01;

        private const byte CommandPing = 0x01;
        private const byte CommandEcho = 0x02;
        private const byte CommandReadRegister = 0x03;
        private const byte CommandWriteRegister = 0x04;
        private const byte CommandGetEvents = 0x05;

        private const byte StatusOk = 0x00;
        private const byte StatusBadCrc = 0x81;
        private const byte StatusUnknownCommand = 0x82;
        private const byte StatusBadLength = 0x83;
        private const byte StatusBadAddress = 0x84;
        private const byte StatusReadOnly = 0x85;

        private readonly byte[] registers;
        private readonly Queue<byte> events;
        private readonly List<byte> request;
        private readonly List<byte> response;
        private State state;
        private bool startOfTransfer;
        private bool requestTransfer;
        private int delayLeft;
        private bool markerSent;
        private int responseIndex;

        private enum State
        {
            Idle,
            Request,
            Response
        }
    }
}
