# Renode files

| File | What |
|---|---|
| `mpfs-ddr-38bit-cached-3gib.repl` | Adds 2 GiB of DDR at `0x10_4000_0000` for the Sv39 DDR window |
| `mpfs-sim-peripherals.repl` | Simulated devices on the MSS SPI/I2C controllers |
| `models/*.cs` | C# device models, compiled by Renode at run time |

Models are written against the **Renode 1.13** API (SoftConsole v2022.2):
`Machine`, not the newer `IMachine`. Renode compiles a model when it is
included; restart Renode after editing one.

The debug launcher loads everything after the board's platform. By hand, in
the Renode monitor (paths relative to the project):

```
machine LoadPlatformDescription @renode/mpfs-ddr-38bit-cached-3gib.repl
include @renode/models/SpiLoopback.cs
include @renode/models/NorFlash.cs
machine LoadPlatformDescription @renode/mpfs-sim-peripherals.repl
```

## Checking that the models loaded

The launcher sends these commands through gdb (`monitor ...`). Errors from
them, such as a model that does not compile, do not show up in the Renode log
window. If a self-test reports "no response", check in the Renode monitor
(`machine-0` prompt):

```
peripherals                                   # spi0: 'flash', spi1: 'loopback'
include @<project>/renode/models/SpiLoopback.cs
include @<project>/renode/models/NorFlash.cs
```

The `include` prints compile errors, if any. Including a model twice is
harmless.

| Device | Model | Attached to | Phase |
|---|---|---|---|
| `loopback` | `SPI.SpiLoopback`: answers each byte with its complement | `spi1` | 1 (test) |
| `flash` | `SPI.NorFlash`: 16 MiB serial NOR, JEDEC ID `EF 40 18` | `spi0` | 2 |

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
