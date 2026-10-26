# mpfs-freertos-smode
A U54 RISCV FreeRTOS Port to work in S-Mode

FreeRTOS (Kernel V11.3.2) running in **supervisor mode** on one PolarFire SoC
U54 core (U54_1). Built from the `mpfs-gpio-interrupt` example (same HAL
2.0.102, drivers and board files) and meant to run in Renode from SoftConsole
v2022.2.

Everything up to the application's `s_mode_main()` is packaged as **static
libraries** built with plain GNU make; the demo is a separate application project
that links them. See [Libraries and applications](#libraries-and-applications).

**Branch `mmu-sv39`:** the S-mode kernel runs with the **Sv39 MMU on**, from
virtual addresses in the upper half of the address space, and owns 3 GiB of
buffers in the 38-bit cached DDR window. See [Sv39 MMU](#sv39-mmu-branch-mmu-sv39).

## Boot flow

Diagrams of the full sequence (reset, MMU/PMP configuration, S-mode handover,
first tick) are in [`docs/boot-to-smode.md`](docs/boot-to-smode.md).

```
reset, all harts ─► mss_entry.S (M-mode, HAL)                    [lib boot]
  E51  ─► main_first_hart: HW init, wakes harts 1..4 ─► e51()     [app or HAL weak]
  U54_2..4 ─► u54_N()                                             [app or HAL weak]
  U54_1 ─► u54_1()  [M-mode, src/smode/u54_1_boot.c]              [lib boot]
            smode_m_prehook()                                     [app, weak]
              demo: clocks/resets, MMUART1 + GPIO2 init, GPIO2 → PLIC routing
            PLIC hart1: all sources off (M and S), S threshold 0
            m_shim_enter_smode()  [src/smode/m_shim.c]
              mideleg = SSIP|STIP|SEIP, medeleg = faults/illegal/misaligned
              PMP entry 0 = whole space RWX, satp = 0
              mtvec = m_shim_trap_vector (own M stack via mscratch)
              mret ─► smode_start  [S-mode, MMU off, physical PC; src/smode/smode_start.S]
                        sv39_build(): gigapage root table  [src/smode/sv39.c]
                        satp = Sv39, jump to the high (virtual) alias, gp/sp too
                        smode_os_init()                           [lib freertos]
                          stvec = freertos_risc_v_trap_handler
                        s_mode_main()  [virtual PC, SIE = 0]      [app]
                          demo: PLIC 43/44/118 → S context, create tasks,
                          vTaskStartScheduler()
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
* Anything else: reported through `smode_diag_puts()` (UART1 in the demo) as
  `*** M-mode shim: unhandled trap ...` and the hart is parked.

## Libraries and applications

| Library | Contents |
|---|---|
| `libmpfs_smode_boot.a` | MPFS HAL (start-up, `mss_entry.S`, PLIC/CLINT/PMP/L2, NWC), `u54_1()`, M-mode SBI shim, Sv39 bring-up, `smode_start`, diagnostics (`src/platform/mpfs_hal`, `src/platform/hal`, `src/smode`) |
| `libfreertos_smode.a` | FreeRTOS kernel, `heap_4`, the S-mode port, PLIC dispatch / fault reporting / FreeRTOS hooks, `smode_os_init()` (`src/middleware/FreeRTOS-Kernel`, `src/freertos_glue`). Compiled against the `FreeRTOSConfig.h` in `FREERTOS_CONFIG_DIR` |
| `libmpfs_drivers.a` | MSS drivers named in `MPFS_DRIVERS` (default `mss_mmuart;mss_gpio`), for applications. The two libraries above do not use them |

The libraries only handle **U54_1**. They define `u54_1()` and nothing for
the E51 or U54_2..4: those run the application's `e51()` / `u54_2()` ..
`u54_4()`, or the HAL's weak defaults (`park_hart()`, which jumps to the
boot-ROM park loop at `0x20003120` in eNVM; on Renode, with an empty eNVM,
define your own, as `examples/freertos-demo/other_harts.c` does).

### Application interface (`smode/mpfs_smode.h`)

| Symbol | Provided by | When / how |
|---|---|---|
| `void s_mode_main(void)` | application, **required** | S-mode, Sv39 on, virtual PC, `stvec` = FreeRTOS handler, `sstatus.SIE = 0`, 4 KiB boot stack. Enable PLIC sources (`PLIC_SetPriority()` + `SPLIC_enable()`), create tasks, `vTaskStartScheduler()`. Do not set `SIE` before the scheduler starts |
| `void smode_m_prehook(void)` | application, optional (weak) | M-mode on U54_1, MMU off (physical PC), before the PLIC S-context set-up and the drop to S-mode. Anything needing M-mode or `mhartid`: `mss_config_clk_rst()`, UART init, `MSS_GPIO_enable_irq()`... |
| `void smode_diag_puts(const char *)` | application, optional (weak, default discards) | Fault, assert and shim reports and SBI `console_putchar`. Called from M- and S-mode with interrupts off: keep it polled |
| `vAssertCalled`, `vApplicationMallocFailedHook`, `vApplicationStackOverflowHook` | library (weak) | Report via `smode_diag_puts()` and park; override if needed |
| `vSmodeYieldFromISR()`, `g_smode_ext_irqs` | library (`smode/smode_irq.h`) | from PLIC handlers |
| `SPLIC_*` (`smode/smode_plic.h`), `sbi_*` (`smode/sbi.h`), `sv39_get_layout()` (`smode/sv39.h`), `g_m_shim_*` (`smode/m_shim.h`), `g_smode_fault` | library | helpers and debug records |

PLIC handlers keep the HAL's names (e.g.
`gpio1_bit16_or_gpio2_bit30_plic_30_IRQHandler`) and are dispatched from the
S context by `src/freertos_glue/smode_irq.c`.

Code that runs in M-mode (`smode_m_prehook()`, anything it calls) must be
built with the library's compile options; `MPFS_SMODE_CFLAGS` from
`mpfs_smode.mk` has them (see [How one image runs both physical and
virtual](#how-one-image-runs-both-physical-m-mode-and-virtual-s-mode)).

### Building

Plain GNU make. Requirements: what SoftConsole ships (`make`, and `sh`,
`mkdir`, `cp`, `rm` from `build_tools`) and a RISC-V bare-metal GCC with
newlib-nano (`riscv64-unknown-elf-gcc` on `PATH`). Nothing else to install;
the same Makefiles work on Linux.

```sh
make                        # libraries, install tree build/dist, demo
make libs                   # libraries + install tree only
make install PREFIX=dist    # copy the install tree to dist/ (any path, e.g. C:/mpfs-smode)
make clean                  # remove build/
```

Outputs:

```
build/dist/                          the install tree ('make install' copies it)
  lib/libmpfs_smode_boot.a
  lib/libfreertos_smode.a
  lib/libmpfs_drivers.a
  lib/ldscripts/mpfs-lim-sv39.ld
  include/                           mpfs_hal/, hal/, drivers/, fpga_design_config/,
                                     mpfs_hal_config/, smode/ (incl. mpfs_smode.h)
  include/freertos/                  kernel + port headers, the FreeRTOSConfig.h used
  mpfs_smode.mk                      for application Makefiles
  mk/toolchain.mk, mk/build_config.mk
build/examples/freertos-demo/freertos-demo.elf (+ .hex, .map)
```

The demo is built only from `build/dist`, exactly like an external
application.

| make variable | Default | |
|---|---|---|
| `BUILD_TYPE` | `Debug` (`-O0 -g3`) | or `Release` (`-Os -g`) |
| `CROSS_COMPILE` | `riscv64-unknown-elf-` | may include a directory |
| `LIBC_SPECS` / `LIBC_LINK_SPECS` | `--specs=nano.specs` / `--specs=nosys.specs` | for a picolibc toolchain: `--specs=picolibc.specs` / empty |
| `MARCH` | `rv64gc`, `rv64gc_zicsr_zifencei` on GCC ≥ 12 | |
| `FREERTOS_CONFIG_DIR` | `examples/freertos-demo` | `FreeRTOSConfig.h` that `libfreertos_smode` is built against (and installs) |
| `MPFS_BOARD_DIR` | `src/boards/icicle-kit-es` | board configuration headers |
| `MPFS_LINKER_SCRIPT` | `<board>/platform_config/linker/mpfs-lim-sv39.ld` | installed with the libraries |
| `MPFS_DRIVERS` | `mss_mmuart mss_gpio` | drivers in `libmpfs_drivers` |
| `V` | | `V=1` prints full command lines |

After changing `CROSS_COMPILE`, `LIBC_SPECS` or `MARCH`, run `make clean`.
Objects are kept per `BUILD_TYPE`, so switching that needs no clean.

Compile options applied to the libraries and to every application:
`-march=rv64gc -mabi=lp64d -mcmodel=medany -mstrict-align -fno-jump-tables
-ffunction-sections -fdata-sections`; link: `-nostartfiles -Wl,--gc-sections
-T mpfs-lim-sv39.ld`.

### Writing an application

Start from `examples/freertos-demo/Makefile`, or include `mpfs_smode.mk` from
the install tree in your own Makefile:

```make
MPFS_SMODE_DIR := C:/mpfs-smode            # the 'make install' PREFIX
include $(MPFS_SMODE_DIR)/mpfs_smode.mk

OBJS := build/main.o

build/my_app.elf: $(OBJS) $(MPFS_SMODE_DEPS)
	$(CC) $(MPFS_SMODE_LDFLAGS) $(OBJS) $(MPFS_SMODE_LIBS) -o $@

build/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(MPFS_SMODE_CFLAGS) -c $< -o $@
```

`mpfs_smode.mk` provides `MPFS_SMODE_CFLAGS` (compile options + include
paths), `MPFS_SMODE_LDFLAGS` (link options + linker script),
`MPFS_SMODE_LIBS`, `MPFS_SMODE_DEPS` and `CC`/`OBJCOPY`/`SIZE`. Its toolchain
defaults are the ones the libraries were built with (`mk/build_config.mk`).

```c
#include "FreeRTOS.h"
#include "task.h"
#include "smode/mpfs_smode.h"

void s_mode_main(void)          /* S-mode, MMU on, FreeRTOS trap handler installed */
{
    xTaskCreate(vMyTask, "my", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    vTaskStartScheduler();
    for (;;) { }
}
```

`MPFS_SMODE_LIBS` links both libraries with `--whole-archive`: weak defaults
(the HAL's `u54_1()`, the boot library's `smode_os_init()`) live in archive
members that are always loaded, so without it the strong definitions would
never be pulled in. `--gc-sections` removes what is unused. To use another
linker script, set `MPFS_SMODE_LDSCRIPT` before including `mpfs_smode.mk`.

`FreeRTOSConfig.h` is baked into `libfreertos_smode.a`. The installed copy
(`include/freertos/FreeRTOSConfig.h`, next to `FreeRTOS.h`) is the one
applications compile against. To change it, rebuild the library with
`make FREERTOS_CONFIG_DIR=<dir with your FreeRTOSConfig.h>`.
`configPORT_YIELD_VIA_SBI_IPI` / `configSBI_HART_ID` must stay consistent
with `SMODE_HART` (checked at build time).

## Files added or changed vs. mpfs-gpio-interrupt

| File | What |
|---|---|
| `Makefile` | Library build, install tree, demo (plain GNU make; command line and SoftConsole) |
| `mk/toolchain.mk`, `mk/mpfs_smode.mk` | Shared tools/flags; what an application Makefile includes (installed) |
| `src/smode/mpfs_smode.h` | Public application interface of the libraries |
| `src/smode/u54_1_boot.c` | `u54_1()`: pre-hook, PLIC S-context clean slate, handoff |
| `src/smode/m_shim.*`, `m_shim_trap.S` | M-mode shim: handoff, SBI timer/IPI, MTIP→STIP |
| `src/smode/smode_diag.c` | Weak `smode_diag_puts()` / `smode_m_prehook()` / `smode_os_init()`, fault reports |
| `src/smode/smode_plic.h` | PLIC helpers for the hart's S context (no `mhartid`) |
| `src/smode/sbi.h` | SBI IDs + `sbi_call()` for S-mode code |
| `src/smode/sv39.*` | *(mmu-sv39)* Gigapage root table from the linker layout |
| `src/smode/smode_start.S` | *(mmu-sv39)* S-mode entry: satp on, jump to the virtual alias, `smode_os_init()`, `s_mode_main()` |
| `src/smode/smode_irq.h`, `src/freertos_glue/smode_irq.c` | S-mode PLIC dispatch, exception/assert/stack-overflow reports, `smode_os_init()` |
| `src/middleware/FreeRTOS-Kernel/` | Kernel V11.3.2 (core files, `include/`, `MemMang/heap_4.c`) |
| `.../portable/GCC/RISC-V-SMode/` | The S-mode port (see its README.md) |
| `src/middleware/README.md` | Deviations from the upstream FreeRTOS RISC-V port, M-mode vs S-mode |
| `src/boards/icicle-kit-es/platform_config/linker/mpfs-lim-sv39.ld` | *(mmu-sv39)* Linker script with physical/virtual regions |
| `src/boards/icicle-kit-es/platform_config/mpfs_hal_config/mss_sw_config.h` | `DDR_SUPPORT` commented out (Renode start-up; image runs from L2LIM) |
| `examples/freertos-demo/` | The demo application: `Makefile` (template for new apps), `FreeRTOSConfig.h` (1 kHz tick, mtime 1 MHz @ 0x0200BFF8, FPU context on, 32 KB heap) |
| `examples/freertos-demo/app_main.c` | `s_mode_main()`: PLIC sources, demo tasks (console, blink, spin "DDR Life", ddr, heartbeat) and PLIC handlers |
| `examples/freertos-demo/m_prehook.c` | `smode_m_prehook()`: MMUART1, GPIO2, GPIO→PLIC routing; `smode_diag_puts()` on MMUART1 |
| `examples/freertos-demo/other_harts.c` | `e51()` (UART0 banner) and `u54_2..4()`: park in WFI |
| `examples/freertos-demo/ddr_buffers.*` | *(mmu-sv39)* 3 x 1 GiB `char[]` buffers + `g_ddr_buffer[]` pointer table |
| `renode/mpfs-ddr-38bit-cached-3gib.repl` | *(mmu-sv39)* Adds 2 GiB of DDR to Renode's platform |
| `.cproject` | One configuration (LIM-Debug), Makefile project calling the top-level `Makefile`; include paths kept for the indexer |
| `*.launch` | The two Renode launchers, renamed for this project. *(mmu-sv39)* PC = `0x08000000`, stop at `s_mode_main`, load the DDR repl; ELF = `build/examples/freertos-demo/freertos-demo.elf` |

`src/application/inc/common.h` from the example was removed (unused).

## Build and run (SoftConsole + Renode)

1. Import: *File → Import → General → Existing Projects into Workspace*, select
   this folder (or copy it next to the other examples first).
2. Build the project (*Project → Build*). It runs `make` in the project
   folder with SoftConsole's own `make` and toolchain, into `build/`. (Or
   run `make` in a shell.)
3. Run the launch group **mpfs-freertos-smode Renode All-harts
   Start-platform-and-debug**. It adds 2 GiB of DDR to the Renode platform
   (`renode/mpfs-ddr-38bit-cached-3gib.repl`), starts all harts at the
   physical entry `0x08000000`, and stops at `s_mode_main` (virtual address,
   MMU already on); press Resume. UART1 is shown by the launcher
   (`showAnalyzer sysbus.mmuart1`).

Expected on UART1:

```
**** PolarFire SoC: FreeRTOS in S-mode on U54_1 ****
[M] u54_1 released, configuring peripherals
[M] pre-hook done, library sets up the PLIC S-context and enters S-mode...
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
  `g_m_shim_fault` (unexpected M-mode traps), `g_hart_parked[]` (demo).
* The libraries are built with full debug info (`Debug`), so their sources
  are visible in the debugger as long as the paths in the `.a` resolve.
* Breakpoint exceptions are deliberately not delegated to S-mode.
* Symbols are virtual addresses. Breakpoints in S-mode code work normally
  once the MMU is on. M-mode code (HAL start-up, `u54_1()`, the shim) runs
  from the physical copy, so its breakpoints must be physical: load a second
  copy of the symbols shifted down, e.g. in the gdb console
  `add-symbol-file build/examples/freertos-demo/freertos-demo.elf -o -0xffffffc000000000`, or use `break *0x0800xxxx`.
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
  bits in `u54_1_boot.c` would need changing to move it.
* Code in S-mode must not call HAL functions that read `mhartid` (PLIC
  helpers, `mss_config_clk_rst()`, `MSS_GPIO_enable_irq()`, `SysTick_Config()`
  ...). Do that set-up in `smode_m_prehook()` before the handoff, or add
  S-mode variants like `smode_plic.h`.

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

`src/boards/icicle-kit-es/platform_config/linker/mpfs-lim-sv39.ld` (installed
with the libraries and used for every application) has a physical and a virtual region for L2LIM and
for the DDR window. Every section is `> xxx_va AT> xxx_pa`. The DDR buffers
are not a section at all: `g_ddr_buffer_0..2` are absolute symbols at
`ORIGIN(ddr_cached_38bit_va)` + 0, 1 and 2 GiB (see below); they cost nothing
in applications that do not use them. ASSERTs check the
gigapage alignment, the "DDR within 1 GiB of L2LIM" rule, and that every
loaded section has `VMA - LMA == ONCHIP_VA_OFFSET`.

### How one image runs both physical (M-mode) and virtual (S-mode)

M-mode has no address translation, so the HAL start-up and the M-mode shim
run the same code from the physical copy. That works because:

* `-mcmodel=medany` code finds every symbol PC-relatively, and the entire
  on-chip gigapage is aliased with a single offset, so the distances are
  the same in both views.
* **`-fno-jump-tables`** (in the compile options of the libraries and of
  every application using `MPFS_SMODE_CFLAGS`). Otherwise `switch`
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

The linker script defines `g_ddr_buffer_0..2` as absolute symbols, 1 GiB
apart from `ORIGIN(ddr_cached_38bit_va)`, and
`examples/freertos-demo/ddr_buffers.c` declares them `extern char[]`. They are deliberately **not** an output
section: even a `NOLOAD` section is still *allocated*, so the linker gives it
a `PT_LOAD` program header with `MemSiz` = 3 GiB. `size` then reports 3 GiB of
bss, loaders that clear BSS segments would try to zero it, and Renode 1.13's
`sysbus LoadELF` crashes on it. (Marking the section `(COPY)` instead makes
ld 2.32 write 3 GiB of zeros into the ELF.) As symbols they cost nothing:
the ELF has only the on-chip segments and `size` shows the real footprint.

**Access them through `g_ddr_buffer[n]`, not by name.** With medany, a
symbol named in code must be within ±2 GiB of the instruction. The buffers
start 0.875, 1.875 and 2.875 GiB above the code, so naming
`g_ddr_buffer_2` fails to link (`relocation truncated to fit:
R_RISCV_PCREL_HI20`). The table holds full 64-bit addresses. (The "DDR
within 1 GiB of L2LIM" rule bounds where the region *starts*; with 3
contiguous 1 GiB buffers, the last one always starts beyond 2 GiB.)

Nothing loads or clears the buffers: contents are undefined after reset.

### Renode

Stock Renode maps only 1 GiB at `0x10_0000_0000`. The debug launcher loads
`renode/mpfs-ddr-38bit-cached-3gib.repl`, which adds `0x10_4000_0000` and
`0x10_8000_0000` (1 GiB each). If the `${workspace_loc:...}` path cannot be
resolved, run in the Renode monitor:
`machine LoadPlatformDescription @<path-to-project>/renode/mpfs-ddr-38bit-cached-3gib.repl`.

On real hardware, 3 GiB needs a board with at least that much DDR (the
Icicle Kit has 2 GiB) and matching DDR segment (SEG) settings.

### Not supported on this branch

Only the LIM (L2LIM, Sv39) layout is built. The HAL's eNVM-scratchpad and DDR
linker scripts are physical-only and lack the symbols the MMU code needs, so
the former `eNVM-Scratchpad-Release` / `DDR-Release` configurations were
dropped with the SoftConsole managed build.
