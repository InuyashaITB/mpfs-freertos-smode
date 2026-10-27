/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Self-tests for the simulated peripherals (branch sim-peripherals). Run from
 * s_mode_main() in S-mode before the scheduler starts (polled, no interrupts).
 * Every wait is bounded, so a missing or misbehaving simulated device shows up
 * as a timeout instead of hanging the boot.
 */

#ifndef PERIPH_SELFTEST_H_
#define PERIPH_SELFTEST_H_

#include <stdint.h>

typedef struct
{
    uint32_t frames;        /* frames attempted */
    uint32_t errors;        /* replies that were not the complement */
    uint32_t tx_timeouts;   /* frames where STATUS.TX_DONE never came */
    uint32_t rx_timeouts;   /* frames where the RX FIFO stayed empty */
    uint32_t rx_ready_flags;/* frames where STATUS.RX_DATA_READY was also set
                             * (0 in Renode 1.13: see periph_selftest.c) */
    uint32_t first_bad_tx;  /* first wrong reply: sent ... */
    uint32_t first_bad_rx;  /* ... and received */
} spi_loopback_result_t;

/* Phase 1: full-duplex transfers on MSS SPI1 against renode/models/
 * SpiLoopback.cs, which answers each byte with its complement. Exercises
 * MSS_SPI_init() / MSS_SPI_configure_master_mode() (HAL PLIC helpers in
 * S-mode), then transfers frames with bounded polling. */
void spi_loopback_selftest(spi_loopback_result_t *result);

#endif /* PERIPH_SELFTEST_H_ */
