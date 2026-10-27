//
// SPDX-License-Identifier: MIT
//
// MSS I2C (CoreI2C) master for the mpfs-freertos-smode project (Phase 3).
// Replaces Renode 1.13's MPFS_I2C on i2c0, which hands a device one byte per
// Write() with no start/stop, and never reaches status 0x58 on a master read,
// so the vendor MSS_I2C driver cannot complete an EEPROM read with it.
//
// Behaves like the hardware as the MSS_I2C driver sees it:
//   - Setting STA while the bus is idle (status 0xF8) sends START: status
//     0x08 and SI set.
//   - Every other action happens when software clears SI, and depends on the
//     status and on STA/STO/AA/DATA at that moment:
//       0x08/0x10            send the address in DATA: 0x18/0x20 (write) or
//                            0x40/0x48 (read)
//       0x18/0x28/0x20/0x30  STA: repeated START (0x10); STO: STOP (0xF8);
//                            both: STOP then START (0x08); neither: send DATA
//                            (0x28, or 0x30 without a device)
//       0x40/0x50            STO/STA as above, else receive a byte: ACK it
//                            (0x50) if AA is set, else NACK (0x58)
//       0x48/0x58            STO/STA as above
//   - IRQ = ENS1 && SI (level).
//
// Devices (II2CPeripheral children, registered by 7-bit address):
//   - one Write(bytes) per written part of a transaction, at the repeated
//     START or STOP that ends it;
//   - Read(1) for each byte the master receives;
//   - FinishTransmission() at STOP;
//   - the address is NACKed if no device is registered there, or if the
//     device has a public 'bool IsBusy()' method that returns true (looked up
//     by reflection, so device models need no shared type), e.g. an EEPROM
//     in its write cycle.
// Master mode only; the slave states are not modelled.
//
// Written against the Renode 1.13 API (SoftConsole v2022.2). It takes the
// place of the built-in model:
//   machine UnregisterFromParent sysbus.i2c0
//   include @renode/models/MpfsI2c.cs
//   machine LoadPlatformDescription @renode/mpfs-sim-peripherals.repl
//
// Monitor (i2c0): Transactions, AddressNacks, LastStatus.
//
using System;
using System.Collections.Generic;
using Antmicro.Renode.Core;
using Antmicro.Renode.Core.Structure;
using Antmicro.Renode.Logging;
using Antmicro.Renode.Peripherals.Bus;

namespace Antmicro.Renode.Peripherals.I2C
{
    public class MpfsI2c : SimpleContainer<II2CPeripheral>, IBytePeripheral, IKnownSize
    {
        public MpfsI2c(Machine machine) : base(machine)
        {
            IRQ = new GPIO();
            writeBuffer = new List<byte>();
            Reset();
        }

        public override void Reset()
        {
            control = 0;
            status = StatusIdle;
            data = 0;
            slaveAddress0 = 0;
            slaveAddress1 = 0;
            smbus = 0;
            frequency = 0;
            glitch = 0;
            selected = null;
            writeBuffer.Clear();
            UpdateInterrupt();
        }

        public byte ReadByte(long offset)
        {
            switch((Registers)offset)
            {
                case Registers.Control:
                    return control;
                case Registers.Status:
                    return status;
                case Registers.Data:
                    return data;
                case Registers.Slave0Address:
                    return slaveAddress0;
                case Registers.SMBus:
                    return smbus;
                case Registers.Frequency:
                    return frequency;
                case Registers.GlitchRegLength:
                    return glitch;
                case Registers.Slave1Address:
                    return slaveAddress1;
                default:
                    this.Log(LogLevel.Warning, "Read from unknown offset 0x{0:X}", offset);
                    return 0;
            }
        }

        public void WriteByte(long offset, byte value)
        {
            switch((Registers)offset)
            {
                case Registers.Control:
                    WriteControl(value);
                    break;
                case Registers.Status:
                    break; // read only
                case Registers.Data:
                    data = value;
                    break;
                case Registers.Slave0Address:
                    slaveAddress0 = value;
                    break;
                case Registers.SMBus:
                    smbus = value;
                    break;
                case Registers.Frequency:
                    frequency = value;
                    break;
                case Registers.GlitchRegLength:
                    glitch = value;
                    break;
                case Registers.Slave1Address:
                    slaveAddress1 = value;
                    break;
                default:
                    this.Log(LogLevel.Warning, "Write of 0x{0:X2} to unknown offset 0x{1:X}", value, offset);
                    break;
            }
        }

        public long Size => 0x1000;
        public GPIO IRQ { get; private set; }

        public ulong Transactions { get; private set; }
        public ulong AddressNacks { get; private set; }
        public byte LastStatus => status;

        private void WriteControl(byte value)
        {
            var siWasSet = (control & ControlSi) != 0;
            // Software can clear SI but not set it.
            control = (byte)(siWasSet ? value : (value & ~ControlSi));

            if((control & ControlEns1) == 0)
            {
                // Disabled: the state machine goes back to idle.
                control = (byte)(control & ~(ControlSi | ControlSto));
                status = StatusIdle;
                selected = null;
                writeBuffer.Clear();
            }
            else if(siWasSet && (control & ControlSi) == 0)
            {
                Step();
            }
            else if(!siWasSet && status == StatusIdle)
            {
                if((control & ControlSta) != 0)
                {
                    SetStatus(StatusStart);
                }
                else if((control & ControlSto) != 0)
                {
                    control = (byte)(control & ~ControlSto); // nothing to stop
                }
            }
            UpdateInterrupt();
        }

        // SI was just cleared: carry out the next bus action.
        private void Step()
        {
            var sta = (control & ControlSta) != 0;
            var sto = (control & ControlSto) != 0;

            switch(status)
            {
                case StatusStart:
                case StatusRepeatedStart:
                    AddressPhase(data);
                    break;

                case StatusAddressWriteAck:
                case StatusDataWriteAck:
                case StatusAddressWriteNack:
                case StatusDataWriteNack:
                    if(!StartOrStop(sta, sto))
                    {
                        if(selected == null)
                        {
                            SetStatus(StatusDataWriteNack);
                        }
                        else
                        {
                            writeBuffer.Add(data);
                            SetStatus(StatusDataWriteAck);
                        }
                    }
                    break;

                case StatusAddressReadAck:
                case StatusDataReadAck:
                    if(!StartOrStop(sta, sto))
                    {
                        var received = (selected != null) ? selected.Read(1) : null;
                        data = (received != null && received.Length > 0) ? received[0] : (byte)0xFF;
                        SetStatus(((control & ControlAa) != 0) ? StatusDataReadAck : StatusDataReadNack);
                    }
                    break;

                case StatusAddressReadNack:
                case StatusDataReadNack:
                    if(!StartOrStop(sta, sto))
                    {
                        this.Log(LogLevel.Warning, "SI cleared in status 0x{0:X2} without STA or STO: bus left waiting", status);
                    }
                    break;

                default:
                    break;
            }
        }

        // Handles STA / STO in a status where either may follow. Returns false
        // if neither is set.
        private bool StartOrStop(bool sta, bool sto)
        {
            if(sto)
            {
                Stop();
                if(sta)
                {
                    SetStatus(StatusStart);
                }
                return true;
            }
            if(sta)
            {
                FlushWrite();
                SetStatus(StatusRepeatedStart);
                return true;
            }
            return false;
        }

        private void AddressPhase(byte addressByte)
        {
            var address = addressByte >> 1;
            var read = (addressByte & 1) != 0;
            II2CPeripheral device;

            selected = null;
            if(!ChildCollection.TryGetValue(address, out device))
            {
                this.Log(LogLevel.Debug, "No device at address 0x{0:X2}: NACK", address);
                AddressNacks++;
            }
            else if(IsBusy(device))
            {
                this.Log(LogLevel.Debug, "Device at 0x{0:X2} busy: NACK", address);
                AddressNacks++;
            }
            else
            {
                selected = device;
            }

            if(read)
            {
                SetStatus((selected != null) ? StatusAddressReadAck : StatusAddressReadNack);
            }
            else
            {
                SetStatus((selected != null) ? StatusAddressWriteAck : StatusAddressWriteNack);
            }
        }

        private static bool IsBusy(II2CPeripheral device)
        {
            var method = device.GetType().GetMethod("IsBusy", Type.EmptyTypes);
            return method != null && method.ReturnType == typeof(bool) && (bool)method.Invoke(device, null);
        }

        private void FlushWrite()
        {
            if(selected != null && writeBuffer.Count > 0)
            {
                selected.Write(writeBuffer.ToArray());
            }
            writeBuffer.Clear();
        }

        private void Stop()
        {
            FlushWrite();
            if(selected != null)
            {
                selected.FinishTransmission();
            }
            selected = null;
            Transactions++;
            control = (byte)(control & ~(ControlSto | ControlSi));
            status = StatusIdle;
        }

        private void SetStatus(byte newStatus)
        {
            status = newStatus;
            control = (byte)(control | ControlSi);
        }

        private void UpdateInterrupt()
        {
            IRQ.Set((control & ControlEns1) != 0 && (control & ControlSi) != 0);
        }

        private const byte ControlAa = 0x04;
        private const byte ControlSi = 0x08;
        private const byte ControlSto = 0x10;
        private const byte ControlSta = 0x20;
        private const byte ControlEns1 = 0x40;

        private const byte StatusStart = 0x08;
        private const byte StatusRepeatedStart = 0x10;
        private const byte StatusAddressWriteAck = 0x18;
        private const byte StatusAddressWriteNack = 0x20;
        private const byte StatusDataWriteAck = 0x28;
        private const byte StatusDataWriteNack = 0x30;
        private const byte StatusAddressReadAck = 0x40;
        private const byte StatusAddressReadNack = 0x48;
        private const byte StatusDataReadAck = 0x50;
        private const byte StatusDataReadNack = 0x58;
        private const byte StatusIdle = 0xF8;

        private readonly List<byte> writeBuffer;
        private II2CPeripheral selected;
        private byte control;
        private byte status;
        private byte data;
        private byte slaveAddress0;
        private byte slaveAddress1;
        private byte smbus;
        private byte frequency;
        private byte glitch;

        private enum Registers : long
        {
            Control = 0x00,
            Status = 0x04,
            Data = 0x08,
            Slave0Address = 0x0C,
            SMBus = 0x10,
            Frequency = 0x14,
            GlitchRegLength = 0x18,
            Slave1Address = 0x1C
        }
    }
}
