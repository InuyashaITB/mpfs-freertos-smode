/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * One bounded SPI master transfer. See spi_xfer.h.
 */

#include "mpfs_hal/mss_hal.h"
#include "drivers/mss/mss_spi/mss_spi.h"
#include "spi_xfer.h"

/* Polls of an empty RX FIFO before a transfer is abandoned (no device). */
#define XFER_POLL_LIMIT             (100000U)

/* MSS SPI register bits (the same values mss_spi.c uses privately). */
#define SPI_CTRL_ENABLE             (0x00000001U)
#define SPI_CTRL_TXRXDFCOUNT_MASK   (0x00FFFF00U)
#define SPI_CTRL_TXRXDFCOUNT_SHIFT  (8U)
#define SPI_FRAMESUP_MASK           (0xFFFF0000U)
#define SPI_STATUS_RX_FIFO_EMPTY    (0x00000040U)
#define SPI_STATUS_TX_FIFO_FULL     (0x00000100U)
#define SPI_CMD_RX_FIFO_RESET       (0x00000004U)
#define SPI_CMD_TX_FIFO_RESET       (0x00000008U)
#define SPI_RX_FIFO_SIZE            (4U)

spi_xfer_status_t spi_xfer(mss_spi_instance_t *spi, const uint8_t *tx, uint32_t tx_len,
                           uint8_t *rd, uint32_t rd_len)
{
    SPI_TypeDef *hw = spi->hw_reg;
    const uint32_t total = tx_len + rd_len;
    uint32_t tx_idx = 0U;
    uint32_t rx_idx = 0U;
    uint32_t in_flight = 0U;
    uint32_t idle = 0U;
    spi_xfer_status_t status = SPI_XFER_OK;

    MSS_SPI_set_slave_select(spi, MSS_SPI_SLAVE_0);

    hw->COMMAND |= (SPI_CMD_TX_FIFO_RESET | SPI_CMD_RX_FIFO_RESET);
    hw->CONTROL &= ~SPI_CTRL_ENABLE;
    hw->FRAMESUP = total & SPI_FRAMESUP_MASK;
    hw->CONTROL = (hw->CONTROL & ~SPI_CTRL_TXRXDFCOUNT_MASK)
                  | ((total << SPI_CTRL_TXRXDFCOUNT_SHIFT) & SPI_CTRL_TXRXDFCOUNT_MASK);
    hw->FRAMESIZE = 8U;
    hw->CONTROL |= SPI_CTRL_ENABLE;

    while (rx_idx < total)
    {
        if (0U == (hw->STATUS & SPI_STATUS_RX_FIFO_EMPTY))
        {
            uint8_t b = (uint8_t)hw->RX_DATA;

            if (rx_idx >= tx_len)
            {
                rd[rx_idx - tx_len] = b;
            }
            rx_idx++;
            in_flight--;
            idle = 0U;
        }
        else if (++idle >= XFER_POLL_LIMIT)
        {
            status = SPI_XFER_NO_RESPONSE;
            break;
        }

        if ((tx_idx < total) && (in_flight < SPI_RX_FIFO_SIZE) &&
            (0U == (hw->STATUS & SPI_STATUS_TX_FIFO_FULL)))
        {
            hw->TX_DATA = (tx_idx < tx_len) ? tx[tx_idx] : 0xFFU;
            tx_idx++;
            in_flight++;
        }
    }

    MSS_SPI_clear_slave_select(spi, MSS_SPI_SLAVE_0);
    return status;
}
