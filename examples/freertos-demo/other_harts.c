/*******************************************************************************
 * Copyright 2019-2022 Microchip FPGA Embedded Systems Solutions.
 *
 * SPDX-License-Identifier: MIT
 *
 * The harts the libraries do not use: E51 and U54_2..4.
 *
 * The libraries only define u54_1(). Without this file these harts would run
 * the HAL's weak e51() / u54_2..4(), whose park_hart() jumps to the boot-ROM
 * park loop at 0x20003120 in eNVM. That is fine on a board programmed with
 * HSS, but in Renode (empty eNVM) or with other eNVM contents it is not, so
 * the demo parks them here in WFI instead. The E51 also prints a banner on
 * MMUART0.
 */

#include "mpfs_hal/mss_hal.h"
#include "drivers/mss/mss_mmuart/mss_uart.h"

/* Incremented by each parked hart, indexed by hart ID, so a debugger can see
 * which harts reached the park loop. */
volatile uint32_t g_hart_parked[5] = { 0U };

static const uint8_t g_info_string[] =
        "\r\n\r\n------------------------------------------------------------\r\n"
        " E51: platform up. FreeRTOS runs in S-mode on U54_1 - see UART1.\r\n"
        " E51 and U54_2..4 are parked in WFI.\r\n"
        "------------------------------------------------------------\r\n";

/* Park the calling hart (M-mode): all of its interrupts are disabled and it
 * sleeps in WFI forever. WFI may return spuriously (or when a debugger
 * halts/resumes the hart), so it is called in a loop. */
static void park_this_hart(void) __attribute__((noreturn));
static void park_this_hart(void)
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

/* Main function for the hart0 (E51 processor). */
void e51(void)
{
    (void)mss_config_clk_rst(MSS_PERIPH_MMUART0, (uint8_t) MPFS_HAL_FIRST_HART, PERIPHERAL_ON);

    MSS_UART_init( &g_mss_uart0_lo,
            MSS_UART_115200_BAUD,
            MSS_UART_DATA_8_BITS | MSS_UART_NO_PARITY | MSS_UART_ONE_STOP_BIT);

    MSS_UART_polled_tx_string(&g_mss_uart0_lo, g_info_string);

    park_this_hart();
}

void u54_2(void)
{
    park_this_hart();
}

void u54_3(void)
{
    park_this_hart();
}

void u54_4(void)
{
    park_this_hart();
}

/* hart0 software interrupt handler (unused: the E51 is parked with all
 * interrupts disabled). Kept so the HAL's weak symbol is satisfied the same
 * way as in the original example. */
void Software_h0_IRQHandler(void)
{
}
