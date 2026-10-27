//
// SPDX-License-Identifier: MIT
//
// Test SPI slave for the mpfs-freertos-smode project (Phase 1): answers every
// byte with its bitwise complement, in the same frame. Lets the firmware check
// a full-duplex transfer through an MSS SPI controller (MPFS_SPI) from S-mode.
//
// Written against the Renode 1.13 API (the version SoftConsole v2022.2 ships).
// Load it before the platform description that attaches it:
//   include @renode/models/SpiLoopback.cs
//   machine LoadPlatformDescription @renode/mpfs-sim-peripherals.repl
//
// Monitor: spi1.loopback BytesTransferred / Transactions
//
using Antmicro.Renode.Logging;

namespace Antmicro.Renode.Peripherals.SPI
{
    public class SpiLoopback : ISPIPeripheral
    {
        public SpiLoopback()
        {
            Reset();
        }

        public byte Transmit(byte data)
        {
            BytesTransferred++;
            return (byte)~data;
        }

        // Called by MPFS_SPI when the master releases slave select.
        public void FinishTransmission()
        {
            Transactions++;
            this.Log(LogLevel.Debug, "Transaction {0} done, {1} bytes so far", Transactions, BytesTransferred);
        }

        public void Reset()
        {
            BytesTransferred = 0;
            Transactions = 0;
        }

        public ulong BytesTransferred { get; private set; }
        public ulong Transactions { get; private set; }
    }
}
