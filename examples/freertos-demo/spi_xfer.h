/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * One bounded SPI master transfer on an MSS SPI controller, chip select 0
 * held from the first byte to the last. Shared by spi_flash.c and
 * proto_dev.c.
 *
 * The same algorithm as MSS_SPI_transfer_block() (frame count = whole
 * transfer, at most a few bytes in flight), but every wait is bounded, so a
 * missing device returns SPI_XFER_NO_RESPONSE instead of hanging. In Renode
 * the frame count matters: MPFS_SPI ends the device's transfer when it is
 * reached, so one logical transfer must not be split over two calls.
 */

#ifndef SPI_XFER_H_
#define SPI_XFER_H_

#include <stdint.h>
#include "drivers/mss/mss_spi/mss_spi.h"

typedef enum
{
    SPI_XFER_OK = 0,
    SPI_XFER_NO_RESPONSE,   /* the controller received nothing back */
} spi_xfer_status_t;

/* Sends tx_len bytes of tx, then clocks in rd_len bytes into rd (sending
 * 0xFF). The bytes received while tx is sent are discarded. */
spi_xfer_status_t spi_xfer(mss_spi_instance_t *spi, const uint8_t *tx, uint32_t tx_len,
                           uint8_t *rd, uint32_t rd_len);

#endif /* SPI_XFER_H_ */
