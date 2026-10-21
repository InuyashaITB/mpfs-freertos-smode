# mpfs-freertos-smode
A U54 RISCV FreeRTOS Port to work in S-Mode

FreeRTOS (Kernel V11.3.2) running in **supervisor mode** on one PolarFire SoC
U54 core (U54_1), with the E51 and U54_2..4 parked. Built from the
`mpfs-gpio-interrupt` example (same HAL 2.0.102, drivers, board files and
build configurations) and meant to run in Renode from SoftConsole v2022.2.

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
              mret ─► s_mode_main()  [S-mode, hart1/app_main.c]
                        stvec = freertos_risc_v_trap_handler
                        create tasks, vTaskStartScheduler()
```

After the handoff, M-mode on U54_1 only runs for traps from S-mode:

* **SBI ecall** `TIME.set_timer` (and legacy `set_timer`/`console_putchar`,
  `BASE` probe): write `mtimecmp[1]`, clear `STIP`, enable `MTIE`.
* **Machine timer interrupt**: disable `MTIE`, raise `sip.STIP`. FreeRTOS
  takes it as its tick (scause 5).
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
| `src/application/hart1/u54_1.c` | M-mode set-up + handoff (replaces the bare-metal demo) |
| `src/application/hart1/app_main.c` | S-mode FreeRTOS demo tasks (console, blink, spin, heartbeat) and PLIC handlers |
| `src/application/hart0/e51.c`, `hart2..4/u54_N.c` | Park the other harts |
| `src/application/inc/hart_park.h` | `park_this_hart()` |
| `src/boards/icicle-kit-es/platform_config/mpfs_hal_config/mss_sw_config.h` | `DDR_SUPPORT` commented out (Renode start-up; image runs from L2LIM) |
| `.cproject` | Two include paths added to every configuration: `FreeRTOS-Kernel/include`, `.../RISC-V-SMode` |
| `*.launch` | The two Renode launchers, renamed for this project |

`src/application/inc/common.h` from the example was removed (unused).

## Build and run (SoftConsole + Renode)

1. Import: *File → Import → General → Existing Projects into Workspace*, select
   this folder (or copy it next to the other examples first).
2. Select the **LIM-Debug** build configuration and build.
3. Run the launch group **mpfs-freertos-smode Renode All-harts
   Start-platform-and-debug**. It stops at `e51`; press Resume. UART1 is
   shown by the launcher (`showAnalyzer sysbus.mmuart1`).

Expected on UART1:

```
**** PolarFire SoC: FreeRTOS in S-mode on U54_1 ****
[M] u54_1 released, configuring peripherals and PLIC S-context
[M] delegating SSIP/STIP/SEIP, PMP open, entering S-mode...
[S] s_mode_main: sstatus=0x...6000 (SPP/SIE clear, FS on)
[S] starting scheduler
[S] scheduler running. Press 1, 2 or 3 to raise GPIO2_30, GPIO2_31 or F2M_0 interrupts.
[S] tick=1000 sstatus=0x...6022 spin=... blink=4 mtimer->stip=1000 sbi=1001 ext=0 heap_free=...
...
[S] heartbeat #1: uptime 00:00:30 (tick 30005), heap free ... (min ...), stack headroom ... words
```

The `heartbeat` task prints every 30 s. All task output goes through
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
* `spin` advancing: the `spin` task never blocks and shares priority 0 with
  idle, so it only runs because of preemption and time slicing on the tick.
* `blink` advancing every 250 ms: delays and context switches work.
* `ext` advancing on 1/2/3: PLIC supervisor context → SEIP → S-mode dispatch.
* `sstatus` low bits `0x22`: SIE=1 and SPIE=1 while a task runs in S-mode.

## Debugging notes

* All five harts are visible in the Debug view as before. In S-mode, gdb can
  read `sstatus`, `sepc`, `scause`, `stval`. The machine CSRs are still
  readable from gdb (the debugger has M-mode access).
* Fault records for the debugger: `g_smode_fault` (S-mode exceptions),
  `g_m_shim_fault` (unexpected M-mode traps), `g_hart_parked[]`.
* Breakpoint exceptions are deliberately not delegated to S-mode.
* Renode macros from `macros.resc` work as before, e.g. `runMacro $MCsrU54_1`.

## Limitations / next steps

* **PMP is wide open** (entry 0 = whole address space RWX). Fine for Renode;
  on hardware restrict it to what S-mode needs. The HAL's `init_pmp(1)`
  settings for entry 0..7 are replaced.
* **No MMU** (`satp = 0`). Paging can be added later; the port does not care.
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
