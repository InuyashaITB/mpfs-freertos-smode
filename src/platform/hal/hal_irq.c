/*******************************************************************************
 * Copyright 2019-2020 Microchip FPGA Embedded Systems Solutions.
 *
 * SPDX-License-Identifier: MIT
 *
 * MPFS HAL Embedded Software
 *
 */
/***************************************************************************//**
 * 
 * Legacy interrupt control functions for the Microchip driver library hardware
 * abstraction layer.
 *
 */
#include <stdio.h>
#include "hal/hal.h"
#include "mpfs_hal/common/mss_util.h"

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
 * 
 */
void HAL_enable_interrupts(void) {
    __enable_irq();
}

/*------------------------------------------------------------------------------
 * 
 */
psr_t HAL_disable_interrupts(void) {
    psr_t psr;

    /* mpfs-freertos-smode: in S-mode mask with sstatus.SIE (mss_util.h). */
    if (0U != g_hal_smode_active)
    {
        __asm volatile ("csrrc %0, sstatus, %1" : "=r"(psr) : "r"(SSTATUS_SIE) : "memory");
        return(psr);
    }

    psr = read_csr(mstatus);
    __disable_irq();
    return(psr);
}

/*------------------------------------------------------------------------------
 * 
 */
void HAL_restore_interrupts(psr_t saved_psr) {
    /* mpfs-freertos-smode: in S-mode restore only sstatus.SIE. */
    if (0U != g_hal_smode_active)
    {
        if (0U != (saved_psr & SSTATUS_SIE))
        {
            __asm volatile ("csrs sstatus, %0" :: "r"(SSTATUS_SIE) : "memory");
        }
        return;
    }

    write_csr(mstatus, saved_psr);
}

#ifdef __cplusplus
}
#endif

