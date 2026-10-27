//
// SPDX-License-Identifier: MIT
//
// SPI NOR flash for the mpfs-freertos-smode project (Phase 2), attached to an
// MSS SPI controller (MPFS_SPI). Behaves like a plain 3-byte-address serial
// NOR (W25Q / MT25Q style) as far as the firmware can tell:
//   - Page program only clears bits (memory &= data) and wraps inside the
//     256-byte page; erase sets bytes back to 0xFF.
//   - Program and erase need the write enable latch (WREN) and clear it when
//     they finish.
//   - They commit when the master ends the command, then report busy (WIP)
//     for a number of status reads, so drivers must poll RDSR. While busy,
//     every command other than RDSR is ignored, as on a real part.
//
// Commands: 0x9F RDID, 0x05 RDSR, 0x06 WREN, 0x04 WRDI, 0x03 READ,
// 0x0B FAST_READ (one dummy byte), 0x02 PP, 0x20 SE (4 KiB), 0xD8 BE (64 KiB),
// 0xC7 / 0x60 CE.
//
// End of a command: MPFS_SPI (Renode 1.13) calls FinishTransmission() when the
// frame count programmed in CONTROL is reached, and again when slave select
// is released. The first call ends the command; the second finds nothing in
// progress. So the firmware sends each command (opcode, address and data) as
// one transfer with the frame count set to its total length.
//
// Written against the Renode 1.13 API (SoftConsole v2022.2). Load it before
// the platform description that attaches it:
//   include @renode/models/NorFlash.cs
//   machine LoadPlatformDescription @renode/mpfs-sim-peripherals.repl
//
// Monitor (spi0.flash): Programs, Erases, Commands, IgnoredCommands,
// BusyPolls (settable), WriteEnabled, Busy, Erase (whole array, no busy),
// SaveImage "<file>", LoadImage "<file>" (contents survive a restart).
//
using System;
using System.IO;
using Antmicro.Renode.Logging;

namespace Antmicro.Renode.Peripherals.SPI
{
    public class NorFlash : ISPIPeripheral
    {
        public NorFlash(long size = 0x1000000, byte manufacturerId = 0xEF,
                        byte memoryType = 0x40, byte capacityId = 0x18)
        {
            if(size < SectorSize || (size & (size - 1)) != 0)
            {
                throw new ArgumentException("NorFlash: size must be a power of two of at least 4 KiB");
            }
            memory = new byte[size];
            jedecId = new byte[] { manufacturerId, memoryType, capacityId };
            pageData = new byte[PageSize];
            BusyPolls = 3;
            Erase();
            Reset();
        }

        // Volatile state only: the array keeps its contents, like a real part
        // across a power cycle.
        public void Reset()
        {
            inCommand = false;
            ignored = false;
            WriteEnabled = false;
            busyPollsLeft = 0;
        }

        public byte Transmit(byte data)
        {
            if(!inCommand)
            {
                StartCommand(data);
                return 0xFF;
            }

            position++;
            if(ignored)
            {
                return 0xFF;
            }

            switch(command)
            {
                case Opcode.ReadStatus:
                    return ReadStatus();

                case Opcode.ReadId:
                    return (position <= jedecId.Length) ? jedecId[position - 1] : (byte)0x00;

                case Opcode.Read:
                case Opcode.FastRead:
                    if(position <= AddressBytes)
                    {
                        AccumulateAddress(data);
                        return 0xFF;
                    }
                    if(command == Opcode.FastRead && position == AddressBytes + 1)
                    {
                        return 0xFF; // dummy byte
                    }
                    var value = memory[address];
                    address = (address + 1) & (memory.Length - 1);
                    return value;

                case Opcode.PageProgram:
                    if(position <= AddressBytes)
                    {
                        AccumulateAddress(data);
                    }
                    else
                    {
                        // The page buffer wraps; the last byte written to a
                        // position wins, as on a real part.
                        pageData[(pageOffset + programmed) % PageSize] = data;
                        programmed++;
                    }
                    return 0xFF;

                case Opcode.SectorErase:
                case Opcode.BlockErase:
                    if(position <= AddressBytes)
                    {
                        AccumulateAddress(data);
                    }
                    return 0xFF;

                default:
                    return 0xFF;
            }
        }

        public void FinishTransmission()
        {
            if(!inCommand)
            {
                return;
            }
            inCommand = false;
            if(ignored)
            {
                return;
            }

            switch(command)
            {
                case Opcode.WriteEnable:
                    WriteEnabled = true;
                    break;

                case Opcode.WriteDisable:
                    WriteEnabled = false;
                    break;

                case Opcode.PageProgram:
                    if(!CheckWrite("page program", position > AddressBytes))
                    {
                        break;
                    }
                    var pageBase = address & ~(long)(PageSize - 1);
                    for(var i = 0; i < PageSize; i++)
                    {
                        memory[pageBase + i] &= pageData[i];
                    }
                    Programs++;
                    StartBusy();
                    break;

                case Opcode.SectorErase:
                    if(CheckWrite("sector erase", position == AddressBytes))
                    {
                        EraseRange(address & ~(long)(SectorSize - 1), SectorSize);
                    }
                    break;

                case Opcode.BlockErase:
                    if(CheckWrite("block erase", position == AddressBytes))
                    {
                        EraseRange(address & ~(long)(Math.Min(BlockSize, memory.Length) - 1),
                                   Math.Min(BlockSize, memory.Length));
                    }
                    break;

                case Opcode.ChipErase:
                case Opcode.ChipErase2:
                    if(CheckWrite("chip erase", position == 0))
                    {
                        EraseRange(0, memory.Length);
                    }
                    break;
            }
        }

        // Monitor helpers.
        public void Erase()
        {
            for(var i = 0; i < memory.Length; i++)
            {
                memory[i] = 0xFF;
            }
        }

        public void SaveImage(string path)
        {
            File.WriteAllBytes(path, memory);
        }

        public void LoadImage(string path)
        {
            var data = File.ReadAllBytes(path);
            Erase();
            Array.Copy(data, memory, Math.Min(data.Length, memory.Length));
        }

        public long Size => memory.Length;
        public bool WriteEnabled { get; private set; }
        public bool Busy => busyPollsLeft > 0;
        // Status reads that report WIP after each program or erase.
        public int BusyPolls { get; set; }
        public ulong Commands { get; private set; }
        public ulong IgnoredCommands { get; private set; }
        public ulong Programs { get; private set; }
        public ulong Erases { get; private set; }

        private void StartCommand(byte opcode)
        {
            inCommand = true;
            command = (Opcode)opcode;
            position = 0;
            address = 0;
            ignored = false;
            Commands++;

            if(Busy && command != Opcode.ReadStatus)
            {
                this.Log(LogLevel.Warning, "Command 0x{0:X2} ignored: program/erase in progress (poll RDSR first)", opcode);
                ignored = true;
                IgnoredCommands++;
                return;
            }

            switch(command)
            {
                case Opcode.PageProgram:
                    for(var i = 0; i < PageSize; i++)
                    {
                        pageData[i] = 0xFF;
                    }
                    programmed = 0;
                    break;

                case Opcode.ReadStatus:
                case Opcode.ReadId:
                case Opcode.Read:
                case Opcode.FastRead:
                case Opcode.WriteEnable:
                case Opcode.WriteDisable:
                case Opcode.SectorErase:
                case Opcode.BlockErase:
                case Opcode.ChipErase:
                case Opcode.ChipErase2:
                    break;

                default:
                    this.Log(LogLevel.Warning, "Unsupported command 0x{0:X2} ignored", opcode);
                    ignored = true;
                    IgnoredCommands++;
                    break;
            }
        }

        private void AccumulateAddress(byte data)
        {
            address = ((address << 8) | data) & (memory.Length - 1);
            if(position == AddressBytes)
            {
                pageOffset = (int)(address & (PageSize - 1));
            }
        }

        private byte ReadStatus()
        {
            var status = (byte)((Busy ? StatusWip : 0) | (WriteEnabled ? StatusWel : 0));
            if(busyPollsLeft > 0)
            {
                busyPollsLeft--;
                if(busyPollsLeft == 0)
                {
                    WriteEnabled = false;
                }
            }
            return status;
        }

        private bool CheckWrite(string what, bool wellFormed)
        {
            if(!wellFormed)
            {
                this.Log(LogLevel.Warning, "{0} ignored: wrong number of address/data bytes ({1})", what, position);
                IgnoredCommands++;
                return false;
            }
            if(!WriteEnabled)
            {
                this.Log(LogLevel.Warning, "{0} ignored: write enable latch not set (send WREN first)", what);
                IgnoredCommands++;
                return false;
            }
            return true;
        }

        private void EraseRange(long start, long length)
        {
            for(var i = start; i < start + length; i++)
            {
                memory[i] = 0xFF;
            }
            Erases++;
            StartBusy();
        }

        private void StartBusy()
        {
            busyPollsLeft = BusyPolls;
            if(busyPollsLeft <= 0)
            {
                WriteEnabled = false;
            }
        }

        private const int AddressBytes = 3;
        private const int PageSize = 256;
        private const int SectorSize = 4096;
        private const int BlockSize = 65536;
        private const byte StatusWip = 0x01;
        private const byte StatusWel = 0x02;

        private readonly byte[] memory;
        private readonly byte[] jedecId;
        private readonly byte[] pageData;
        private bool inCommand;
        private bool ignored;
        private Opcode command;
        private int position;
        private long address;
        private int pageOffset;
        private int programmed;
        private int busyPollsLeft;

        private enum Opcode : byte
        {
            WriteDisable = 0x04,
            ReadStatus = 0x05,
            WriteEnable = 0x06,
            PageProgram = 0x02,
            Read = 0x03,
            FastRead = 0x0B,
            SectorErase = 0x20,
            ChipErase2 = 0x60,
            ChipErase = 0xC7,
            ReadId = 0x9F,
            BlockErase = 0xD8,
        }
    }
}
