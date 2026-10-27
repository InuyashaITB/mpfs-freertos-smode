/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Minimal SPI NOR flash driver (3-byte addresses, 256-byte pages, 4 KiB
 * sectors) on an MSS SPI controller, as a master on slave select 0, mode 0,
 * 8-bit frames. Used against renode/models/NorFlash.cs on SPI0; the commands
 * are the common W25Q / MT25Q subset, so it also suits a real part.
 *
 * Every call is polled and every wait is bounded: a missing device returns
 * SPI_FLASH_ERR_BUS instead of hanging. Not thread-safe (one static command
 * buffer): call it from one task at a time, or before the scheduler starts.
 */

#ifndef SPI_FLASH_H_
#define SPI_FLASH_H_

#include <stdint.h>
#include "drivers/mss/mss_spi/mss_spi.h"

#define SPI_FLASH_PAGE_SIZE     (256U)
#define SPI_FLASH_SECTOR_SIZE   (4096U)

typedef enum
{
    SPI_FLASH_OK = 0,
    SPI_FLASH_ERR_BUS,      /* the controller received nothing back: no device */
    SPI_FLASH_ERR_TIMEOUT,  /* status register stayed busy (WIP) */
    SPI_FLASH_ERR_WEL,      /* write enable did not set the latch */
} spi_flash_status_t;

/* Configures the controller (MSS_SPI_init + master mode). clk_div: even,
 * 2..512. */
void spi_flash_init(mss_spi_instance_t *spi, uint32_t clk_div);

/* JEDEC ID (0x9F): manufacturer, memory type, capacity. */
spi_flash_status_t spi_flash_read_id(uint8_t id[3]);

spi_flash_status_t spi_flash_read(uint32_t addr, uint8_t *buf, uint32_t len);

/* Programs len bytes, split at page boundaries; each page: WREN, PP, wait.
 * Programming only clears bits: erase first to write arbitrary data. */
spi_flash_status_t spi_flash_program(uint32_t addr, const uint8_t *data, uint32_t len);

/* Erases the 4 KiB sector that contains addr: WREN, SE, wait. */
spi_flash_status_t spi_flash_erase_sector(uint32_t addr);

/* Polls the status register until WIP clears. *polls (if not NULL) gets the
 * number of reads that still reported busy. */
spi_flash_status_t spi_flash_wait_ready(uint32_t *polls);

/* Status reads that saw WIP during the last program or erase call. */
uint32_t spi_flash_last_busy_polls(void);

#endif /* SPI_FLASH_H_ */
