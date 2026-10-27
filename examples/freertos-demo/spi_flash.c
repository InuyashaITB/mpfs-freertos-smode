/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Minimal SPI NOR flash driver. See spi_flash.h.
 */

#include <stddef.h>
#include <string.h>

#include "mpfs_hal/mss_hal.h"
#include "drivers/mss/mss_spi/mss_spi.h"
#include "spi_flash.h"

#define CMD_WRITE_ENABLE    (0x06U)
#define CMD_READ_STATUS     (0x05U)
#define CMD_READ            (0x03U)
#define CMD_PAGE_PROGRAM    (0x02U)
#define CMD_SECTOR_ERASE    (0x20U)
#define CMD_READ_ID         (0x9FU)

#define STATUS_WIP          (0x01U)
#define STATUS_WEL          (0x02U)

/* Longest single transfer: opcode + address + one page. Reads are split into
 * chunks of the same size. */
#define HEADER_SIZE         (4U)
#define XFER_MAX_DATA       (SPI_FLASH_PAGE_SIZE)

/* Polls of an empty RX FIFO before a transfer is abandoned (no device). */
#define XFER_POLL_LIMIT     (100000U)

/* Status reads before spi_flash_wait_ready() gives up. A 4 KiB sector erase
 * takes up to ~400 ms on real parts; one status read is 16 SPI clocks. */
#define READY_POLL_LIMIT    (1000000U)

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

static mss_spi_instance_t *s_spi = NULL;
static uint8_t s_cmd[HEADER_SIZE + XFER_MAX_DATA];
static uint32_t s_last_busy_polls = 0U;

/* One command, chip select held from the first byte to the last: sends
 * cmd_len bytes of cmd, then clocks in rd_len bytes (sending 0xFF).
 *
 * The same algorithm as MSS_SPI_transfer_block() (frame count = whole
 * command, at most a few bytes in flight), but bounded. The frame count
 * matters in Renode: MPFS_SPI ends the slave's command when it is reached, so
 * a command must never be split over two transfers. */
static spi_flash_status_t spi_xfer(const uint8_t *cmd, uint32_t cmd_len, uint8_t *rd, uint32_t rd_len)
{
    SPI_TypeDef *spi = s_spi->hw_reg;
    const uint32_t total = cmd_len + rd_len;
    uint32_t tx_idx = 0U;
    uint32_t rx_idx = 0U;
    uint32_t in_flight = 0U;
    uint32_t idle = 0U;
    spi_flash_status_t status = SPI_FLASH_OK;

    MSS_SPI_set_slave_select(s_spi, MSS_SPI_SLAVE_0);

    spi->COMMAND |= (SPI_CMD_TX_FIFO_RESET | SPI_CMD_RX_FIFO_RESET);
    spi->CONTROL &= ~SPI_CTRL_ENABLE;
    spi->FRAMESUP = total & SPI_FRAMESUP_MASK;
    spi->CONTROL = (spi->CONTROL & ~SPI_CTRL_TXRXDFCOUNT_MASK)
                   | ((total << SPI_CTRL_TXRXDFCOUNT_SHIFT) & SPI_CTRL_TXRXDFCOUNT_MASK);
    spi->FRAMESIZE = 8U;
    spi->CONTROL |= SPI_CTRL_ENABLE;

    while (rx_idx < total)
    {
        if (0U == (spi->STATUS & SPI_STATUS_RX_FIFO_EMPTY))
        {
            uint8_t b = (uint8_t)spi->RX_DATA;

            if (rx_idx >= cmd_len)
            {
                rd[rx_idx - cmd_len] = b;
            }
            rx_idx++;
            in_flight--;
            idle = 0U;
        }
        else if (++idle >= XFER_POLL_LIMIT)
        {
            status = SPI_FLASH_ERR_BUS;
            break;
        }

        if ((tx_idx < total) && (in_flight < SPI_RX_FIFO_SIZE) &&
            (0U == (spi->STATUS & SPI_STATUS_TX_FIFO_FULL)))
        {
            spi->TX_DATA = (tx_idx < cmd_len) ? cmd[tx_idx] : 0xFFU;
            tx_idx++;
            in_flight++;
        }
    }

    MSS_SPI_clear_slave_select(s_spi, MSS_SPI_SLAVE_0);
    return status;
}

static spi_flash_status_t read_status(uint8_t *sr)
{
    const uint8_t cmd = CMD_READ_STATUS;

    return spi_xfer(&cmd, 1U, sr, 1U);
}

/* WREN, then check that the latch is set: a part that is busy, or a command
 * the device did not see, shows up here rather than as a silent no-op. */
static spi_flash_status_t write_enable(void)
{
    const uint8_t cmd = CMD_WRITE_ENABLE;
    uint8_t sr = 0U;
    spi_flash_status_t st = spi_xfer(&cmd, 1U, NULL, 0U);

    if (SPI_FLASH_OK == st)
    {
        st = read_status(&sr);
    }
    if ((SPI_FLASH_OK == st) && (0U == (sr & STATUS_WEL)))
    {
        st = SPI_FLASH_ERR_WEL;
    }
    return st;
}

static void put_header(uint8_t opcode, uint32_t addr)
{
    s_cmd[0] = opcode;
    s_cmd[1] = (uint8_t)(addr >> 16);
    s_cmd[2] = (uint8_t)(addr >> 8);
    s_cmd[3] = (uint8_t)addr;
}

void spi_flash_init(mss_spi_instance_t *spi, uint32_t clk_div)
{
    s_spi = spi;
    MSS_SPI_init(spi);
    MSS_SPI_configure_master_mode(spi, MSS_SPI_SLAVE_0, MSS_SPI_MODE0, clk_div, 8U, NULL);
}

spi_flash_status_t spi_flash_read_id(uint8_t id[3])
{
    const uint8_t cmd = CMD_READ_ID;

    return spi_xfer(&cmd, 1U, id, 3U);
}

spi_flash_status_t spi_flash_wait_ready(uint32_t *polls)
{
    uint32_t n;
    uint8_t sr = STATUS_WIP;
    spi_flash_status_t st = SPI_FLASH_OK;

    for (n = 0U; n < READY_POLL_LIMIT; n++)
    {
        st = read_status(&sr);
        if ((SPI_FLASH_OK != st) || (0U == (sr & STATUS_WIP)))
        {
            break;
        }
    }
    if (NULL != polls)
    {
        *polls = n;
    }
    if ((SPI_FLASH_OK == st) && (0U != (sr & STATUS_WIP)))
    {
        st = SPI_FLASH_ERR_TIMEOUT;
    }
    return st;
}

spi_flash_status_t spi_flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    spi_flash_status_t st = SPI_FLASH_OK;

    while ((len > 0U) && (SPI_FLASH_OK == st))
    {
        uint32_t n = (len < XFER_MAX_DATA) ? len : XFER_MAX_DATA;

        put_header(CMD_READ, addr);
        st = spi_xfer(s_cmd, HEADER_SIZE, buf, n);
        addr += n;
        buf += n;
        len -= n;
    }
    return st;
}

spi_flash_status_t spi_flash_program(uint32_t addr, const uint8_t *data, uint32_t len)
{
    spi_flash_status_t st = SPI_FLASH_OK;

    s_last_busy_polls = 0U;
    while ((len > 0U) && (SPI_FLASH_OK == st))
    {
        uint32_t room = SPI_FLASH_PAGE_SIZE - (addr % SPI_FLASH_PAGE_SIZE);
        uint32_t n = (len < room) ? len : room;
        uint32_t polls = 0U;

        st = write_enable();
        if (SPI_FLASH_OK == st)
        {
            put_header(CMD_PAGE_PROGRAM, addr);
            (void)memcpy(&s_cmd[HEADER_SIZE], data, n);
            st = spi_xfer(s_cmd, HEADER_SIZE + n, NULL, 0U);
        }
        if (SPI_FLASH_OK == st)
        {
            st = spi_flash_wait_ready(&polls);
            s_last_busy_polls += polls;
        }
        addr += n;
        data += n;
        len -= n;
    }
    return st;
}

spi_flash_status_t spi_flash_erase_sector(uint32_t addr)
{
    spi_flash_status_t st = write_enable();

    s_last_busy_polls = 0U;
    if (SPI_FLASH_OK == st)
    {
        put_header(CMD_SECTOR_ERASE, addr);
        st = spi_xfer(s_cmd, HEADER_SIZE, NULL, 0U);
    }
    if (SPI_FLASH_OK == st)
    {
        st = spi_flash_wait_ready(&s_last_busy_polls);
    }
    return st;
}

uint32_t spi_flash_last_busy_polls(void)
{
    return s_last_busy_polls;
}
