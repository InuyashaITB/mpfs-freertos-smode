/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Self-tests for the simulated peripherals. See periph_selftest.h.
 */

#include "mpfs_hal/mss_hal.h"
#include "drivers/mss/mss_spi/mss_spi.h"
#include "periph_selftest.h"

#define SPI_LOOPBACK_FRAMES     (16U)

/* Polls before a wait is given up. A frame at the configured clock divider
 * takes a few microseconds on hardware; in Renode the status is immediate. */
#define SPI_POLL_LIMIT          (100000U)

/* MSS SPI register bits (the same values mss_spi.c uses privately). */
#define SPI_STATUS_TX_DONE          (0x00000001U)
#define SPI_STATUS_RX_DATA_READY    (0x00000002U)
#define SPI_STATUS_RX_FIFO_EMPTY    (0x00000040U)
#define SPI_CMD_RX_FIFO_RESET       (0x00000004U)
#define SPI_CMD_TX_FIFO_RESET       (0x00000008U)
#define SPI_CTRL_TXRXDFCOUNT_MASK   (0x00FFFF00U)
#define SPI_CTRL_TXRXDFCOUNT_SHIFT  (8U)

typedef enum
{
    SPI_XFER_OK,
    SPI_XFER_TX_TIMEOUT,
    SPI_XFER_RX_TIMEOUT,
} spi_xfer_status_t;

/* One full-duplex frame, as MSS_SPI_transfer_frame() does it, but with both
 * waits bounded, and waiting for "RX FIFO not empty" rather than
 * STATUS.RX_DATA_READY. Renode's MPFS_SPI sets RX_DATA_READY (its RXDATRCVD)
 * only when the received byte count is a multiple of the frame size in bits,
 * i.e. every 8 bytes for 8-bit frames, and each frame here starts with a FIFO
 * flush, so it never comes. RX_FIFO_EMPTY follows the FIFO directly, and
 * waiting for it to clear is as valid on hardware. *rx_ready_seen records
 * whether RX_DATA_READY was set as well. */
static spi_xfer_status_t spi_transfer_frame_bounded(SPI_TypeDef *spi, uint32_t tx, uint32_t *rx,
                                                    uint32_t *rx_ready_seen)
{
    uint32_t n;

    spi->FRAMESUP = 0U;
    spi->CONTROL = (spi->CONTROL & ~SPI_CTRL_TXRXDFCOUNT_MASK)
                   | (1U << SPI_CTRL_TXRXDFCOUNT_SHIFT);
    spi->COMMAND |= (SPI_CMD_TX_FIFO_RESET | SPI_CMD_RX_FIFO_RESET);

    spi->TX_DATA = tx;

    for (n = 0U; (n < SPI_POLL_LIMIT) && (0U == (spi->STATUS & SPI_STATUS_TX_DONE)); n++)
    {
    }
    if (n == SPI_POLL_LIMIT)
    {
        return SPI_XFER_TX_TIMEOUT;
    }

    for (n = 0U; (n < SPI_POLL_LIMIT) && (0U != (spi->STATUS & SPI_STATUS_RX_FIFO_EMPTY)); n++)
    {
    }
    if (n == SPI_POLL_LIMIT)
    {
        return SPI_XFER_RX_TIMEOUT;
    }

    *rx_ready_seen = (0U != (spi->STATUS & SPI_STATUS_RX_DATA_READY)) ? 1U : 0U;
    *rx = spi->RX_DATA;
    return SPI_XFER_OK;
}

void spi_loopback_selftest(spi_loopback_result_t *result)
{
    *result = (spi_loopback_result_t){ 0 };

    /* In S-mode: MSS_SPI_init() calls PLIC_DisableIRQ(), which must use the
     * S context instead of reading mhartid (mss_plic.h). */
    MSS_SPI_init(&g_mss_spi1_lo);
    MSS_SPI_configure_master_mode(&g_mss_spi1_lo, MSS_SPI_SLAVE_0, MSS_SPI_MODE0,
                                  256U, 8U, NULL);

    MSS_SPI_set_slave_select(&g_mss_spi1_lo, MSS_SPI_SLAVE_0);
    for (uint32_t i = 0U; i < SPI_LOOPBACK_FRAMES; i++)
    {
        uint32_t tx = (i * 0x11U) & 0xFFU;
        uint32_t rx = 0U;
        uint32_t rx_ready_seen = 0U;

        result->frames++;
        switch (spi_transfer_frame_bounded(g_mss_spi1_lo.hw_reg, tx, &rx, &rx_ready_seen))
        {
            case SPI_XFER_TX_TIMEOUT:
                result->tx_timeouts++;
                break;

            case SPI_XFER_RX_TIMEOUT:
                result->rx_timeouts++;
                break;

            default:
                result->rx_ready_flags += rx_ready_seen;
                rx &= 0xFFU;
                if (rx != ((~tx) & 0xFFU))
                {
                    if (0U == result->errors)
                    {
                        result->first_bad_tx = tx;
                        result->first_bad_rx = rx;
                    }
                    result->errors++;
                }
                break;
        }
    }
    MSS_SPI_clear_slave_select(&g_mss_spi1_lo, MSS_SPI_SLAVE_0);
}
