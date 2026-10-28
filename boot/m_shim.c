/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * M-mode shim on U54_1: hands the hart to S-mode, then serves what S-mode
 * cannot do itself.
 *
 * m_main() (called from entry.S): delegation, PMP entry 0, mtvec and the
 * M-mode trap stack, then mret to s_start in S-mode with the MMU off.
 *
 * m_trap() (from m_trap.S), for traps from S-mode:
 *   SBI TIME.set_timer / legacy set_timer   mtimecmp = value, clear STIP,
 *                                           enable MTIE
 *   SBI IPI.send_ipi (to this hart only)    set mip.SSIP (the FreeRTOS yield)
 *   SBI BASE                                version and probe
 *   machine timer interrupt                 disable MTIE, raise mip.STIP
 *                                           (the FreeRTOS tick)
 *   anything else                           record it in g_m_fault, park
 */

#include <stdint.h>
#include "mpfs.h"

typedef struct
{
    uintptr_t ra, t0, t1, t2, a0, a1, a2, a3, a4, a5, a6, a7, t3, t4, t5, t6;
} m_frame_t;

/* Unexpected trap (count != 0): read with a debugger. */
volatile struct
{
    uintptr_t mcause, mepc, mtval;
    uint32_t count;
} g_m_fault;

extern void m_trap_vector(void);
extern void s_start(void);
extern char __m_stack_top[];

void m_main(void) __attribute__((noreturn));
void m_trap(m_frame_t *f);

static void park(void) __attribute__((noreturn));
static void park(void)
{
    csr_write(mie, 0);
    for (;;)
    {
        __asm volatile ("wfi");
    }
}

static void set_timer(uint64_t value)
{
    *(volatile uint64_t *)(CLINT_MTIMECMP_BASE + 8U * MPFS_SMODE_HART) = value;
    csr_clear(mip, MIP_STIP);
    csr_set(mie, MIP_MTIP);
}

static void sbi(m_frame_t *f)
{
    long error = SBI_SUCCESS;
    long value = 0;

    switch (f->a7)
    {
        case SBI_EXT_TIME:
        case SBI_EXT_LEGACY_SET_TIMER:
            set_timer((uint64_t)f->a0);
            break;

        case SBI_EXT_IPI:
            /* Only this hart runs S-mode: the yield is an IPI to itself. */
            csr_set(mip, MIP_SSIP);
            break;

        case SBI_EXT_BASE:
            if (3U == f->a6)            /* probe_extension */
            {
                value = ((SBI_EXT_TIME == f->a0) || (SBI_EXT_IPI == f->a0)) ? 1 : 0;
            }
            else
            {
                value = (0U == f->a6) ? 0x2 : 0;    /* spec version 0.2 */
            }
            break;

        default:
            error = SBI_ERR_NOT_SUPPORTED;
            break;
    }

    f->a0 = (uintptr_t)error;
    f->a1 = (uintptr_t)value;
}

void m_trap(m_frame_t *f)
{
    uintptr_t mcause = csr_read(mcause);

    if ((MCAUSE_INT | MCAUSE_M_TIMER) == mcause)
    {
        csr_clear(mie, MIP_MTIP);       /* until S-mode sets the next compare */
        csr_set(mip, MIP_STIP);
        return;
    }

    if (MCAUSE_S_ECALL == mcause)
    {
        sbi(f);
        csr_write(mepc, csr_read(mepc) + 4U);
        return;
    }

    g_m_fault.mcause = mcause;
    g_m_fault.mepc = csr_read(mepc);
    g_m_fault.mtval = csr_read(mtval);
    g_m_fault.count++;
    park();
}

void m_main(void)
{
    uintptr_t mstatus;

    csr_write(mie, 0);
    csr_clear(mip, MIP_SSIP | MIP_STIP | MIP_SEIP);

    /* Traps from S-mode: own vector, and the M-mode stack (this function's
     * frame is abandoned at the mret below). */
    csr_write(mtvec, (uintptr_t)&m_trap_vector);
    csr_write(mscratch, (uintptr_t)__m_stack_top);

    csr_write(mideleg, MIDELEG_S);
    csr_write(medeleg, MEDELEG_S);
    csr_write(mcounteren, 0x7);

    /* PMP entry 0: the whole address space for S-mode. */
    csr_write(pmpaddr0, ~0UL);
    csr_write(pmpcfg0, PMPCFG_NAPOT_RWX);

    /* S-mode starts with the MMU off; s_start turns on Sv39. */
    csr_write(satp, 0);
    __asm volatile ("sfence.vma" ::: "memory");
    csr_write(sie, 0);

    /* mret to S-mode with SIE = 0. */
    mstatus = csr_read(mstatus);
    mstatus &= ~(MSTATUS_MPP | MSTATUS_MPIE | MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_SPP |
                 MSTATUS_MPRV | MSTATUS_SUM | MSTATUS_TVM | MSTATUS_TW | MSTATUS_TSR);
    mstatus |= MSTATUS_MPP_S;
    csr_write(mstatus, mstatus);
    csr_write(mepc, (uintptr_t)&s_start);

    __asm volatile ("fence.i\n mret" ::: "memory");
    __builtin_unreachable();
}
