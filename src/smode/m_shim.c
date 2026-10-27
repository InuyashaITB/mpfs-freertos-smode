/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Minimal M-mode runtime that lets one U54 hart run an S-mode kernel.
 * See m_shim.h for an overview.
 *
 * Everything in this file runs in M-mode. It must not use floating point:
 * the trap vector does not save FP registers, and the S-mode kernel may be
 * using them when it traps.
 */

#include "mpfs_hal/mss_hal.h"
#include "smode/mpfs_smode.h"
#include "smode/m_shim.h"
#include "smode/sbi.h"
#include "smode/sv39.h"

/* LIBRARY-FIXED: M-mode stack for traps from S-mode (SBI, timer). */
#define M_SHIM_STACK_SIZE   (4096U)

/* Private M-mode stack for traps taken after the handoff. */
static uint8_t g_m_shim_stack[M_SHIM_STACK_SIZE] __attribute__((aligned(16)));

volatile m_shim_fault_t g_m_shim_fault;
volatile uint64_t g_m_shim_timer_irqs = 0U;
volatile uint64_t g_m_shim_sbi_calls = 0U;

extern void m_shim_trap_vector(void);

/* PMP configuration byte: R | W | X | A=NAPOT. */
#define M_SHIM_PMP_NAPOT_RWX    (0x1FUL)

static void m_shim_fatal(uintptr_t mcause) __attribute__((noreturn));
static void m_shim_fatal(uintptr_t mcause)
{
    g_m_shim_fault.mcause  = mcause;
    g_m_shim_fault.mepc    = read_csr(mepc);
    g_m_shim_fault.mtval   = read_csr(mtval);
    g_m_shim_fault.mstatus = read_csr(mstatus);
    g_m_shim_fault.count++;

    smode_diag_puts("\r\n*** M-mode shim: unhandled trap mcause=");
    smode_diag_puthex(g_m_shim_fault.mcause);
    smode_diag_puts(" mepc=");
    smode_diag_puthex(g_m_shim_fault.mepc);
    smode_diag_puts(" mtval=");
    smode_diag_puthex(g_m_shim_fault.mtval);
    smode_diag_puts(" mstatus=");
    smode_diag_puthex(g_m_shim_fault.mstatus);
    smode_diag_puts("\r\n");

    /* Park. Keep interrupts off so nothing else runs on this hart. */
    write_csr(mie, 0U);
    for (;;)
    {
        __asm volatile ("wfi");
    }
}

/*------------------------------------------------------------------------------
 * SBI
 */
static void m_shim_set_timer(uint64_t stime_value)
{
    uint64_t hart_id = read_csr(mhartid);

    CLINT->MTIMECMP[hart_id] = stime_value;   /* also clears MTIP */
    (void)clear_csr(mip, MIP_STIP);
    (void)set_csr(mie, MIP_MTIP);
}

/* Only the calling hart runs S-mode, so an IPI may only target itself.
 * hart_mask_base == -1 means "all harts". Setting SSIP from M-mode (a write
 * to mip) also makes Renode re-check interrupts, which an S-mode write to
 * sip does not. */
static long m_shim_send_ipi(uintptr_t hart_mask, uintptr_t hart_mask_base)
{
    uint64_t hart_id = read_csr(mhartid);

    if ((uintptr_t)-1 != hart_mask_base)
    {
        if ((hart_id < hart_mask_base) || ((hart_id - hart_mask_base) >= 64U) ||
            (hart_mask != (1UL << (hart_id - hart_mask_base))))
        {
            return SBI_ERR_INVALID_PARAM;
        }
    }

    (void)set_csr(mip, MIP_SSIP);
    return SBI_SUCCESS;
}

static void m_shim_sbi(m_shim_frame_t *f)
{
    uintptr_t eid = f->a7;
    uintptr_t fid = f->a6;
    long error = SBI_SUCCESS;
    long value = 0;

    g_m_shim_sbi_calls++;

    switch (eid)
    {
        case SBI_EXT_TIME:
            if (SBI_TIME_SET_TIMER == fid)
            {
                m_shim_set_timer((uint64_t)f->a0);
            }
            else
            {
                error = SBI_ERR_NOT_SUPPORTED;
            }
            break;

        case SBI_EXT_IPI:
            if (SBI_IPI_SEND_IPI == fid)
            {
                error = m_shim_send_ipi(f->a0, f->a1);
            }
            else
            {
                error = SBI_ERR_NOT_SUPPORTED;
            }
            break;

        case SBI_EXT_LEGACY_SET_TIMER:
            m_shim_set_timer((uint64_t)f->a0);
            break;

        case SBI_EXT_LEGACY_CONSOLE_PUTCHAR:
        {
            const char c[2] = { (char)f->a0, '\0' };
            smode_diag_puts(c);
            break;
        }

        case SBI_EXT_MPFS_SHIM:
            if (SBI_SHIM_READ_PHYS_U64 == fid)
            {
                /* Debug aid: only the DDR window from the linker script, and
                 * only aligned qwords, so it cannot fault inside M-mode. */
                sv39_layout_t l;
                uint64_t pa = (uint64_t)f->a0;

                sv39_get_layout(&l);
                if ((0U != (pa & 7U)) || (pa < l.ddr_pa) || (pa >= (l.ddr_pa + l.ddr_size)))
                {
                    error = SBI_ERR_INVALID_ADDRESS;
                }
                else
                {
                    value = (long)*(volatile uint64_t *)(uintptr_t)pa;
                }
            }
            else
            {
                error = SBI_ERR_NOT_SUPPORTED;
            }
            break;

        case SBI_EXT_BASE:
            switch (fid)
            {
                case SBI_BASE_GET_SPEC_VERSION:
                    value = 0x2;            /* v0.2 */
                    break;
                case SBI_BASE_GET_IMPL_ID:
                    value = 0x4D50;         /* "MP", not a registered ID */
                    break;
                case SBI_BASE_GET_IMPL_VERSION:
                    value = 1;
                    break;
                case SBI_BASE_PROBE_EXTENSION:
                    value = ((SBI_EXT_TIME == f->a0) || (SBI_EXT_IPI == f->a0) ||
                             (SBI_EXT_BASE == f->a0) ||
                             (SBI_EXT_MPFS_SHIM == f->a0)) ? 1 : 0;
                    break;
                default:
                    error = SBI_ERR_NOT_SUPPORTED;
                    break;
            }
            break;

        default:
            error = SBI_ERR_NOT_SUPPORTED;
            break;
    }

    /* Legacy extensions only return a0; harmless to set a1 as well. */
    f->a0 = (uintptr_t)error;
    f->a1 = (uintptr_t)value;
}

/*------------------------------------------------------------------------------
 * Trap handler, called from m_shim_trap.S on the private M-mode stack.
 */
void m_shim_trap_handler(m_shim_frame_t *frame)
{
    uintptr_t mcause = read_csr(mcause);

    if (0U != (mcause & MCAUSE_INT))
    {
        if (IRQ_M_TIMER == (mcause & MCAUSE_CAUSE))
        {
            /* Forward to S-mode: mask MTIP until S-mode programs the next
             * compare value with set_timer, and raise STIP. */
            (void)clear_csr(mie, MIP_MTIP);
            (void)set_csr(mip, MIP_STIP);
            g_m_shim_timer_irqs++;
            return;
        }

        m_shim_fatal(mcause);
    }

    if (CAUSE_SUPERVISOR_ECALL == mcause)
    {
        m_shim_sbi(frame);
        write_csr(mepc, read_csr(mepc) + 4U);
        return;
    }

    m_shim_fatal(mcause);
}

/*------------------------------------------------------------------------------
 * Handoff
 */
void m_shim_enter_smode(void (*entry)(void), uintptr_t s_stack_top)
{
    uintptr_t mstatus;

    /* Quiesce M-mode interrupts on this hart. In S-mode, M-level interrupts
     * are taken regardless of mstatus.MIE, so mie decides what reaches the
     * shim; only MTIE is ever enabled (by set_timer). */
    (void)clear_csr(mstatus, MSTATUS_MIE);
    write_csr(mie, 0U);
    (void)clear_csr(mip, MIP_SSIP | MIP_STIP | MIP_SEIP);

    /* M-mode trap vector + private stack for traps from S-mode. */
    write_csr(mtvec, (uintptr_t)&m_shim_trap_vector);
    write_csr(mscratch, (uintptr_t)&g_m_shim_stack[M_SHIM_STACK_SIZE]);

    /* Delegation. */
    write_csr(mideleg, M_SHIM_MIDELEG);
    write_csr(medeleg, M_SHIM_MEDELEG);

    /* Let S-mode read cycle/time/instret. (The U54 has no 'time' CSR in
     * hardware; the FreeRTOS port reads mtime from the CLINT instead.) */
    write_csr(mcounteren, 0x7U);

    /* PMP entry 0: whole address space, RWX, for S/U-mode. Without a
     * matching PMP entry every S-mode access faults. */
    write_csr(pmpaddr0, ~0UL);
    write_csr(pmpcfg0, M_SHIM_PMP_NAPOT_RWX);

    /* Bare addressing (no MMU) in S-mode. */
    write_csr(satp, 0U);
    __asm volatile ("sfence.vma" ::: "memory");

    /* Clean S-mode state. stvec is set by the S-mode entry code. */
    write_csr(sie, 0U);
    write_csr(sscratch, 0U);

    /* mret target: S-mode, interrupts off (SIE=0), no MPRV/SUM/TVM/TW/TSR.
     * FS is left as set by the HAL (FPU on). */
    mstatus = read_csr(mstatus);
    mstatus &= ~(MSTATUS_MPP | MSTATUS_MPIE | MSTATUS_SIE | MSTATUS_SPIE |
                 MSTATUS_SPP | MSTATUS_MPRV | MSTATUS_SUM | MSTATUS_TVM |
                 MSTATUS_TW | MSTATUS_TSR);
    mstatus |= ((uintptr_t)PRV_S << 11);     /* MPP = S */
    write_csr(mstatus, mstatus);

    write_csr(mepc, (uintptr_t)entry);

    /* From here on the HAL's PLIC helpers (mss_plic.h) must not read
     * mhartid on this hart: point them at its supervisor context. */
    g_hal_smode_active = 1U;
    __asm volatile ("fence rw, rw" ::: "memory");

    __asm volatile ("mv sp, %0\n"
                    "fence.i\n"
                    "mret\n"
                    :: "r"(s_stack_top) : "memory");

    __builtin_unreachable();
}
