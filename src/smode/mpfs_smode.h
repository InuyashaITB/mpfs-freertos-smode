/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Public interface of libmpfs_smode_boot.a / libfreertos_smode.a.
 *
 * The libraries bring U54_1 from reset to an S-mode C entry point with the
 * Sv39 MMU on. The application provides s_mode_main() and, optionally, the
 * weak hooks below. The E51 and U54_2..4 are not handled by the libraries:
 * they run the HAL's weak e51() / u54_2() .. u54_4() unless the application
 * defines its own.
 *
 * Sequence on U54_1:
 *
 *   HAL start-up (M-mode) ─► u54_1()                       [libmpfs_smode_boot]
 *     smode_m_prehook()                                    [application, weak]
 *     PLIC S-context: all sources disabled, threshold 0
 *     m_shim_enter_smode() ─► mret
 *   smode_start (S-mode): Sv39 on, jump to the virtual alias
 *     smode_os_init()                                      [libfreertos_smode]
 *       stvec = freertos_risc_v_trap_handler
 *     s_mode_main()                                        [application]
 */

#ifndef MPFS_SMODE_H_
#define MPFS_SMODE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Application entry point, required. Never returns (it should end in
 * vTaskStartScheduler()).
 *
 * Called in S-mode with:
 *   - the Sv39 MMU on, running at the virtual (high) alias of the image;
 *   - stvec = the FreeRTOS trap handler (installed by smode_os_init());
 *   - sstatus.SIE = 0, on a 4 KiB boot stack.
 *
 * Do not set sstatus.SIE before the scheduler starts. Until then a trap would
 * save the boot context into the last created task's TCB, and the FreeRTOS
 * critical sections used by xTaskCreate() & co. leave SIE clear anyway.
 * vTaskStartScheduler() starts every task with interrupts enabled.
 *
 * Interrupt sources: the library leaves every PLIC source disabled for the
 * S context (threshold 0). Enable what you need here with SPLIC_enable() and
 * PLIC_SetPriority() (smode/smode_plic.h), and define the handler under the
 * HAL's name for that source (see mss_irq_handler_stubs.c, e.g.
 * gpio1_bit16_or_gpio2_bit30_plic_30_IRQHandler).
 */
void s_mode_main(void);

/*
 * Optional M-mode bring-up hook (weak, default does nothing).
 *
 * Called on U54_1 in M-mode, after the HAL start-up and before the PLIC
 * S-context set-up and the drop to S-mode, with M-mode interrupts off. Use it
 * for anything that needs machine mode or mhartid: mss_config_clk_rst(),
 * MSS_GPIO_enable_irq(), UART init for smode_diag_puts(), ...
 *
 * It runs from the physical copy of the image (MMU off). Build it with the
 * library's compile options (-mcmodel=medany -fno-jump-tables): that is what
 * makes the virtual-linked code run correctly at its physical address. Do not
 * dereference absolute pointers stored in data (function-pointer tables,
 * string tables) here: they hold virtual addresses.
 */
void smode_m_prehook(void);

/*
 * Diagnostic output (weak, default discards). Used for fatal reports from the
 * M-mode shim, S-mode faults, configASSERT() and the FreeRTOS hooks, and for
 * the legacy SBI console_putchar call.
 *
 * Called from both M-mode (physical addresses) and S-mode (virtual), with
 * interrupts off, possibly from a trap: keep it polled and self-contained,
 * e.g. MSS_UART_polled_tx_string() on a UART set up in smode_m_prehook().
 */
void smode_diag_puts(const char *s);

/* Hex / decimal helpers on top of smode_diag_puts(). */
void smode_diag_puthex(uintptr_t v);
void smode_diag_putdec(unsigned long v);

/*
 * OS hook called by smode_start right before s_mode_main(), in S-mode with
 * the MMU on. Provided by libfreertos_smode (installs the FreeRTOS trap
 * handler); the boot library's weak default only keeps the early trap
 * handler.
 */
void smode_os_init(void);

/* Last S-mode fault, for inspection in the debugger. */
typedef struct
{
    uintptr_t scause;
    uintptr_t sepc;
    uintptr_t stval;
    uintptr_t sstatus;
} smode_fault_t;

extern volatile smode_fault_t g_smode_fault;

/* Report an S-mode fault on smode_diag_puts(), record it in g_smode_fault
 * and park the hart with interrupts off. 'what' starts the message. */
void smode_fatal(const char *what, uintptr_t scause, uintptr_t sepc, uintptr_t stval)
    __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif /* MPFS_SMODE_H_ */
