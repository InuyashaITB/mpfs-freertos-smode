/*******************************************************************************
 * Copyright 2019-2022 Microchip FPGA Embedded Systems Solutions.
 *
 * SPDX-License-Identifier: MIT
 *
 * Application code running on e51 (hart 0, M-mode only).
 *
 * In this project the E51 only finishes platform start-up (done by the HAL
 * before e51() is called), prints a pointer to UART1, releases U54_1 and then
 * parks itself in WFI. U54_1 runs FreeRTOS in S-mode.
 */

#include <stdio.h>
#include <string.h>
#include "mpfs_hal/mss_hal.h"
#include "drivers/mss/mss_mmuart/mss_uart.h"
#include "inc/hart_park.h"

volatile uint32_t g_hart_parked[5] = { 0U };

/* Defined in hart1/u54_1.c. */
extern volatile uint32_t g_u54_1_waiting_for_release;

const uint8_t g_info_string[] =
        "\r\n\r\n------------------------------------------------------------\r\n"
        " E51: platform up. FreeRTOS runs in S-mode on U54_1 - see UART1.\r\n"
        " E51 and U54_2..4 are parked in WFI.\r\n"
        "------------------------------------------------------------\r\n";

/* Main function for the hart0(e51 processor).
 * Application code running on hart0 is placed here
 */
void e51(void)
{
    (void)mss_config_clk_rst(MSS_PERIPH_MMUART0, (uint8_t) MPFS_HAL_FIRST_HART, PERIPHERAL_ON);

    MSS_UART_init( &g_mss_uart0_lo,
            MSS_UART_115200_BAUD,
            MSS_UART_DATA_8_BITS | MSS_UART_NO_PARITY | MSS_UART_ONE_STOP_BIT);

    MSS_UART_polled_tx_string(&g_mss_uart0_lo, g_info_string);

#if (IMAGE_LOADED_BY_BOOTLOADER == 0)
    /* Wait until u54_1() has cleared stale software interrupts and is
     * waiting, then raise the software interrupt that releases it from its
     * WFI loop. Harts 2..4 are never released and stay parked. */
    while (0U == g_u54_1_waiting_for_release)
    {
        /* spin */
    }
    __asm volatile ("fence rw, rw" ::: "memory");
    raise_soft_interrupt(1U);
#endif

    park_this_hart();
}

/* hart0 software interrupt handler (unused: the E51 is parked with all
 * interrupts disabled). Kept so the HAL's weak symbol is satisfied the same
 * way as in the original example. */
void Software_h0_IRQHandler(void)
{
}
