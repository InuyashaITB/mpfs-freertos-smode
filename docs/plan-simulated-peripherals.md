# Plan: simulated peripherals (branch `sim-peripherals`)

Goal: find out, and then build, what it takes to add simulated peripherals to
this project. The project runs FreeRTOS in S-mode on U54_1 with Sv39, in
Renode. Four targets:

1. **SPI NOR flash** on an MSS SPI controller. True NOR semantics: program
   only clears bits (1 → 0); erase sets a sector back to all 1s.
2. **Custom SPI device** with its own request parsing and responses
   (bidirectional).
3. **I2C EEPROM** on an MSS I2C controller.
4. **Fabric soft MAC** with **FreeRTOS+TCP**.

Each target is split into a Renode model (the device), a driver in the
firmware, and a demo task that proves it works on UART1.

## 1. What already exists

Sources: the `renode-infrastructure` peripheral sources (master), and the
`polarfire-soc.repl` from **SoftConsole's Renode 1.13**. The repl confirms
the controllers, addresses and PLIC lines below.

| Piece | Status | Notes |
|---|---|---|
| MSS SPI controller | **modelled (confirmed in 1.13)**: `spi0` (`SPI.MPFS_SPI` @ `0x20108000`, PLIC 54), `spi1` (`0x20109000`, PLIC 55) | One slave per controller (no chip-select routing); `SLAVE_SELECT` → 0 ends a transfer (`FinishTransmission`). FIFO, frame size, interrupts modelled. |
| MSS I2C controller | **modelled (confirmed in 1.13)**: `i2c0` (`I2C.MPFS_I2C` @ `0x2010A000`, PLIC 58), `i2c1` (`0x2010B000`, PLIC 61) | Slaves by 7-bit address. Master mode with standard status codes. Slave gets `Write(byte[])` per transaction and `Read()` one byte at a time. **Never calls `FinishTransmission`.** |
| SPI NOR flash device | partly | `GenericSpiFlash` / `Micron_MT25Q` exist, but page program **overwrites** (`WriteByte`); there's no 1 → 0 AND. A custom model is needed. |
| I2C EEPROM device | **missing** | No AT24-style model. `MB85RC1MT` (FRAM, about 150 lines) is the closest reference. |
| Custom SPI device | **missing** | By definition. |
| Fabric soft MAC | **modelled (confirmed in 1.13)**: `Network.LiteX_Ethernet` (LiteEth) | Slot-based TX/RX buffer window (2 + 2 slots of 2 KiB), **no DMA**, one IRQ, bit-banged MDIO to an `EthernetPhysicalLayer`. Not in the MPFS platform; we place it on a fabric (FIC) address. Note: its `Slot.TryWrite` padding looks wrong for short frames, to verify. |
| Hard MAC (for reference) | modelled (`mac0`/`mac1`, `CadenceGEM`) | Not used: you chose a fabric soft MAC. |
| Microchip drivers | removed in `9b7725d` (cleanup); in git history | `mss_spi` (1441 lines), `mss_i2c` (1511). LiteEth has no Microchip driver; we write a small one. |
| PLIC | `prioritiesEnabled: false` in 1.13 | Priorities are ignored in simulation; thresholds and enables still apply. |
| Address map in 1.13 | `pcie1` ECAM occupies `0x6000_0000`–`0x7FFF_FFFF` (all of FIC0) | A fabric peripheral must go elsewhere: FIC3 `0x4000_0000` onwards is free (PCIe control regs sit at `0x5300_4000`/`0x5300_8000`). |
| Adding models to Renode | **supported (confirmed in 1.13)** | C# peripherals compile at run time: `include @renode/models/X.cs`, then `machine LoadPlatformDescription @renode/x.repl` (as we already do for the DDR repl). Restart Renode after editing a `.cs`. Models must target the **1.13 API**: e.g. `Machine`, not master's `IMachine`, and the older register/collection helpers. Master's sources are a reference, not drop-in code. |

## 2. Cross-cutting work (Phase 1, before any device)

### 2.1 S-mode and the HAL PLIC functions (blocker)

`mss_spi.c` and `mss_i2c.c` call the HAL's `PLIC_EnableIRQ` /
`PLIC_DisableIRQ` (inline in `mss_plic.h`), including inside `*_init()`.
These read `mhartid`, which is an **illegal instruction in S-mode**.

The fix belongs in the library, not in each driver. `mss_plic.h` picks
the PLIC context from `mhartid`; it will instead ask a helper that answers
"hart 1 S-context" once `m_shim_enter_smode()` has run. The shim sets a flag
right before `mret`; before that, M-mode code keeps the M context. Drivers
then work unchanged from both `smode_m_prehook()` and S-mode tasks.
- Touches: `mss_plic.h` (vendor HAL, in `src/platform`, not middleware),
  `m_shim.c`.
- This changes LIBRARY-FIXED behaviour, so it gets documented.
- `PLIC_SetPriority` has no hart dependency; claim/complete already go
  through `SPLIC_*`.

### 2.2 Drivers and build

- Restore `src/platform/drivers/mss/mss_spi` and `mss_i2c` from `9b7725d^`.
- Add both to the default `MPFS_DRIVERS` (make and CMake).
- Clocks and resets for SPI0/1, I2C0/1 and the FIC go in the demo's
  `smode_m_prehook()` (`mss_config_clk_rst`, M-mode).
- PLIC sources are enabled from S-mode in `s_mode_main()`.

### 2.3 Renode project layout

```
renode/models/NorFlash.cs         SPI NOR flash with AND-programming
renode/models/ProtoSpiDevice.cs   custom request/response SPI device
renode/models/I2CEeprom.cs        AT24C256-like EEPROM
renode/mpfs-sim-peripherals.repl  attaches them (+ the LiteEth MAC, PHY, switch)
renode/mpfs-sim-peripherals.resc  include @models/*.cs; LoadPlatformDescription; networking
```

- The launcher's "other init commands" gain the `include` lines, next to
  the existing DDR repl.
- Optional: Renode Robot Framework tests (`renode-test`) that boot the demo
  and check UART1 lines, one per peripheral.

### 2.4 Memory budget

L2LIM is 255 KiB. The demo today uses about 57 KiB of code and 108 KiB of
`.bss`, including the 32 KiB FreeRTOS heap and the stacks.
- Flash, EEPROM and the SPI device add little.
- **FreeRTOS+TCP will not fit next to that.** Plan to move the FreeRTOS
  heap and the network buffers into the DDR window, which is already mapped
  RW. With a no-DMA MAC, buffers there need no address translation. This
  changes `FreeRTOSConfig.h` (heap placement; LIBRARY-FIXED) and adds a
  DDR section to the linker layout. Prefer `configAPPLICATION_ALLOCATED_HEAP`
  with an app-provided `ucHeap` in a DDR section, or `heap_5` regions.

## 3. Feature: SPI NOR flash (`spi0`)

**Renode model `NorFlash : ISPIPeripheral`** (C#, about 250 lines,
modelled on `GenericSpiFlash`):
- **Commands:** `0x9F` JEDEC ID, `0x05` RDSR, `0x06` WREN, `0x04` WRDI,
  `0x03` READ, `0x0B` FAST_READ.
- **`0x02` PAGE PROGRAM:** `mem[a] &= data` (1 → 0 only); 256-byte page
  wrap; needs WEL.
- **Erase:** `0x20` 4 KiB sector, `0xD8` 64 KiB block, `0xC7` chip. Each
  sets bytes to `0xFF`.
- **Busy (WIP):** optional, for a number of virtual microseconds, so
  drivers must poll RDSR.
- **Backing store:** a `MappedMemory`, optionally file-backed so contents
  survive a restart.
- **Command boundary:** `FinishTransmission()`, i.e. the master releasing
  slave-select.
- **Attach:** `spiFlash: SPI.NorFlash @ spi0` (single slave).

**Firmware (app):** `spi_flash.c` on `MSS_SPI`, master, mode 0, 8-bit
frames.
- Each command is wrapped in `MSS_SPI_set_slave_select` /
  `MSS_SPI_clear_slave_select`.
- API: `read`, `program` (page-split), `erase_sector`, `wait_ready`,
  `read_id`.

**Demo task:**
1. Read the ID.
2. Erase sector 0 → all `0xFF`.
3. Program `0xA5`, then program `0x5A` over it → reads `0x00` (bits only
   clear).
4. Erase → `0xFF` again.
5. Print PASS/FAIL per step.

## 4. Feature: custom bidirectional SPI device (`spi1`)

**Example protocol** (half-duplex frames, master-clocked):
- **Request:** `0xA5 | cmd | len | payload[len] | crc8`.
- **Response:** the device holds an internal response buffer. The master
  clocks `0x00`s until it sees `0x5A`, then reads
  `status | len | payload | crc8`.
- **Commands:** `PING`, `ECHO`, `READ_REG(addr)`, `WRITE_REG(addr,val)`,
  `GET_EVENTS`.
- **Errors:** bad CRC or an unknown command answers `NAK` with a reason.
- **Data-ready line (optional):** the model raises a GPIO output when it
  has unsolicited events. It's wired to a GPIO2 input or an F2H PLIC line,
  so the app can be interrupt-driven.

**Renode model `ProtoSpiDevice : ISPIPeripheral`** (C#):
- A byte-wise parser state machine and a register file.
- Monitor commands to poke it from Renode (`spi1.proto InjectEvent 3`,
  `SetRegister`), for scripted tests.

**Firmware:** `proto_dev.c`, which frames requests, computes CRC8, polls
for the response marker with a timeout, and validates the response. The
demo task exercises each command and one deliberate bad-CRC request.

## 5. Feature: I2C EEPROM (`i2c0`)

**Renode model `I2CEeprom : II2CPeripheral`** (AT24C256-like: 32 KiB,
64-byte pages, 2-byte address):
- **Write:** each `Write(byte[])` is one transaction. The first two bytes
  set the address pointer; the rest are written with **page wrap** inside
  the 64-byte page.
- **Read:** `Read()` returns one byte at a time and auto-increments across
  the whole array. Current-address read is supported; a "random read" is a
  2-byte write followed by reads.
- `MPFS_I2C` never calls `FinishTransmission`, so the model relies only on
  `Write`/`Read`. *(Superseded: 1.13's `MPFS_I2C` turned out unusable, and
  `i2c0` is replaced by our own controller. See "Phase 3 status".)*
- **Optional:** write-cycle busy, where the device NAKs its address for
  about 5 ms of virtual time, for ACK polling; a WP input.
- **Attach:** `eeprom: I2C.I2CEeprom @ i2c0 0x50`.

**Firmware:** `eeprom.c` (became `i2c_eeprom.c`) on `MSS_I2C` as master, interrupt-driven. This
needs the §2.1 PLIC fix, or polled mode as a fallback. It uses
`MSS_I2C_write_read` for random reads and splits writes on page
boundaries.

**Demo:**
1. Write a block that crosses a page boundary. Done raw in one transaction
   to show the wrap, then correctly split.
2. Read back and print the results.

## 6. Feature: fabric soft MAC + FreeRTOS+TCP

**MAC choice (recommended): LiteEth.**
- Renode already has `LiteX_Ethernet`, and LiteEth can be generated for
  PolarFire fabric.
- With no DMA, the virtual/physical address issue (the MSS GEM driver
  truncates buffer addresses to 32 bits) doesn't exist.
- **Alternative:** Microchip **CoreTSE**. It is the "native" Libero soft
  MAC, but Renode has no model for it, so it would need a register-level
  C# model written from the CoreTSE handbook (DMA descriptors included)
  plus a driver. That's several times the work; it's a later option.

**Placement in the address map:**
- Put the MAC behind the **FIC3** window (`0x4000_0000`–`0x5FFF_FFFF`), e.g.:
  - CSR at `0x4000_0000`
  - MDIO at `0x4000_1000`
  - buffer window at `0x4001_0000`
- Not FIC0: in SoftConsole's Renode 1.13 the `pcie1` ECAM region covers
  `0x6000_0000`–`0x7FFF_FFFF`.
- Its IRQ goes to a fabric→PLIC line (`F2H_n`).
- In Renode: `eth: Network.LiteX_Ethernet @ { sysbus <csr>; sysbus new Bus.BusMultiRegistration { address: <buf>; size: <n>; region: "buffer" }; sysbus new ... "phy" }`,
  plus `IRQ -> plic@<F2H source>` and a PHY.

**Library changes:**
- **Sv39:** the library maps only gigapage 0 (`0x0000_0000`–`0x3FFF_FFFF`)
  with an identity mapping. The FIC3 window (`0x4000_0000`–`0x5FFF_FFFF`) is
  in gigapage 1, so it needs an identity mapping too: RW, non-executable,
  from a linker-script region. This changes `sv39.c`
  (LIBRARY-FIXED mapping policy), and the mapping is useful for any fabric
  IP.
- **FIC clock and reset:** set in the pre-hook (the HAL already turns on the
  FIC0–3 clocks in standalone mode).

**FreeRTOS+TCP (new library, as you chose):**
- Vendor `FreeRTOS-Plus-TCP` (a release compatible with Kernel V11.3.2)
  into `src/middleware/FreeRTOS-Plus-TCP`. It sits next to `FreeRTOS-Kernel`,
  which is not touched.
- New `libfreertos_tcp.a` in make and CMake, plus a
  `MpfsSmode::freertos_tcp` target and `MPFS_SMODE_TCP_LIBS`.
- `FreeRTOSIPConfig.h` from `FREERTOS_IP_CONFIG_DIR`, marked
  **LIBRARY-FIXED**, installed and recorded in `build_config` like
  `FreeRTOSConfig.h`.
- **NetworkInterface for LiteEth** (new, in the library):
  - `xNetworkInterfaceInitialise`: MDIO PHY reset and link.
  - `xNetworkInterfaceOutput`: copy into a free read slot, set the length,
    start.
  - RX: the IRQ defers to a task (`vSmodeYieldFromISR`), which copies the
    slot into a network buffer, acks the slot and calls
    `xSendEventStructToIPTask`.
  - Link status.
  - Uses `BufferAllocation_2`, buffers in DDR (§2.4).
- Renode networking is **user-mode only**: no TAP, since that needs
  administrator rights and a driver, which some platforms can't have.
  - **Test setup:** two simulated boards (`mach create` twice, same platform
    and repls) on one `emulation CreateSwitch "sw"`, each `connector
    Connect eth sw`, running the **same demo ELF**.
    - A GPIO2 input "strap", set per machine in the `.resc`, selects the
      role (TCP echo server or client).
    - The MAC and IP address are derived from the same strap.
    - This proves FreeRTOS+TCP end to end with no host privileges.
    - The SoftConsole launcher debugs machine 0; machine 1 runs free.
  - **Optional host access without admin:** a small C# `SocketBridge`
    model (`IMACInterface`) on the switch.
    - It tunnels raw Ethernet frames over an ordinary localhost TCP/UDP
      socket to a user-mode host helper (Python).
    - The helper starts with ARP/ICMP/UDP echo, and can grow to a
      pure-Python TCP/IP stack if host TCP is wanted.

**Demo:**
- Static IP (DHCP optional).
- ICMP ping reply.
- TCP echo server on port 7, plus a small TCP client task that connects to
  the peer.
- Link and IP stats on the console.

## 7. Phases

| Phase | Work | Done when |
|---|---|---|
| 0 | **Done.** In SoftConsole's Renode 1.13: `MPFS_SPI`, `MPFS_I2C`, addresses and PLIC lines confirmed; `LiteX_Ethernet` loads; `include @x.cs` compiles a probe model written against the 1.13 API (`Machine`), verified; no TAP (no admin) → user-mode networking | Findings folded into this plan |
| 1 | §2: PLIC S-mode fix, drivers restored, Renode scaffolding (repl/resc/launcher), prehook clocks | Demo still runs; a trivial SPI loop transfer works from S-mode. **Done** (see "Phase 1 status") |
| 2 | §3 SPI NOR flash | Flash demo PASS in Renode. **Implemented; passes in Renode 1.13.1** (see "Phase 2 status"); waiting for the SoftConsole run |
| 3 | §5 I2C EEPROM | EEPROM demo PASS. **Implemented; `make sim` passes in Renode 1.13.1** (see "Phase 3 status"); waiting for the SoftConsole run |
| 4 | §4 custom SPI device | Protocol demo PASS, incl. NAK path and injected event |
| 5a | §6 MAC: Sv39 fabric window, LiteEth model on FIC, bare driver (raw frame TX/RX through the switch) | Raw frames exchanged between the two simulated boards |
| 5b | FreeRTOS+TCP library + NetworkInterface + memory move to DDR | Ping + TCP echo between board 0 and board 1 |
| 5c | *(optional)* `SocketBridge` model + user-mode host helper | Host helper answers ARP/ping from a board |
| 6 | Docs (README, boot doc), Robot tests, LIBRARY-FIXED marking | Tests green; docs updated |

Phases 2–4 are independent once Phase 1 is done. Phase 5 is the largest
and riskiest.

## 8. Risks and open points

- **Renode 1.13 vs master:** the controllers exist in 1.13 (confirmed from
  its repl), but their behaviour above was read from master. 1.13's
  `MPFS_SPI`/`MPFS_I2C` may differ in detail, and `LiteX_Ethernet` is
  unconfirmed. Phase 0 settles this; if needed, we ship our own copies of
  models in `renode/models/`.
- **The §2.1 PLIC approach** changes vendor HAL behaviour. The alternative
  is to patch the two drivers. Decide in Phase 1.
- **L2LIM capacity** for TCP: heap and buffers move to DDR. On real
  hardware that needs DDR training (`DDR_SUPPORT`), not done in LIM
  standalone mode today.
- **Real hardware for the soft MAC:** this needs a Libero design with LiteEth
  (or CoreTSE) on a FIC, and regenerated `fpga_design_config` headers.
  Simulation-only in this plan.
- **No TAP** (no administrator rights). Host access exists only through
  the optional `SocketBridge` (5c); the main tests run between two
  simulated boards.
- **Two machines** roughly double the simulation load, so the TCP tests
  run slower in Renode.
- **1.13 API drift** for our C# models: keep them small and
  self-contained, and test each with `include` in 1.13 early.
- **`LiteX_Ethernet` short-frame padding** looks suspicious. We may need a
  model subclass, or the driver can pad frames to 60 bytes itself.

## Phase 1 status

Implemented, built (make and CMake, Debug and Release); not yet run in
Renode (no Renode in the build container).

- **HAL S-mode support** (`g_hal_smode_active`, defined in `mss_util.c`,
  set by `m_shim_enter_smode()` right before `mret`):
  - `mss_plic.h`: `PLIC_EnableIRQ/DisableIRQ/ClaimIRQ/CompleteIRQ/
    SetPriority_Threshold/init` use U54_1's S context instead of reading
    `mhartid`.
  - `hal/hal_irq.c`: `HAL_disable_interrupts/HAL_restore_interrupts` use
    `sstatus.SIE` instead of `mstatus`. Found while restoring the drivers:
    `mss_i2c` masks interrupts with them.
  - Checked in the ELF: every `mhartid` read in those helpers comes after
    the flag test; the SPI driver functions touch no machine CSR.
- **Drivers** `mss_spi`, `mss_i2c` restored from `9b7725d^` and added to the
  default `MPFS_DRIVERS`. Small fixes to build with this HAL:
  - includes point to `mpfs_hal/mss_hal.h` (they used the older
    `mpfs_hal/mss_plic.h` layout);
  - `mss_i2c.c`'s private `disable_interrupts`/`restore_interrupts`
    renamed (clash with `mss_util.h`) and keep the full `psr_t`.
- **Prehook:** SPI0/1 and I2C0/1 clocks and resets on.
- **Renode:**
  - `renode/models/SpiLoopback.cs`: a test slave that answers each byte with
    its complement; 1.13 API.
  - `renode/mpfs-sim-peripherals.repl` attaches it to `spi1`.
  - `renode/README.md` describes the files.
  - The debug launcher includes the model and loads the repl after the DDR
    repl.
- **Demo:** `periph_selftest.c` runs `spi_loopback_selftest()` in
  `s_mode_main()` (S-mode, before the scheduler, polled):
  `MSS_SPI_init()` + master mode 0 + 16 × `MSS_SPI_transfer_frame()` on SPI1.
  Expected UART1 line:
  `[S] SPI1 loopback (S-mode, MSS_SPI driver): 16 frames OK`.

**First Renode run: hang after the DDR lines.**
- **Symptom:** UART1 stopped after `[S] DDR buf2 ...`, and the debug session ended with
  "Remote doesn't know how to detach".
- **Cause:** the self-test used `MSS_SPI_transfer_frame()`, which polls
  `STATUS` without a timeout. With no answering device on `spi1`, or a
  1.13 `MPFS_SPI` that doesn't raise the bits it waits for, the boot hung
  there.
- **Fix:**
  - The self-test now does the same steps with both waits bounded. It
    reports `no response` with the stage that timed out (TX done / RX
    ready), and prints `[S] SPI1 loopback: starting` first.
  - `SpiLoopback.cs` no longer uses the logging API (bare 1.13 interface).
  - `renode/README.md` explains how to check that models loaded: errors
    from the launcher's `monitor include` are not shown in the Renode log.

**Second Renode run: `no response, 16 of 16 frames timed out (TX done 0,
RX ready 16)`.**
- The boot no longer hangs and the scheduler runs.
- TX done came on every frame, so the device on `spi1` is attached and
  transmitting.
- **Cause:** `MPFS_SPI` sets `STATUS.RXDATRCVD` (the driver's
  `RX_DATA_READY`) only when the received byte count is a multiple of the
  frame size *in bits* (every 8 bytes for 8-bit frames). Each frame here
  flushes the FIFO first, so it never comes.
- **Fix:** wait for `RXFIFOEMPTY` to clear before reading `RX_DATA`, which
  is equally valid on hardware. The report says on how many frames
  `RX_DATA_READY` was also set.
- **For later phases:** the vendor `MSS_SPI_transfer_frame()` cannot be used
  in Renode 1.13 as is; `MSS_SPI_transfer_block()` (which watches the FIFO
  flags) is the one to check for the flash and protocol drivers.

**Debugger not staying attached.**
- **Symptom:** the launch loaded and started the program, then the session
  ended ("Remote doesn't know how to detach"); no pause or breakpoints.
- **Cause:** the launcher's "Load symbols" step (`symbol-file`) runs after
  connecting, at a physical PC with only virtual symbols. gdb 9.1 fails with
  "Argument to arithmetic operation not a number or boolean", and Eclipse
  aborts the launch.
- **Fix:** "Load symbols" and semihosting turned off in
  `mpfs-freertos-smode Renode All-harts Debug.launch`. The symbols still come
  from the `file` command in "Other gdb client commands". README "Debugging
  notes" explains both.
- **Then:** the launch got past `load`, but `tbreak s_mode_main` (the stop-at
  step) failed with "Cannot access memory at address 0xffffffc008001bd8",
  and Eclipse ended the session again. With `use-compressed-breakpoints`
  set to `auto`, gdb 9.1 reads the instruction at the (virtual) breakpoint
  address while the MMU is still off.
- **Fix:** `set riscv use-compressed-breakpoints on` in the launcher. Renode
  handles breakpoints through `Z0` and ignores their size.

**Third Renode run: loopback passes.** UART1:
`[S] SPI1 loopback (S-mode, MSS_SPI driver): 16 frames OK (STATUS.RX_DATA_READY seen on 0)`.
`RX_DATA_READY` never came, as predicted. The scheduler runs normally after
it. With both launcher fixes the debugger stays attached (confirmed). Phase 1
is done.

## Phase 2 status

**Implemented, and it passes in Renode 1.13.1.** Checked with the official
Linux portable release, the same version as SoftConsole's. The board was
loaded as the launcher loads it and run headless. UART1:

```
[S] SPI0 flash: starting
[S] SPI0 flash: JEDEC ID ef 40 18
[S] SPI0 flash: erase sector -> 0xFF               PASS (busy for 3 status reads)
[S] SPI0 flash: program 0xA5 -> 0xA5               PASS (busy for 3 status reads)
[S] SPI0 flash: program 0x5A over it -> 0x00       PASS (busy for 3 status reads)
[S] SPI0 flash: program across a page boundary     PASS (busy for 6 status reads)
[S] SPI0 flash: erase sector again -> 0xFF         PASS (busy for 3 status reads)
[S] SPI0 flash (S-mode, NorFlash model): PASS
```

The model's counters agree: 4 page programs, 2 erases, 0 ignored commands.

- **Model:** `renode/models/NorFlash.cs`, attached as `spi0.flash` in
  `mpfs-sim-peripherals.repl`; the launcher includes it. Behaviour and
  monitor commands are in `renode/README.md`. Before Renode, it was also
  unit-tested with a C# harness (Mono) that copies `MPFS_SPI`'s calls.
- **`MPFS_SPI` finding (1.13 source):** in master mode the controller calls
  the slave's `FinishTransmission()` when the received frame count reaches
  `CONTROL.FRAMECNT`, and again when `SLAVE_SELECT` goes to 0.
  - The model ends a command on the first call; the second call finds
    nothing in progress.
  - Drivers must send each command (opcode + address + data) as one transfer
    with the frame count set to its length, as `MSS_SPI_transfer_block()`
    does.
  - `TX_FIFO_FULL` / `RX_FIFO_EMPTY` follow the queues, so FIFO-flag polling
    works. The FIFO limit is `fifoSize × frameSize` = 32 × 8 after the driver
    sets BIGFIFO.
- **Driver:** `examples/freertos-demo/spi_flash.c` (app side), with
  `read_id`, `read`, `program` (split at pages), `erase_sector`,
  `wait_ready`.
  - Its transfer is `MSS_SPI_transfer_block()`'s algorithm, bounded, so a
    missing model reports "no response" instead of hanging.
  - It checks WEL after WREN, so a command the device ignored shows up as an
    error.
- **Demo:** `spi_flash_selftest()` in `periph_selftest.c`, run from
  `s_mode_main()` after the loopback. It uses the sector at `0x10000`.
- **Noise:** each transfer logs `spi0: Unhandled write ... SPS, CLKMODE` and
  `offset 0x50` (FRAMESUP) warnings. They are harmless and noted in the
  Renode README.

## Headless run (`make sim`)

Added after Phase 2, so every later phase has a one-command check:
- `renode/run-demo.resc` loads the board and devices as the launcher does,
  runs the demo and writes MMUART1 to `build/sim/uart1.txt`.
- `renode/check-uart.sh` fails on `FAIL`, `no response` or a `*** ` fault
  report, and on any line of `renode/expected-uart.txt` that is missing.

Usage and Renode 1.13 quirks are in `renode/README.md`. Phases 3–5 are done
when their PASS line is in `expected-uart.txt` and `make sim` passes. It is
also the base for Phase 6's tests.

## Phase 3 status

**Implemented; `make sim` passes in Renode 1.13.1** (5 expected lines).
UART1:

```
[S] I2C0 EEPROM: raw write at 0x3C wraps to 0x00      PASS
[S] I2C0 EEPROM: split write of 100 bytes at 0x70     PASS (3 pages, 9 busy ACK polls)
[S] I2C0 EEPROM: random + current-address read        PASS
[S] I2C0 EEPROM: absent device 0x51 is NACKed         PASS (status 2, expected 2 = NACK)
[S] I2C0 EEPROM (S-mode, MSS_I2C driver, interrupts): PASS
```

- **Built-in controller unusable:** reading 1.13's `MPFS_I2C.cs` showed it
  calls the device's `Write()` once per byte, with no start or stop. Its
  master read never reaches 0x58, so with the vendor ISR it either waits
  forever or writes one byte past the buffer.
- **Our controller:** `renode/models/MpfsI2c.cs`, a CoreI2C master with
  actions on SI clear, one `Write()` per transaction, `Read(1)` per byte,
  and address NACK for absent or busy devices (`IsBusy()` by reflection).
  - It replaces `i2c0`: `machine UnregisterFromParent sysbus.i2c0` works in
    1.13 (tested), then the repl declares the new `i2c0` at the same address
    and PLIC 58.
  - It was unit-tested with mcs and a C# copy of `mss_i2c_isr` before
    running in Renode.
- **EEPROM:** `renode/models/I2cEeprom.cs`.
- **Firmware:** `examples/freertos-demo/i2c_eeprom.c`, on the vendor
  `MSS_I2C` driver, unchanged and interrupt-driven from S-mode.
  - `PLIC_SetPriority` + `SPLIC_enable(I2C0_MAIN_PLIC)`. The driver's
    `PLIC_External_i2c0_main_IRQHandler` is reached through the S-mode
    dispatch table.
  - Waits poll `MSS_I2C_get_status` with `vTaskDelay(1)`, bounded, because
    `MSS_I2C_wait_complete` spins forever without `MSS_I2C_system_tick`.
- **Where the test runs:** it needs interrupts, so the console task runs it
  when it starts. A separate task would not fit in the heap: about 4.6 KB
  are left of 32 KB.
- **Negative check:** with `eeprom` removed from the repl, the test reports
  "no device at 0x50", the scheduler keeps running, and `make sim` fails.
- **`expected-uart.txt`:** the tick line is now `[S] tick=2`, since the
  EEPROM test shifts the status lines by a tick.
