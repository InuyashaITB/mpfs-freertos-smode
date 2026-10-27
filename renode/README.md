# Renode files

| File | What |
|---|---|
| `mpfs-ddr-38bit-cached-3gib.repl` | Adds 2 GiB of DDR at `0x10_4000_0000` for the Sv39 DDR window |
| `mpfs-sim-peripherals.repl` | Simulated devices on the MSS SPI/I2C controllers |
| `models/*.cs` | C# device models, compiled by Renode at run time |
| `run-demo.resc` | Loads the board and devices as the launcher does, runs the demo without a debugger, MMUART1 to a file |
| `expected-uart.txt` | Lines the UART output must contain (one per self-test) |
| `check-uart.sh` | Checks a UART log against `expected-uart.txt` (POSIX sh / busybox sh) |

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

## Headless run: `make sim`

Builds, runs the demo in Renode with no windows and no debugger, and checks
UART1. From the project root, in the same shell you build from (SoftConsole's
`build_tools\bin` on `PATH` on Windows):

```
make sim RENODE=C:/Microchip/SoftConsole-v2022.2-RISC-V-747/<renode dir>/Renode.exe
```

`RENODE` can also be set in the environment. Forward slashes avoid quoting
problems.

* **Outputs (`build/sim/`):** `uart1.txt` is MMUART1. `renode.log` is
  Renode's console, including the model counters printed at the end.
* **Result:** `check-uart: PASS (N expected lines found ...)`, or a `FAIL`
  line for each problem, and make stops with an error. The check fails if:
  * UART1 is empty (Renode did not start or a script command failed: see
    `renode.log`);
  * a line contains `FAIL` or `no response`, or starts with `*** ` (a fault
    report from the libraries);
  * a line of `expected-uart.txt` is missing.
* **Options:**
  * `SIM_TIME=10`: emulated seconds; the default 3 takes about 20 s real time.
  * `RENODE_FLAGS=...`: default `--disable-xwt --console`.
* **New self-test:** add its PASS line to `expected-uart.txt`; a new model
  also goes into `run-demo.resc` (`include`) and the launcher.

Renode 1.13 notes, which explain how the script is written:
* The UART file must be an absolute path. A relative one fails with "could
  not be created", so make passes `$(CURDIR)/build/sim/uart1.txt`.
* `$ORIGIN` is not expanded inside paths. So the script's other paths are
  relative to the working directory, and make runs Renode in the project
  root.
* If a script command fails, Renode stops the script and waits at its
  prompt. make gives it end-of-file on stdin, so it exits instead of hanging.

By hand (Renode started in the project root, e.g. from its monitor window):

```
$uart_log=@C:/path/to/uart1.txt
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
