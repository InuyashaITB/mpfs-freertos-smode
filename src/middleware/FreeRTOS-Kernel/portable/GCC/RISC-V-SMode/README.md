# FreeRTOS RISC-V port, supervisor mode (S-mode)

Derived from `portable/GCC/RISC-V` of FreeRTOS-Kernel **V11.3.2**
(commit 640d0c43705e64cd53f46b31d882de6cd8433933). The task stack frame,
critical nesting, FPU/VPU handling and ISR-stack switch are unchanged; only the
privilege-specific parts differ.

The full list of deviations from upstream, and a section-by-section
comparison of M-mode and S-mode handling, is in
[`src/middleware/README.md`](../../../../README.md).

## What changed vs. the M-mode port

| Area | M-mode port | This port |
|---|---|---|
| Status / return | `mstatus`, `mepc`, `mcause`, `mret` | `sstatus`, `sepc`, `scause`, `sret` |
| Global interrupt enable | `mstatus.MIE` (bit 3) | `sstatus.SIE` (bit 1) |
| Initial task status | `MPIE=1, MPP=M` (0x1880) | `SPIE=1, SPP=S` (0x120) |
| Trap vector | `mtvec` | `stvec` (direct mode), set by the application |
| Yield | `ecall` (mcause 11) | set `sip.SSIP` (scause 1); handler clears it |
| Tick | machine timer (mcause 7), port writes `mtimecmp` | supervisor timer (scause 5), port calls SBI `TIME.set_timer` |
| Interrupt enables at start | `mie`: MTIE + MEIE | `sie`: SSIE + STIE + SEIE |
| Other interrupts / exceptions | weak `freertos_risc_v_application_*_handler` | same, with `a0 = scause`, `a1 = sepc` (+4 for exceptions) |

Why the yield is not an `ecall`: an `ecall` from S-mode traps to M-mode,
where it is used for SBI. A pended supervisor software interrupt gives the same
behaviour as PendSV on Cortex-M: immediate when interrupts are enabled,
deferred until the critical section ends otherwise.

## Requirements on M-mode firmware

Whatever runs in M-mode (OpenSBI, or the small shim in
`src/application/smode/m_shim.c` of this project) must:

1. Delegate interrupts in `mideleg`: SSIP (bit 1), STIP (bit 5) and, if PLIC
   interrupts are used, SEIP (bit 9).
2. Implement SBI `TIME.set_timer` (EID `0x54494D45`, FID 0): write
   `mtimecmp[hart]`, clear `mip.STIP`, set `mie.MTIE`. On the machine timer
   interrupt: clear `mie.MTIE`, set `mip.STIP`. (OpenSBI does exactly this.)
3. Configure PMP so S-mode can access its code/data, the peripherals it uses,
   and `mtime` (`configMTIME_BASE_ADDRESS`).
4. Enter S-mode with `sstatus.SIE = 0` and `sstatus.FS != 0` when
   `configENABLE_FPU = 1`.

## Application responsibilities

* Write `stvec = freertos_risc_v_trap_handler` before `vTaskStartScheduler()`.
* Provide `freertos_risc_v_application_interrupt_handler(scause, sepc)` for
  supervisor external interrupts (claim/complete on the PLIC **S** context)
  and `freertos_risc_v_application_exception_handler(scause, sepc)`.
* Do not use instructions that need M-mode (`csrr mhartid`, `mstatus`, ...)
  or HAL functions that do. On PolarFire SoC that includes the HAL `PLIC_*`
  helpers, `mss_config_clk_rst()` and `MSS_GPIO_enable_irq()`.
* `FreeRTOSConfig.h` is included by `portASM.S`; keep C-only content inside
  `#ifndef __ASSEMBLER__`.

## Config

* `configCPU_CLOCK_HZ` = `mtime` frequency (1 MHz on PolarFire SoC).
* `configMTIME_BASE_ADDRESS` = `mtime` address (`0x0200BFF8` on PolarFire SoC).
  `configMTIMECMP_BASE_ADDRESS` is not used by this port.
* `configISR_STACK_SIZE_WORDS` should be defined (static ISR stack).
