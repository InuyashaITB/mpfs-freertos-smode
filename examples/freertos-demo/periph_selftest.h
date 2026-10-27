/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Self-tests for the simulated peripherals (branch sim-peripherals). Run from
 * s_mode_main() in S-mode before the scheduler starts (polled, no interrupts).
 */

#ifndef PERIPH_SELFTEST_H_
#define PERIPH_SELFTEST_H_

#include <stdint.h>

typedef struct
{
    uint32_t frames;        /* frames transferred */
    uint32_t errors;        /* replies that were not the complement */
    uint32_t first_bad_tx;  /* first failing frame: sent ... */
    uint32_t first_bad_rx;  /* ... and received */
} spi_loopback_result_t;

/* Phase 1: full-duplex transfers on MSS SPI1 against renode/models/
 * SpiLoopback.cs, which answers each byte with its complement. Exercises
 * MSS_SPI_init() (HAL PLIC helpers in S-mode) and polled transfers. */
void spi_loopback_selftest(spi_loopback_result_t *result);

#endif /* PERIPH_SELFTEST_H_ */
