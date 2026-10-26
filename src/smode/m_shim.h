/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Minimal M-mode runtime ("shim") that lets one U54 hart run an S-mode
 * kernel without OpenSBI.
 *
 * What it does:
 *   - m_shim_enter_smode(): configures delegation, PMP, counters and the
 *     M-mode trap vector, then drops the calling hart to S-mode with mret.
 *   - After that, M-mode only runs from traps:
 *       * SBI ecalls from S-mode (TIME.set_timer, IPI.send_ipi to self,
 *         legacy set_timer, legacy console_putchar, BASE probe)
 *       * machine timer interrupt -> forwarded to S-mode as sip.STIP
 *       * anything else is fatal: reported through smode_diag_puts() and the
 *         hart is parked.
 */

#ifndef SMODE_M_SHIM_H_
#define SMODE_M_SHIM_H_

#include <stdint.h>

/* Register frame pushed by m_shim_trap_vector (m_shim_trap.S). */
typedef struct
{
    uintptr_t ra;
    uintptr_t t0, t1, t2;
    uintptr_t a0, a1, a2, a3, a4, a5, a6, a7;
    uintptr_t t3, t4, t5, t6;
    uintptr_t gp;           /* interrupted gp (S-mode may use the virtual alias) */
    uintptr_t pad;
} m_shim_frame_t;

/* Last fatal M-mode trap, for inspection in the debugger. */
typedef struct
{
    uintptr_t mcause;
    uintptr_t mepc;
    uintptr_t mtval;
    uintptr_t mstatus;
    uint32_t  count;
} m_shim_fault_t;

extern volatile m_shim_fault_t g_m_shim_fault;
extern volatile uint64_t g_m_shim_timer_irqs;   /* MTIP -> STIP forwards */
extern volatile uint64_t g_m_shim_sbi_calls;

/* Exceptions delegated to S-mode. Not delegated: breakpoint (3, left to the
 * debugger), ecall from S (9, SBI) and ecall from M (11). */
#define M_SHIM_MEDELEG  ( (1UL << 0)  /* instruction address misaligned */ \
                        | (1UL << 1)  /* instruction access fault */       \
                        | (1UL << 2)  /* illegal instruction */            \
                        | (1UL << 4)  /* load address misaligned */        \
                        | (1UL << 5)  /* load access fault */              \
                        | (1UL << 6)  /* store/AMO address misaligned */   \
                        | (1UL << 7)  /* store/AMO access fault */         \
                        | (1UL << 8)  /* ecall from U-mode */              \
                        | (1UL << 12) /* instruction page fault */         \
                        | (1UL << 13) /* load page fault */                \
                        | (1UL << 15) /* store/AMO page fault */ )

/* Interrupts delegated to S-mode: supervisor software, timer, external. */
#define M_SHIM_MIDELEG  ( (1UL << 1) | (1UL << 5) | (1UL << 9) )

/*
 * Drop the calling hart to S-mode at 'entry' with sp = 's_stack_top'.
 * Must be called in M-mode with interrupts disabled. Never returns.
 *
 * PMP: entry 0 is overwritten with a single NAPOT region covering the whole
 * address space with RWX for S/U-mode. That is fine for bring-up on Renode;
 * on hardware replace it with a configuration that only opens what the
 * S-mode kernel needs.
 */
void m_shim_enter_smode(void (*entry)(void), uintptr_t s_stack_top)
    __attribute__((noreturn));

/* Called from m_shim_trap.S. */
void m_shim_trap_handler(m_shim_frame_t *frame);

#endif /* SMODE_M_SHIM_H_ */
