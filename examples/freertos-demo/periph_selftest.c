/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Self-tests for the simulated peripherals. See periph_selftest.h.
 */

#include "mpfs_hal/mss_hal.h"
#include "drivers/mss/mss_spi/mss_spi.h"
#include "periph_selftest.h"

#define SPI_LOOPBACK_FRAMES     (16U)

void spi_loopback_selftest(spi_loopback_result_t *result)
{
    result->frames = 0U;
    result->errors = 0U;
    result->first_bad_tx = 0U;
    result->first_bad_rx = 0U;

    /* In S-mode: MSS_SPI_init() calls PLIC_DisableIRQ(), which must use the
     * S context instead of reading mhartid (mss_plic.h). */
    MSS_SPI_init(&g_mss_spi1_lo);
    MSS_SPI_configure_master_mode(&g_mss_spi1_lo, MSS_SPI_SLAVE_0, MSS_SPI_MODE0,
                                  256U, 8U, NULL);

    MSS_SPI_set_slave_select(&g_mss_spi1_lo, MSS_SPI_SLAVE_0);
    for (uint32_t i = 0U; i < SPI_LOOPBACK_FRAMES; i++)
    {
        uint32_t tx = (i * 0x11U) & 0xFFU;
        uint32_t rx = MSS_SPI_transfer_frame(&g_mss_spi1_lo, tx) & 0xFFU;

        result->frames++;
        if (rx != ((~tx) & 0xFFU))
        {
            if (0U == result->errors)
            {
                result->first_bad_tx = tx;
                result->first_bad_rx = rx;
            }
            result->errors++;
        }
    }
    MSS_SPI_clear_slave_select(&g_mss_spi1_lo, MSS_SPI_SLAVE_0);
}
