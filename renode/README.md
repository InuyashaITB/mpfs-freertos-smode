# Renode files

| File | What |
|---|---|
| `mpfs-ddr-38bit-cached-3gib.repl` | Adds 2 GiB of DDR at `0x10_4000_0000` for the Sv39 DDR window |
| `mpfs-sim-peripherals.repl` | Simulated devices on the MSS SPI/I2C controllers and the fabric (LiteEth) |
| `models/*.cs` | C# device models, compiled by Renode at run time |
| `run-demo.resc` | Two boards, each loaded as the launcher loads one, on one Ethernet switch; runs the demo without a debugger, each board's MMUART1 to a file |
| `expected-uart.txt` | Lines both boards' UART output must contain (one per self-test) |
| `expected-uart-board0.txt` | Lines only board 0 must show (the injected protocol event) |
| `check-uart.sh` | Checks a UART log against expected-line files (POSIX sh / busybox sh) |

Models are written against the **Renode 1.13** API (SoftConsole v2022.2):
`Machine`, not the newer `IMachine`. Renode compiles a model when it is
included; restart Renode after editing one.

The debug launcher loads everything after the board's platform. By hand, in
the Renode monitor (paths relative to the project):

```
machine LoadPlatformDescription @renode/mpfs-ddr-38bit-cached-3gib.repl
include @renode/models/ProtoSpiDevice.cs
include @renode/models/NorFlash.cs
include @renode/models/MpfsI2c.cs
include @renode/models/I2cEeprom.cs
machine UnregisterFromParent sysbus.i2c0      # replaced by MpfsI2c in the repl
machine LoadPlatformDescription @renode/mpfs-sim-peripherals.repl
sysbus.eth MAC "02:4d:50:46:53:00"            # must match the firmware's (a repl cannot set it)
```

## Headless run: `make sim`

Builds, runs the demo on **two boards** in Renode with no windows and no
debugger, and checks each board's UART1. The boards share one Ethernet
switch, so the raw-frame test (Phase 5a) has a peer. From the project root, in the same shell you build from (SoftConsole's
`build_tools\bin` on `PATH` on Windows):

```
make sim RENODE=C:/Microchip/SoftConsole-v2022.2-RISC-V-747/<renode dir>/Renode.exe
```

`RENODE` can also be set in the environment. Forward slashes avoid quoting
problems.

* **Outputs (`build/sim/`):** `uart1.txt` and `uart1-board1.txt` are the
  boards' MMUART1. `renode.log` is Renode's console, including both boards'
  model counters printed at the end.
* **The boards** run the same ELF and differ only in:
  * the GPIO2_0 strap, which is the board id (board 1: `sysbus.gpio2 OnGPIO
    0 true`, after configuring the pin as an input: `MPFS_GPIO` ignores
    input changes on a pin that isn't one yet);
  * the LiteEth MAC, `02:4d:50:46:53:<id>`, matching the firmware's.
  * Board 0 also gets the injected protocol event.
* **Result:** `check-uart: PASS (N expected lines found ...)`, or a `FAIL`
  line for each problem, and make stops with an error. The check fails if:
  * UART1 is empty (Renode did not start or a script command failed: see
    `renode.log`);
  * a line contains `FAIL` or `no response`, or starts with `*** ` (a fault
    report from the libraries);
  * a line of `expected-uart.txt` is missing (on either board), or a line of
    `expected-uart-board0.txt` (on board 0).
* **Options:**
  * `SIM_TIME=10`: emulated seconds after the first one, in which the script
    injects a `spi1.proto` event. The default 3 takes about a minute of real
    time (two boards).
  * `RENODE_FLAGS=...`: default `--disable-xwt --console`.
* **New self-test:** add its PASS line to `expected-uart.txt`. A new model
  also goes into `run-demo.resc` (`include`) and the launcher.

Renode 1.13 notes, which explain how the script is written:
* The UART file must be an absolute path. A relative one fails with "could
  not be created", so make passes `$(CURDIR)/build/sim/uart1.txt`.
* `$ORIGIN` is not expanded inside paths. So the script's other paths are
  relative to the working directory, and make runs Renode in the project
  root.
* If a script command fails, Renode stops the script and waits at its
  prompt. make gives it end-of-file on stdin, so it exits instead of hanging.
* A macro defined while a machine is selected is only visible from that
  machine, so the script defines its macros before `mach create`.
* A repl cannot set the `MACAddress` property (type mismatch), but the
  monitor can: `sysbus.eth MAC "02:..."`.

By hand (Renode started in the project root, e.g. from its monitor window):

```
$uart_log=@C:/path/to/uart1.txt
$uart_log_b=@C:/path/to/uart1-board1.txt
include @renode/run-demo.resc
```

Tested with the Linux build of Renode 1.13.1. On Windows, if Renode rejects
`--console`, try `make sim RENODE=... RENODE_FLAGS=--disable-xwt`.

## Checking that the models loaded

The launcher sends these commands through gdb (`monitor ...`). Errors from
them, such as a model that does not compile, do not show up in the Renode log
window. If a self-test reports "no response", check in the Renode monitor
(`machine-0` prompt):

```
peripherals                                   # spi0: 'flash', spi1: 'proto', i2c0 (MpfsI2c): 'eeprom'
include @<project>/renode/models/ProtoSpiDevice.cs
include @<project>/renode/models/NorFlash.cs
include @<project>/renode/models/MpfsI2c.cs
include @<project>/renode/models/I2cEeprom.cs
```

The `include` prints compile errors, if any. Including a model twice is
harmless.

An SPI controller with no device attached is worse than "no response":
Renode 1.13's `MPFS_SPI` stops the emulation with a fatal error on the first
transfer. If the UART output ends at a `starting` line, look for `Fatal error`
in the Renode log.

| Device | Model | Attached to | Phase |
|---|---|---|---|
| `proto` | `SPI.ProtoSpiDevice`: example request/response protocol, data-ready line (replaced the Phase 1 loopback) | `spi1`, `DataReady` → PLIC 119 (F2M_1) | 4 |
| `eth` | `Network.LiteX_Ethernet` (built in): LiteEth soft MAC | fabric FIC3 `0x4000_0000`, IRQ → PLIC 120 (F2M_2) | 5a |
| `flash` | `SPI.NorFlash`: 16 MiB serial NOR, JEDEC ID `EF 40 18` | `spi0` | 2 |
| `i2c0` | `I2C.MpfsI2c`: MSS I2C controller, **replaces** the built-in `MPFS_I2C` | `sysbus` `0x2010A000`, PLIC 58 | 3 |
| `eeprom` | `I2C.I2cEeprom`: AT24C256-like, 32 KiB, 64-byte pages | `i2c0` address `0x50` | 3 |

## Soft Ethernet MAC (`eth`, Renode's `LiteX_Ethernet`)

LiteEth in the fabric, behind FIC3. The library's Sv39 table identity maps
gigapage 1 (`0x4000_0000`–`0x7FFF_FFFF`, RW) for it.

| Region | Address | Contents |
|---|---|---|
| CSRs | `0x4000_0000` | Old LiteX layout: 8-bit subregisters every 4 bytes, MSB first |
| MDIO | `0x4000_1000` | Bit-banged PHY interface (no PHY attached yet: Phase 5b) |
| Buffers | `0x4001_0000` | 2 RX ("writer") slots, then 2 TX ("reader") slots, 0x800 bytes each, 32-bit accesses |

* **CSRs:**
  * `WriterSlot` 0x00 (reading it latches the slot), `WriterLength`
    0x04–0x10, `WriterEvPending` 0x28 (write 1: release the latched slot),
    `WriterEvEnable` 0x2C;
  * `ReaderStart` 0x30, `ReaderReady` 0x34, `ReaderSlot` 0x3C,
    `ReaderLength` 0x40/0x44, `ReaderEvPending` 0x4C, `ReaderEvEnable` 0x50.
* **Renode 1.13 quirks:**
  * The received length includes the FCS plus `len % 60` extra zero bytes.
    That is a padding bug, so take the frame length from the payload.
  * There are only two RX slots: a frame arriving while both are full is
    dropped (with a warning).
  * Frames are sent with the FCS appended; send at least 60 bytes.
* **The switch** (`emulation CreateSwitch`) delivers a unicast frame only to
  the interface whose `MAC` matches the destination, and broadcasts to all.
  So the model's MAC must equal the firmware's: `sysbus.eth MAC
  "02:4d:50:46:53:00"` in the launcher, and per board in `run-demo.resc`.
  The repl cannot set it.
* **Firmware:** `examples/freertos-demo/liteeth.c` and the raw-frame test in
  `periph_selftest.c`:
  * Each board broadcasts HELLO frames (EtherType 0x88B5) and answers a
    peer's HELLO with a unicast ACK.
  * Result: `[S] ETH: raw frames with board <n>: PASS`.
  * A single board, as with the SoftConsole launcher, reports `no peer
    answered (single board?)` after 3 s, which is not a failure.

## Protocol device (`spi1.proto`, `models/ProtoSpiDevice.cs`)

An example of a custom SPI device: framed requests, a response the master
polls for, a register file, NAKs, and a data-ready line for events the
device starts. CRC-8 is polynomial 0x07, init 0x00.

* **Request**, one transfer: `A5 | cmd | len | payload[len] | crc8(cmd, len,
  payload)`, with len ≤ 32.
* **Response**, read in later transfers:
  * The master clocks filler (`FF`). The device answers `00` until the
    response is ready (`ResponseDelay` bytes, default 2), then `5A`, then
    `status | len | payload | crc8(status, len, payload)`.
  * The response position survives chip select. A transfer starting with
    `A5` begins a new request and drops an unread response.
  * A transfer that ends in the middle of a request drops it
    (`AbortedRequests`).

| cmd | Name | Payload → response |
|---|---|---|
| `01` | PING | – → `C5 01` (ID, version) |
| `02` | ECHO | bytes → the same bytes |
| `03` | READ_REG | addr → value |
| `04` | WRITE_REG | addr, value → – |
| `05` | GET_EVENTS | – → up to 16 queued event bytes (removed) |

* **Registers:** 16 × 8 bit; 0 (ID) and 1 (version) are read-only.
* **Status:** `00` OK; NAK `81` BAD_CRC, `82` UNKNOWN_CMD, `83` BAD_LEN,
  `84` BAD_ADDR, `85` READ_ONLY.
* **Events:** `DataReady` is high while events are queued. It is wired to
  PLIC 119 (F2M_1).
  * The app's handler disables the source and wakes the console task, which
    fetches the events with GET_EVENTS, prints
    `[S] SPI1 proto: data-ready interrupt, events: ...` and re-enables the
    source.
  * In SoftConsole, type `spi1.proto InjectEvent 0x42` in the Renode
    monitor at any time. `make sim` does this after 1 s.
* **Monitor:** `spi1.proto InjectEvent <byte>`, `SetRegister <a> <v>`,
  `GetRegister <a>`, `ResponseDelay 5` (set), `Requests`, `Naks`,
  `AbortedRequests`, `EventsQueued`, `EventsDropped`.

Firmware: `examples/freertos-demo/proto_dev.c`, on the shared bounded
transfer in `spi_xfer.c`, which `spi_flash.c` uses too.

## I2C controller (`i2c0`, `models/MpfsI2c.cs`)

Renode 1.13's built-in `MPFS_I2C` cannot drive an EEPROM with the vendor
`MSS_I2C` driver:
* It calls the device's `Write()` once per byte, with no start or stop, so a
  device cannot tell a new write's address bytes from data.
* A master read never reaches status 0x58 (last byte NACKed). With exactly
  N bytes the driver waits forever; with more, it writes past its buffer.

So `i2c0` is replaced by our own model. The built-in one is unregistered
first (`machine UnregisterFromParent sysbus.i2c0` in the launcher and in
`run-demo.resc`), then `mpfs-sim-peripherals.repl` declares
`i2c0: I2C.MpfsI2c` at the same address and PLIC line. `i2c1` keeps the
built-in model.

* **Hardware semantics** (CoreI2C status codes):
  * STA while idle sends START (0x08, SI set). Every other action happens
    when software clears SI, from the status and STA/STO/AA/DATA:
    * send the address: 0x18/0x20 for write, 0x40/0x48 for read;
    * send a data byte: 0x28/0x30;
    * receive with ACK (0x50) or NACK (0x58), depending on AA;
    * repeated START (0x10), or STOP (0xF8).
  * `IRQ = ENS1 && SI`. Master mode only.
* **Devices** (children by 7-bit address):
  * one `Write(bytes)` per written part of a transaction, at the repeated
    START or STOP that ends it;
  * `Read(1)` per received byte;
  * `FinishTransmission()` at STOP.
  * The address is NACKed if nothing is registered there, or if the device
    has a public `bool IsBusy()` that returns true. It is looked up by
    reflection, so device models need no shared type.
* **Monitor:** `i2c0 Transactions`, `AddressNacks`, `LastStatus`.

## I2C EEPROM (`i2c0.eeprom`, `models/I2cEeprom.cs`)

AT24C256-like: 32 KiB, 64-byte pages, 2-byte big-endian memory address.
* **Write:** a write transaction starts with the 2 address bytes. Data after
  them is written with **page wrap**: past the end of the 64-byte page it
  continues at the start of the same page. A write of only the 2 address
  bytes sets the pointer (the first half of a random read).
* **Read:** reads continue from the pointer and increment it, so
  current-address reads work.
* **Write cycle:** after a data write the device NACKs its address for
  `BusyPolls` address phases (default 3), so drivers must ACK-poll.
* **Monitor:** `i2c0.eeprom Writes`, `BytesRead`, `BusyPolls 10` (set),
  `WriteProtect true` (writes ACKed but ignored), `Erase`, `SaveImage
  "<file>"` / `LoadImage "<file>"`.

The firmware side is `examples/freertos-demo/i2c_eeprom.c`, on the vendor
`MSS_I2C` driver, interrupt-driven from S-mode.

## SPI NOR flash (`spi0.flash`)

`models/NorFlash.cs` behaves like a 3-byte-address serial NOR (W25Q / MT25Q
command subset): `9F` RDID, `05` RDSR, `06` WREN, `04` WRDI, `03` READ, `0B`
FAST_READ, `02` page program, `20` 4 KiB sector erase, `D8` 64 KiB block
erase, `C7`/`60` chip erase.

* Page program only clears bits (`memory &= data`) and wraps inside its
  256-byte page. Erase sets bytes to `0xFF`.
* Program and erase need WREN and clear the latch when they finish. They
  commit when the command ends. The part then reports busy (WIP) for
  `BusyPolls` status reads (default 3), and ignores any other command while
  busy, as a real part does. Ignored commands are logged as warnings.
* **One command per transfer.** Renode 1.13's `MPFS_SPI` ends the slave's
  command when the frame count programmed in `CONTROL` is reached, and again
  when slave select is released. So a driver must send the opcode, address
  and data of a command as one transfer, with the frame count set to its
  whole length. `examples/freertos-demo/spi_flash.c` does this, and so does
  the vendor `MSS_SPI_transfer_block()`. Splitting a command over two
  transfers inside one chip select does not work in Renode.
* Contents survive a machine reset, not a Renode restart. To keep them:
  `spi0.flash SaveImage "C:/path/flash.bin"`, and later `LoadImage` with the
  same path.
* Monitor: `spi0.flash Programs`, `Erases`, `Commands`, `IgnoredCommands`,
  `WriteEnabled`, `Busy`, `BusyPolls 10` (set), `Erase` (whole array).

Every flash transfer logs `spi0: Unhandled write to offset 0x0 ... SPS,
CLKMODE` and `offset 0x50` (`FRAMESUP`) as warnings. `MPFS_SPI` does not
model those bits, and the warnings are harmless.
