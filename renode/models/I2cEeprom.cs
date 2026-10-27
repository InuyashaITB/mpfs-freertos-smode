//
// SPDX-License-Identifier: MIT
//
// I2C EEPROM for the mpfs-freertos-smode project (Phase 3), AT24C256-like:
// 32 KiB, 64-byte pages, 2-byte (big-endian) memory address.
//   - A write transaction starts with the 2 address bytes. Data after them is
//     written with page wrap: past the end of the 64-byte page it continues at
//     the start of the same page, as on a real part. A write of only the 2
//     address bytes just sets the address pointer (first half of a random
//     read).
//   - Reads return bytes from the address pointer and increment it, wrapping
//     at the end of the array, so current-address reads work.
//   - After a data write the part is in its write cycle: it NACKs its address
//     for the next BusyPolls address phases, so drivers must ACK-poll.
//     (The controller model, MpfsI2c.cs, calls IsBusy() once per address
//     phase.)
//
// Needs a controller that calls Write() once per transaction, i.e.
// renode/models/MpfsI2c.cs, not Renode 1.13's MPFS_I2C. Written against the
// Renode 1.13 API (SoftConsole v2022.2).
//
// Monitor (i2c0.eeprom): Writes, BytesRead, BusyPolls (settable),
// WriteProtect (settable: writes are ACKed but ignored), Erase (all 0xFF),
// SaveImage "<file>", LoadImage "<file>" (contents survive a restart).
//
using System;
using System.IO;
using Antmicro.Renode.Logging;

namespace Antmicro.Renode.Peripherals.I2C
{
    public class I2cEeprom : II2CPeripheral
    {
        public I2cEeprom(int size = 0x8000, int pageSize = 64)
        {
            if(size <= 0 || (size & (size - 1)) != 0 || size > 0x10000)
            {
                throw new ArgumentException("I2cEeprom: size must be a power of two, at most 64 KiB");
            }
            if(pageSize <= 0 || (pageSize & (pageSize - 1)) != 0 || pageSize > size)
            {
                throw new ArgumentException("I2cEeprom: pageSize must be a power of two, at most size");
            }
            memory = new byte[size];
            this.pageSize = pageSize;
            BusyPolls = 3;
            Erase();
            Reset();
        }

        // Volatile state only: the array keeps its contents.
        public void Reset()
        {
            pointer = 0;
            busyPollsLeft = 0;
        }

        public void Write(byte[] data)
        {
            if(data.Length < 2)
            {
                this.Log(LogLevel.Warning, "Write of {0} byte(s) ignored: the memory address takes 2 bytes", data.Length);
                return;
            }
            pointer = ((data[0] << 8) | data[1]) & (memory.Length - 1);
            var count = data.Length - 2;
            if(count == 0)
            {
                return;
            }
            if(WriteProtect)
            {
                this.Log(LogLevel.Warning, "Write of {0} byte(s) at 0x{1:X4} ignored: write protected", count, pointer);
                return;
            }

            var pageBase = pointer & ~(pageSize - 1);
            var offset = pointer & (pageSize - 1);
            if(count > pageSize)
            {
                this.Log(LogLevel.Warning, "Write of {0} bytes at 0x{1:X4}: more than a page, only the last {2} are kept", count, pointer, pageSize);
            }
            for(var i = 0; i < count; i++)
            {
                memory[pageBase + ((offset + i) % pageSize)] = data[2 + i];
            }
            pointer = pageBase + ((offset + count) % pageSize);
            Writes++;
            busyPollsLeft = BusyPolls;
        }

        public byte[] Read(int count = 1)
        {
            var result = new byte[count];
            for(var i = 0; i < count; i++)
            {
                result[i] = memory[pointer];
                pointer = (pointer + 1) & (memory.Length - 1);
            }
            BytesRead += (ulong)count;
            return result;
        }

        public void FinishTransmission()
        {
        }

        // Called by the controller once per address phase: true NACKs it.
        public bool IsBusy()
        {
            if(busyPollsLeft > 0)
            {
                busyPollsLeft--;
                return true;
            }
            return false;
        }

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

        public int Size => memory.Length;
        // Address phases NACKed after each data write.
        public int BusyPolls { get; set; }
        public bool WriteProtect { get; set; }
        public ulong Writes { get; private set; }
        public ulong BytesRead { get; private set; }

        private readonly byte[] memory;
        private readonly int pageSize;
        private int pointer;
        private int busyPollsLeft;
    }
}
