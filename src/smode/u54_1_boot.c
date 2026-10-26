/*******************************************************************************
 * Copyright 2019-2022 Microchip FPGA Embedded Systems Solutions.
 *
 * SPDX-License-Identifier: MIT
 *
 * U54_1 M-mode entry: the minimum the S-mode kernel needs, then handoff.
 *
 * Sequence:
 *   1. Standalone (IMAGE_LOADED_BY_BOOTLOADER = 0): the HAL's
 *      main_first_hart() (E51) finishes the system start-up and only then
 *      releases harts 1..4 into main_other_hart(), which calls this u54_1().
 *      No further handshake with the E51 is needed, so the library does not
 *      depend on what the E51 runs.
 *      Loaded by a bootloader (= 1, e.g. HSS): the bootloader has done the
 *      system start-up; the HAL's entry code sets up stacks, .data and .bss
 *      and calls u54_1() on hart 1 (MPFS_HAL_FIRST_HART, by default also the
 *      last hart).
 *   2. smode_m_prehook(): the application's M-mode bring-up (clocks, resets,
 *      peripherals, PLIC routing...). Weak, empty by default.
 *   3. PLIC supervisor context of this hart: every source disabled for both
 *      of its contexts, S threshold 0, stale claims drained. The application
 *      enables its sources from S-mode (smode/smode_plic.h).
 *   4. m_shim_enter_smode(): delegation, PMP, M-mode trap vector, mret to
 *      smode_start (smode_start.S) in S-mode, which turns the Sv39 MMU on,
 *      calls smode_os_init() and continues in the application's s_mode_main()
 *      at the virtual (high) alias of the image.
 *
 * Everything here runs in M-mode from the physical copy of L2LIM. Addresses
 * taken in code are therefore physical, which is what mret needs.
 */

#include "mpfs_hal/mss_hal.h"
#include "smode/mpfs_smode.h"
#include "smode/m_shim.h"
#include "smode/smode_plic.h"

/* The HAL only calls u54_1() if hart 1 is one of the harts it starts. */
#if (MPFS_HAL_FIRST_HART > 1) || (MPFS_HAL_LAST_HART < 1)
#error "MPFS_HAL_FIRST_HART..MPFS_HAL_LAST_HART must include hart 1 (U54_1)"
#endif

/* S-mode entry (MMU bring-up), in smode_start.S. */
extern void smode_start(void);

/* LIBRARY-FIXED: stack used by smode_start / s_mode_main() until the
 * scheduler starts.
 * After that FreeRTOS uses its own ISR stack (configISR_STACK_SIZE_WORDS)
 * and task stacks. */
#define S_BOOT_STACK_SIZE   (4096U)
static uint8_t g_s_boot_stack[S_BOOT_STACK_SIZE] __attribute__((aligned(16)));

static void m_mode_plic_init_for_smode(void)
{
    /* Start from a clean slate for both of this hart's contexts. This also
     * drops anything the pre-hook enabled in the M context (for example
     * through MSS_GPIO_enable_irq()). */
    SPLIC_disable_all();

    /* The HAL leaves S-context thresholds at 7 (everything masked). */
    SPLIC_set_threshold(0U);

    /* Drain anything already claimed/pending for the S context. */
    for (uint32_t i = 0U; i < 8U; i++)
    {
        uint32_t id = SPLIC_claim();
        if (0U == id)
        {
            break;
        }
        SPLIC_complete(id);
    }
}

/* Main function for the hart1 (U54 processor). Called by the HAL in M-mode.
 * Overrides the HAL's weak u54_1(). */
void u54_1(void)
{
    /* The release IPI from the E51 may still be pending. */
    clear_soft_interrupt();
    (void)clear_csr(mstatus, MSTATUS_MIE);
    write_csr(mie, 0U);

    smode_m_prehook();

    m_mode_plic_init_for_smode();

    m_shim_enter_smode(smode_start, (uintptr_t)&g_s_boot_stack[S_BOOT_STACK_SIZE]);
}
