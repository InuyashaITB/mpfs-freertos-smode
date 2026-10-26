/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * PLIC helpers for the supervisor context of the S-mode hart.
 *
 * The HAL's PLIC_* functions pick the context from mhartid, which S-mode
 * cannot read (illegal instruction), and they always use the M-mode context.
 * These helpers address the supervisor context of SMODE_HART directly, so
 * they work from both M-mode (during setup) and S-mode (at run time).
 *
 * MPFS PLIC contexts: 0 = hart0 M, then for hart n (1..4): 2n-1 = M, 2n = S.
 * In Renode's polarfire-soc.repl context 2 is wired to u54_1 IRQ 9 (SEIP).
 */

#ifndef SMODE_PLIC_H_
#define SMODE_PLIC_H_

#include "mpfs_hal/mss_hal.h"

#ifndef SMODE_HART
#define SMODE_HART              1U
#endif

#if (SMODE_HART == 1U)
#define SMODE_PLIC_TARGET       TARGET_OFFSET_HART1_S
#define SMODE_PLIC_S_ENA        (PLIC->HART1_SMODE_ENA)
#define SMODE_PLIC_M_ENA        (PLIC->HART1_MMODE_ENA)
#elif (SMODE_HART == 2U)
#define SMODE_PLIC_TARGET       TARGET_OFFSET_HART2_S
#define SMODE_PLIC_S_ENA        (PLIC->HART2_SMODE_ENA)
#define SMODE_PLIC_M_ENA        (PLIC->HART2_MMODE_ENA)
#elif (SMODE_HART == 3U)
#define SMODE_PLIC_TARGET       TARGET_OFFSET_HART3_S
#define SMODE_PLIC_S_ENA        (PLIC->HART3_SMODE_ENA)
#define SMODE_PLIC_M_ENA        (PLIC->HART3_MMODE_ENA)
#elif (SMODE_HART == 4U)
#define SMODE_PLIC_TARGET       TARGET_OFFSET_HART4_S
#define SMODE_PLIC_S_ENA        (PLIC->HART4_SMODE_ENA)
#define SMODE_PLIC_M_ENA        (PLIC->HART4_MMODE_ENA)
#else
#error "SMODE_HART must be 1..4 (the E51 has no S-mode)"
#endif

/* Disable every source for both contexts of the S-mode hart. */
static inline void SPLIC_disable_all(void)
{
    for (uint32_t i = 0U; i < PLIC_SET_UP_REGISTERS; i++)
    {
        SMODE_PLIC_S_ENA[i] = 0U;
        SMODE_PLIC_M_ENA[i] = 0U;
    }
}

static inline void SPLIC_enable(PLIC_IRQn_Type irq)
{
    SMODE_PLIC_S_ENA[irq / 32U] |= ((uint32_t)1U << (irq % 32U));
}

static inline void SPLIC_disable(PLIC_IRQn_Type irq)
{
    SMODE_PLIC_S_ENA[irq / 32U] &= ~((uint32_t)1U << (irq % 32U));
}

/* Remove a source from the M-mode context (e.g. after an HAL driver call
 * such as MSS_GPIO_enable_irq() enabled it there). */
static inline void SPLIC_disable_m_context(PLIC_IRQn_Type irq)
{
    SMODE_PLIC_M_ENA[irq / 32U] &= ~((uint32_t)1U << (irq % 32U));
}

static inline void SPLIC_set_threshold(uint32_t threshold)
{
    PLIC->TARGET[SMODE_PLIC_TARGET].PRIORITY_THRESHOLD = threshold;
}

static inline uint32_t SPLIC_claim(void)
{
    return PLIC->TARGET[SMODE_PLIC_TARGET].CLAIM_COMPLETE;
}

static inline void SPLIC_complete(uint32_t irq)
{
    PLIC->TARGET[SMODE_PLIC_TARGET].CLAIM_COMPLETE = irq;
}

#endif /* SMODE_PLIC_H_ */
