/*******************************************************************************
 * Copyright 2019-2022 Microchip FPGA Embedded Systems Solutions.
 *
 * SPDX-License-Identifier: MIT
 *
 * Demo M-mode bring-up, run by the library's u54_1() before the drop to
 * S-mode (smode_m_prehook(), see smode/mpfs_smode.h), plus the diagnostic
 * output used by the library (smode_diag_puts()).
 *
 * Interrupt wiring used by the demo (same as the original example and
 * Renode's polarfire-soc-icicle-board.resc):
 *   GPIO2_26 -> GPIO2_30 (PLIC 43), GPIO2_27 -> GPIO2_31 (PLIC 44),
 *   GPIO2_28 -> F2M_0 (PLIC 118). LEDs on GPIO2_16..19.
 * The PLIC sources are enabled for the S context in s_mode_main().
 *
 * Runs in M-mode from the physical copy of L2LIM (MMU off).
 */

#include "mpfs_hal/mss_hal.h"
#include "drivers/mss/mss_gpio/mss_gpio.h"
#include "drivers/mss/mss_mmuart/mss_uart.h"
#include "smode/mpfs_smode.h"

static const uint8_t g_m_banner[] =
    "\r\n\r\n**** PolarFire SoC: FreeRTOS in S-mode on U54_1 ****\r\n"
    "[M] u54_1 released, configuring peripherals\r\n";

static const uint8_t g_m_handoff[] =
    "[M] pre-hook done, library sets up the PLIC S-context and enters S-mode...\r\n";

/* Library diagnostics (faults, asserts, SBI console_putchar) on MMUART1.
 * Called from M-mode and S-mode; &g_mss_uart1_lo is computed PC-relatively,
 * so it is the right address in both. */
void smode_diag_puts(const char *s)
{
    MSS_UART_polled_tx_string(&g_mss_uart1_lo, (const uint8_t *)s);
}

void smode_m_prehook(void)
{
    /* Reset the peripherals / turn on the clocks used from S-mode. Done in
     * M-mode because mss_config_clk_rst() reads mhartid. */
    (void)mss_config_clk_rst(MSS_PERIPH_MMUART1, (uint8_t) MPFS_HAL_FIRST_HART, PERIPHERAL_ON);
    (void)mss_config_clk_rst(MSS_PERIPH_GPIO2, (uint8_t) MPFS_HAL_FIRST_HART, PERIPHERAL_ON);
    (void)mss_config_clk_rst(MSS_PERIPH_CFM, (uint8_t) MPFS_HAL_FIRST_HART, PERIPHERAL_ON);

    MSS_UART_init( &g_mss_uart1_lo,
            MSS_UART_115200_BAUD,
            MSS_UART_DATA_8_BITS | MSS_UART_NO_PARITY | MSS_UART_ONE_STOP_BIT);

    MSS_UART_polled_tx_string(&g_mss_uart1_lo, g_m_banner);

    /* Route GPIO2 interrupts to the PLIC (instead of GPIO0/GPIO1). See
     * mss_gpio.h for how GPIO interrupts are routed to the PLIC. */
    SYSREG->GPIO_INTERRUPT_FAB_CR = 0xFFFFFFFFUL;

    MSS_GPIO_init(GPIO2_LO);

    for (uint32_t pin = 16U; pin < 20U; pin++)           /* LEDs */
    {
        MSS_GPIO_config(GPIO2_LO, (mss_gpio_id_t)pin, MSS_GPIO_OUTPUT_MODE);
    }

    MSS_GPIO_config(GPIO2_LO, MSS_GPIO_26, MSS_GPIO_OUTPUT_MODE);
    MSS_GPIO_config(GPIO2_LO, MSS_GPIO_27, MSS_GPIO_OUTPUT_MODE);
    MSS_GPIO_config(GPIO2_LO, MSS_GPIO_28, MSS_GPIO_OUTPUT_MODE);

    MSS_GPIO_config(GPIO2_LO, MSS_GPIO_30, MSS_GPIO_INPUT_MODE | MSS_GPIO_IRQ_LEVEL_HIGH);
    MSS_GPIO_config(GPIO2_LO, MSS_GPIO_31, MSS_GPIO_INPUT_MODE | MSS_GPIO_IRQ_LEVEL_HIGH);

    MSS_GPIO_set_outputs(GPIO2_LO, 0U);

    /* Sets the interrupt-enable bit in the GPIO block. It also enables the
     * source in this hart's M-mode PLIC context; the library clears all of
     * this hart's PLIC enables after the pre-hook returns. */
    MSS_GPIO_enable_irq(GPIO2_LO, MSS_GPIO_30);
    MSS_GPIO_enable_irq(GPIO2_LO, MSS_GPIO_31);

    MSS_UART_polled_tx_string(&g_mss_uart1_lo, g_m_handoff);
}
