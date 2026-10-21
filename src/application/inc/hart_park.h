/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Park the calling hart (M-mode only): all of its interrupts are disabled and
 * it sleeps in WFI forever. WFI may return spuriously (or when a debugger
 * halts/resumes the hart), so it is called in a loop.
 */

#ifndef HART_PARK_H_
#define HART_PARK_H_

#include "mpfs_hal/mss_hal.h"

/* Incremented by each parked hart, indexed by hart ID, so a debugger can see
 * which harts reached the park loop. */
extern volatile uint32_t g_hart_parked[5];

static inline void park_this_hart(void) __attribute__((noreturn));
static inline void park_this_hart(void)
{
    uint64_t hart_id = read_csr(mhartid);

    (void)clear_csr(mstatus, MSTATUS_MIE);
    write_csr(mie, 0U);
    clear_soft_interrupt();

    if (hart_id < 5U)
    {
        g_hart_parked[hart_id]++;
    }

    for (;;)
    {
        __asm volatile ("wfi");
    }
}

#endif /* HART_PARK_H_ */
