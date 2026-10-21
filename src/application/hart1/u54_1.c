/*******************************************************************************
 * Copyright 2019-2022 Microchip FPGA Embedded Systems Solutions.
 *
 * SPDX-License-Identifier: MIT
 *
 * U54_1 M-mode entry: platform set-up for the S-mode kernel, then handoff.
 *
 * Sequence:
 *   1. Wait in WFI until the E51 releases this hart (software interrupt).
 *   2. M-mode only work: peripheral clocks/resets, MMUART1 and GPIO2 init,
 *      GPIO2 -> PLIC routing, and PLIC set-up for this hart's *supervisor*
 *      context (the HAL PLIC helpers only know the M-mode context).
 *   3. m_shim_enter_smode(): delegation, PMP, M-mode trap vector, mret to
 *      s_mode_main() (hart1/app_main.c) in S-mode.
 *
 * Interrupt wiring used by the demo (same as the original example and
 * Renode's polarfire-soc-icicle-board.resc):
 *   GPIO2_26 -> GPIO2_30 (PLIC 43), GPIO2_27 -> GPIO2_31 (PLIC 44),
 *   GPIO2_28 -> F2M_0 (PLIC 118). LEDs on GPIO2_16..19.
 */

#include <stdio.h>
#include <string.h>
#include "mpfs_hal/mss_hal.h"
#include "drivers/mss/mss_gpio/mss_gpio.h"
#include "drivers/mss/mss_mmuart/mss_uart.h"
#include "smode/m_shim.h"
#include "smode/smode_plic.h"

/* S-mode kernel entry point, in hart1/app_main.c. */
extern void s_mode_main(void);

/* Stack used by s_mode_main() until the scheduler starts. After that FreeRTOS
 * uses its own ISR stack (configISR_STACK_SIZE_WORDS) and task stacks. */
#define S_BOOT_STACK_SIZE   (4096U)
static uint8_t g_s_boot_stack[S_BOOT_STACK_SIZE] __attribute__((aligned(16)));

static const uint8_t g_m_banner[] =
    "\r\n\r\n**** PolarFire SoC: FreeRTOS in S-mode on U54_1 ****\r\n"
    "[M] u54_1 released, configuring peripherals and PLIC S-context\r\n";

static const uint8_t g_m_handoff[] =
    "[M] delegating SSIP/STIP/SEIP, PMP open, entering S-mode...\r\n";

/* PLIC sources used by the S-mode application. */
static const PLIC_IRQn_Type g_smode_irqs[] =
{
    GPIO1_BIT16_or_GPIO2_BIT30_PLIC_30,
    GPIO1_BIT17_or_GPIO2_BIT31_PLIC_31,
    FABRIC_F2H_0_PLIC,
};

static void m_mode_platform_init(void)
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
     * source in this hart's M-mode PLIC context, which is undone below. */
    MSS_GPIO_enable_irq(GPIO2_LO, MSS_GPIO_30);
    MSS_GPIO_enable_irq(GPIO2_LO, MSS_GPIO_31);
}

static void m_mode_plic_init_for_smode(void)
{
    /* Start from a clean slate for both of this hart's contexts. */
    SPLIC_disable_all();

    for (uint32_t i = 0U; i < (sizeof(g_smode_irqs) / sizeof(g_smode_irqs[0])); i++)
    {
        PLIC_SetPriority(g_smode_irqs[i], 2U);
        SPLIC_disable_m_context(g_smode_irqs[i]);
        SPLIC_enable(g_smode_irqs[i]);
    }

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

/* Set once this hart has cleared stale software interrupts and is about to
 * wait for the E51. The E51 waits for it before raising the release IPI, so
 * the IPI cannot be lost by the clear_soft_interrupt() below. */
volatile uint32_t g_u54_1_waiting_for_release = 0U;

/* Main function for the hart1(U54 processor). Called by the HAL in M-mode. */
void u54_1(void)
{
    /* Clear pending software interrupt in case there was any.
     * Enable only the software interrupt so that the E51 core can bring this
     * core out of WFI by raising a software interrupt.
     */
    clear_soft_interrupt();
    set_csr(mie, MIP_MSIP);

    __asm volatile ("fence rw, rw" ::: "memory");
    g_u54_1_waiting_for_release = 1U;

#if (IMAGE_LOADED_BY_BOOTLOADER == 0)
    /* Put this hart into WFI until the E51 releases it. */
    do
    {
        __asm("wfi");
    } while (0 == (read_csr(mip) & MIP_MSIP));

    /* The hart is out of WFI, clear the SW interrupt. */
    clear_soft_interrupt();
#endif

    write_csr(mie, 0U);

    m_mode_platform_init();
    m_mode_plic_init_for_smode();

    MSS_UART_polled_tx_string(&g_mss_uart1_lo, g_m_handoff);

    m_shim_enter_smode(s_mode_main, (uintptr_t)&g_s_boot_stack[S_BOOT_STACK_SIZE]);
}
