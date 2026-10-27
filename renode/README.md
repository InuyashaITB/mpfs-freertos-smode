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
machine LoadPlatformDescription @renode/mpfs-sim-peripherals.repl
```

| Device | Model | Attached to | Phase |
|---|---|---|---|
| `loopback` | `SPI.SpiLoopback`: answers each byte with its complement | `spi1` | 1 (test) |
