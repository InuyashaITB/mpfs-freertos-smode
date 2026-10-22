# mpfs-freertos-smode
A U54 RISCV FreeRTOS Port to work in S-Mode

FreeRTOS (Kernel V11.3.2) running in **supervisor mode** on one PolarFire SoC
U54 core (U54_1), with the E51 and U54_2..4 parked. Built from the
`mpfs-gpio-interrupt` example (same HAL 2.0.102, drivers, board files and
build configurations) and meant to run in Renode from SoftConsole v2022.2.

**Branch `mmu-sv39`:** the S-mode kernel runs with the **Sv39 MMU on**, from
virtual addresses in the upper half of the address space, and owns 3 GiB of
buffers in the 38-bit cached DDR window. See [Sv39 MMU](#sv39-mmu-branch-mmu-sv39).

## Boot flow

Diagrams of the full sequence (reset, MMU/PMP configuration, S-mode handover,
first tick) are in [`docs/boot-to-smode.md`](docs/boot-to-smode.md).

```
reset, all harts ─► mss_entry.S (M-mode, HAL)
  E51  ─► main_first_hart: HW init, wakes harts 1..4 ─► e51():
            prints on UART0, raise_soft_interrupt(1), park (WFI forever)
  U54_2..4 ─► u54_N(): park (WFI forever, interrupts off)
  U54_1 ─► u54_1()  [M-mode, hart1/u54_1.c]
            wait for E51's software interrupt
            clocks/resets, MMUART1 + GPIO2 init, GPIO2 → PLIC routing
            PLIC: sources 43/44/118 → hart1 *S* context, threshold 0
            m_shim_enter_smode()  [smode/m_shim.c]
              mideleg = SSIP|STIP|SEIP, medeleg = faults/illegal/misaligned
              PMP entry 0 = whole space RWX, satp = 0
              mtvec = m_shim_trap_vector (own M stack via mscratch)
              mret ─► smode_start  [S-mode, MMU off, physical PC; smode/smode_start.S]
                        sv39_build(): gigapage root table  [smode/sv39.c]
                        satp = Sv39, jump to the high (virtual) alias, gp/sp too
                        s_mode_main()  [virtual PC; hart1/app_main.c]
                          stvec = freertos_risc_v_trap_handler
                          create tasks, vTaskStartScheduler()
```

After the handoff, M-mode on U54_1 only runs for traps from S-mode:

* **SBI ecall** `TIME.set_timer` (and legacy `set_timer`/`console_putchar`,
  `BASE` probe): write `mtimecmp[1]`, clear `STIP`, enable `MTIE`.
* **SBI ecall** `0x0A000000` / `read_phys_u64` (debug, this project only):
  read a qword at a physical address in the DDR window, used to check the
  page tables from outside the MMU.
* **Machine timer interrupt**: disable `MTIE`, raise `sip.STIP`. FreeRTOS
  takes it as its tick (scause 5).
* **SBI ecall** `IPI.send_ipi` to this hart: set `mip.SSIP`. This is the
  FreeRTOS yield while `configPORT_YIELD_VIA_SBI_IPI = 1`, the default here
  because Renode 1.13 ignores S-mode writes to `sip` (see
  `src/middleware/README.md` section 3.2). Set it to 0 on hardware.
* Anything else: printed on UART1 as `*** M-mode shim: unhandled trap ...`
  and the hart is parked.

## Files added or changed vs. mpfs-gpio-interrupt

| File | What |
|---|---|
| `src/middleware/FreeRTOS-Kernel/` | Kernel V11.3.2 (core files, `include/`, `MemMang/heap_4.c`) |
| `.../portable/GCC/RISC-V-SMode/` | The S-mode port (see its README.md) |
| `src/middleware/README.md` | Deviations from the upstream FreeRTOS RISC-V port, M-mode vs S-mode |
| `src/application/FreeRTOSConfig.h` | 1 kHz tick, mtime 1 MHz @ 0x0200BFF8, FPU context on, 32 KB heap |
| `src/application/smode/m_shim.*`, `m_shim_trap.S` | M-mode shim: handoff, SBI timer, MTIP→STIP |
| `src/application/smode/smode_plic.h` | PLIC helpers for the hart's S context (no `mhartid`) |
| `src/application/smode/smode_irq.*` | S-mode PLIC dispatch, exception/assert/stack-overflow reports |
| `src/application/smode/sbi.h` | SBI IDs + `sbi_call()` for S-mode code |
| `src/application/smode/sv39.*` | *(mmu-sv39)* Gigapage root table from the linker layout |
| `src/application/smode/smode_start.S` | *(mmu-sv39)* S-mode entry: satp on, jump to the virtual alias, early trap report |
| `src/application/ddr/ddr_buffers.*` | *(mmu-sv39)* 3 x 1 GiB `char[]` buffers + `g_ddr_buffer[]` pointer table |
| `src/boards/icicle-kit-es/platform_config/linker/mpfs-lim-sv39.ld` | *(mmu-sv39)* Linker script with physical/virtual regions |
| `renode/mpfs-ddr-38bit-cached-3gib.repl` | *(mmu-sv39)* Adds 2 GiB of DDR to Renode's platform |
| `src/application/hart1/u54_1.c` | M-mode set-up + handoff (replaces the bare-metal demo) |
| `src/application/hart1/app_main.c` | S-mode FreeRTOS demo tasks (console, blink, spin, heartbeat; *mmu-sv39*: ddr, and spin plays "DDR Life" in DDR) and PLIC handlers |
| `src/application/hart0/e51.c`, `hart2..4/u54_N.c` | Park the other harts |
| `src/application/inc/hart_park.h` | `park_this_hart()` |
| `src/boards/icicle-kit-es/platform_config/mpfs_hal_config/mss_sw_config.h` | `DDR_SUPPORT` commented out (Renode start-up; image runs from L2LIM) |
| `.cproject` | Two include paths added to every configuration: `FreeRTOS-Kernel/include`, `.../RISC-V-SMode`. *(mmu-sv39)* `-fno-jump-tables`; LIM configs use `mpfs-lim-sv39.ld` |
| `*.launch` | The two Renode launchers, renamed for this project. *(mmu-sv39)* PC = `0x08000000`, stop at `s_mode_main`, load the DDR repl |

`src/application/inc/common.h` from the example was removed (unused).

## Build and run (SoftConsole + Renode)

1. Import: *File → Import → General → Existing Projects into Workspace*, select
   this folder (or copy it next to the other examples first).
2. Select the **LIM-Debug** build configuration and build.
3. Run the launch group **mpfs-freertos-smode Renode All-harts
   Start-platform-and-debug**. It adds 2 GiB of DDR to the Renode platform
   (`renode/mpfs-ddr-38bit-cached-3gib.repl`), starts all harts at the
   physical entry `0x08000000`, and stops at `s_mode_main` (virtual address,
   MMU already on); press Resume. UART1 is shown by the launcher
   (`showAnalyzer sysbus.mmuart1`).

Expected on UART1:

```
**** PolarFire SoC: FreeRTOS in S-mode on U54_1 ****
[M] u54_1 released, configuring peripherals and PLIC S-context
[M] delegating SSIP/STIP/SEIP, PMP open, entering S-mode...
[S] s_mode_main: sstatus=0x...6000 (SPP/SIE clear, FS on)
[S] Sv39 on: satp=0x80000000000080xx, running at 0xffffffc0080xxxxx (s_mode_main)
[S]   L2LIM VA 0xffffffc008000000 -> PA 0x8000000
[S]   DDR buf0 VA 0xffffffc040000000 -> PA 0x1000000000 (1024 MiB)
[S]   DDR buf1 VA 0xffffffc080000000 -> PA 0x1040000000 (1024 MiB)
[S]   DDR buf2 VA 0xffffffc0c0000000 -> PA 0x1080000000 (1024 MiB)
[S] starting scheduler
[S] scheduler running. Press 1, 2 or 3 to raise GPIO2_30, GPIO2_31 or F2M_0 interrupts.
[S] tick=1000 sstatus=0x...4022 spin=... blink=4 mtimer->stip=1000 sbi=1010 ext=0 ddr_ok=9 ddr_bad=0 heap_free=...
...
[S] heartbeat #1: uptime 00:00:05 (tick 5000), heap free ... (min ...), stack headroom ... words
```

The `heartbeat` task prints every 5 s. All task output goes through
`prvPrintf()` in `app_main.c`, which holds a console mutex, so lines from
different tasks never interleave.

Typing `1`, `2` or `3` in the UART1 analyzer prints
`[S] PLIC S-context interrupt: GPIO2_30 / GPIO2_31 / F2M_0`, and the four LEDs
count in binary. The Renode board script also defines buttons
(`button1` → GPIO2_30, `button2` → GPIO2_31, `button3` → F2M_0); pressing
one from the Renode monitor should give the same messages.

What each counter proves:

* `tick` advancing at 1000/s, and `mtimer->stip` tracking it: the M-mode shim
  forwards the machine timer as STIP and the S-mode tick handler re-arms it
  through SBI.
* `spin` advancing: the `spin` task never sleeps and shares priority 0 with
  idle, so it only runs because of preemption and time slicing on the tick.
  On this branch it counts DDR Life generations (below).
* `blink` advancing every 250 ms: delays and context switches work.
* `ext` advancing on 1/2/3: PLIC supervisor context → SEIP → S-mode dispatch.
* `sstatus` low bits `0x22`: SIE=1 and SPIE=1 while a task runs in S-mode.
* `ddr_ok` +9 per second, `ddr_bad` 0: the `ddr` task wrote the first,
  middle and last qword of each 1 GiB buffer through its virtual address,
  read it back, and M-mode found the same value at the expected physical
  address (0x10_0000_0000 + n GiB + offset).

### DDR Life (the `spin` task)

`spin` plays Conway's Game of Life on a 64 x 32 torus whose state lives
entirely in the 38-bit cached DDR window, reached only through the Sv39
mapping:

| What | Where | PA |
|---|---|---|
| grid A | `buf0` + 256 MiB | `0x10_1000_0000` |
| grid B | `buf1` + 768 MiB | `0x10_7000_0000` |
| history ring (hash + population of the last 64 generations) | last MiB of `buf2` | `0x10_BFF0_0000` |

Each generation reads one grid and writes the other, 1.5 GiB apart. The
history ring detects when the board repeats itself (still life, oscillator up
to period 32, or extinction); the settled board runs for another 500
generations and is then reseeded. The offsets avoid the qwords the `ddr` task
probes.

Every 10 s it prints the board, the population sparkline from the history
ring, the settle state, the VA -> PA of each region, and a cross-check in
which M-mode reads a qword with live cells at its physical address. In Renode
it runs at about 340 generations/s:

```
[S] .- DDR Life - seed #5 - generation 572 --------------------------.
[S] |                                                        ##      |
[S] |                     #                                          |
[S] |                    # #                         #               |
[S] |                     #                         # #              |
...
[S] '----------------------------------------------------------------'
[S]   population 73 (+6 born, -8 died)   337 gens/s, 10130 total
[S]   last 48 gens [#*#%@###%@*#*####*==-==++==-=:=-:------::.......] 73..137 cells
[S]   state      evolving
[S]   previous   seed #4 settled at gen 667, period 2, 44 cells
[S]   grid A *   buf0 VA 0xffffffc050000000 -> PA 0x1010000000
[S]   grid B     buf1 VA 0xffffffc0b0000000 -> PA 0x1070000000
[S]   history    buf2 VA 0xffffffc0fff00000 -> PA 0x10bff00000
[S]   check      row 0 col 16: S-mode VA reads 0x0000000000000001,
[S]              M-mode PA 0x1010000010 reads 0x0000000000000001: match
```

The whole block is printed under the console mutex, so other tasks' lines
never land inside it. That is the only time `spin` can block.

## Debugging notes

* All five harts are visible in the Debug view as before. In S-mode, gdb can
  read `sstatus`, `sepc`, `scause`, `stval`. The machine CSRs are still
  readable from gdb (the debugger has M-mode access).
* Fault records for the debugger: `g_smode_fault` (S-mode exceptions),
  `g_m_shim_fault` (unexpected M-mode traps), `g_hart_parked[]`.
* Breakpoint exceptions are deliberately not delegated to S-mode.
* Symbols are virtual addresses. Breakpoints in S-mode code work normally
  once the MMU is on. M-mode code (HAL start-up, `u54_1()`, the shim) runs
  from the physical copy, so its breakpoints must be physical: load a second
  copy of the symbols shifted down, e.g. in the gdb console
  `add-symbol-file Debug/mpfs-freertos-smode.elf -o -0xffffffc000000000`
  (path as built by your configuration), or use `break *0x0800xxxx`.
* Renode macros from `macros.resc` work as before, e.g. `runMacro $MCsrU54_1`.
* If `vTaskDelay()` returns immediately, or the hart spins in
  `vListInsert()` with `xTickCount` stuck, the yield is not being taken:
  check that `configPORT_YIELD_VIA_SBI_IPI` is 1 when running on Renode.

## Limitations / next steps

* **PMP is wide open** (entry 0 = whole address space RWX). Fine for Renode;
  on hardware restrict it to what S-mode needs. Note that the HAL's
  `init_pmp(1)` has already locked entry 0 (`pmpcfg0 = 0x9F`) when the shim
  runs, so the shim's write to entry 0 has no effect; see
  `docs/boot-to-smode.md` section 5.
* **No OpenSBI.** The shim implements only what FreeRTOS needs. If you later
  boot through HSS + OpenSBI, drop `m_shim.*` and enter `s_mode_main()` from
  OpenSBI's next stage; the port is unchanged (it only uses the standard
  SBI TIME extension).
* Only U54_1 is used. `SMODE_HART` in `smode_plic.h` and the hart-specific
  bits in `u54_1.c` would need changing to move it.
* Code in S-mode must not call HAL functions that read `mhartid` (PLIC
  helpers, `mss_config_clk_rst()`, `MSS_GPIO_enable_irq()`, `SysTick_Config()`
  ...). Do that set-up in `u54_1()` before the handoff, or add S-mode
  variants like `smode_plic.h`.

## Sv39 MMU (branch `mmu-sv39`)

### Address map

| Virtual (S-mode) | Physical | Size | Perms | Purpose |
|---|---|---|---|---|
| `0x00000000_00000000` | `0x00_0000_0000` | 1 GiB | RWX | identity: peripherals (drivers use physical addresses), MMU switch-over |
| `0xFFFFFFC0_00000000` | `0x00_0000_0000` | 1 GiB | RWX | on-chip alias; code/data run at `0xFFFFFFC0_08000000` (L2LIM) |
| `0xFFFFFFC0_40000000` | `0x10_0000_0000` | 3 GiB | RW | 38-bit cached DDR, 3 gigapages |

Five gigapage PTEs in one root table (`g_sv39_root`), all with A/D preset
and G set. `ORIGIN(ddr_cached_38bit_va) - ORIGIN(l2lim_va)` = 896 MiB.

### Linker script

`src/boards/icicle-kit-es/platform_config/linker/mpfs-lim-sv39.ld` (used by
LIM-Debug and LIM-Release) has a physical and a virtual region for L2LIM and
for the DDR window. Every section is `> xxx_va AT> xxx_pa`. The DDR buffers
are their own NOLOAD output section, `.ddr_cached_38bit`. ASSERTs check the
gigapage alignment, the "DDR within 1 GiB of L2LIM" rule, and that every
loaded section has `VMA - LMA == ONCHIP_VA_OFFSET`.

### How one image runs both physical (M-mode) and virtual (S-mode)

M-mode has no address translation, so the HAL start-up and the M-mode shim
run the same code from the physical copy. That works because:

* `-mcmodel=medany` code finds every symbol PC-relatively, and the entire
  on-chip gigapage is aliased with a single offset, so the distances are
  the same in both views.
* **`-fno-jump-tables`** (added to the compiler flags). Otherwise `switch`
  statements compile to tables of absolute (virtual) addresses, which crash
  in M-mode. The HAL's `main_other_hart()` was the first to hit this.
* Symbols the linker defines for copy loops (`__sc_load`, `__data_load`, ...)
  are expressed as virtual addresses.
* The M-mode shim swaps `gp` to its physical value on every trap.

Still true and worth knowing:

* Absolute pointers stored in data (function-pointer tables,
  `const char *table[]`, ...) hold virtual addresses: fine in S-mode, wrong
  if dereferenced by code that runs in M-mode.
* Prebuilt library code (newlib) was not built with `-fno-jump-tables`.
  It is only called from S-mode here, which is fine; don't call it from
  M-mode code.

### DDR buffers

`src/application/ddr/ddr_buffers.c` defines `g_ddr_buffer_0..2`, each
`char[1 GiB]`, in input sections `.bss.ddr_cached_38bit.N`. The `.bss.`
prefix makes compilers emit NOBITS; with any other name GCC writes 1 GiB of
zeros per buffer into the object file.

**Access them through `g_ddr_buffer[n]`, not by name.** With medany, a
symbol named in code must be within ±2 GiB of the instruction. The buffers
start 0.875, 1.875 and 2.875 GiB above the code, so naming
`g_ddr_buffer_2` fails to link (`relocation truncated to fit:
R_RISCV_PCREL_HI20`). The table holds full 64-bit addresses. (The "DDR
within 1 GiB of L2LIM" rule bounds where the region *starts*; with 3
contiguous 1 GiB buffers, the last one always starts beyond 2 GiB.)

The section is not loaded or cleared: contents are undefined after reset.

### Renode

Stock Renode maps only 1 GiB at `0x10_0000_0000`. The debug launcher loads
`renode/mpfs-ddr-38bit-cached-3gib.repl`, which adds `0x10_4000_0000` and
`0x10_8000_0000` (1 GiB each). If the `${workspace_loc:...}` path cannot be
resolved, run in the Renode monitor:
`machine LoadPlatformDescription @<path-to-project>/renode/mpfs-ddr-38bit-cached-3gib.repl`.

On real hardware, 3 GiB needs a board with at least that much DDR (the
Icicle Kit has 2 GiB) and matching DDR segment (SEG) settings.

### Not supported on this branch

The `eNVM-Scratchpad-Release` and `DDR-Release` configurations still use
the HAL's physical linker scripts and do not link (the MMU code needs the
symbols from `mpfs-lim-sv39.ld`).
