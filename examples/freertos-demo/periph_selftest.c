/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Self-tests for the simulated peripherals. See periph_selftest.h.
 */

#include <string.h>

#include "mpfs_hal/mss_hal.h"
#include "drivers/mss/mss_spi/mss_spi.h"
#include "spi_flash.h"
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

/*------------------------------------------------------------------------------
 * Phase 2: SPI NOR flash
 */
#define FLASH_CLK_DIV       (8U)
#define FLASH_SPAN_OFFSET   (0x180U)  /* crosses from page 1 into page 2 */
#define FLASH_SPAN_LEN      (300U)

static uint8_t s_flash_buf[SPI_FLASH_PAGE_SIZE];

static const char *flash_status_str(spi_flash_status_t st)
{
    switch (st)
    {
        case SPI_FLASH_OK:          return "ok";
        case SPI_FLASH_ERR_BUS:     return "no response on the bus";
        case SPI_FLASH_ERR_TIMEOUT: return "still busy (WIP) after the poll limit";
        case SPI_FLASH_ERR_WEL:     return "write enable latch did not set";
        default:                    return "?";
    }
}

/* Reads [addr, addr + len) in page-sized chunks and checks every byte against
 * expect(offset). Returns the offset of the first mismatch, or len. */
static uint32_t flash_verify(uint32_t addr, uint32_t len, uint8_t (*expect)(uint32_t),
                             spi_flash_status_t *st, uint8_t *got)
{
    for (uint32_t done = 0U; done < len; )
    {
        uint32_t n = ((len - done) < sizeof(s_flash_buf)) ? (len - done) : sizeof(s_flash_buf);

        *st = spi_flash_read(addr + done, s_flash_buf, n);
        if (SPI_FLASH_OK != *st)
        {
            return done;
        }
        for (uint32_t i = 0U; i < n; i++)
        {
            if (s_flash_buf[i] != expect(done + i))
            {
                *got = s_flash_buf[i];
                return done + i;
            }
        }
        done += n;
    }
    return len;
}

static uint8_t expect_ff(uint32_t off)   { (void)off; return 0xFFU; }
static uint8_t expect_a5(uint32_t off)   { (void)off; return 0xA5U; }
static uint8_t expect_00(uint32_t off)   { (void)off; return 0x00U; }
static uint8_t span_byte(uint32_t off)   { return (uint8_t)((off * 7U) ^ 0x3CU); }

/* The sector as it should read after the span write: page 0 is 0x00 from the
 * 0xA5 / 0x5A step, the span holds span_byte(), the rest is erased. */
static uint8_t expect_after_span(uint32_t off)
{
    if (off < SPI_FLASH_PAGE_SIZE)
    {
        return 0x00U;
    }
    if ((off >= FLASH_SPAN_OFFSET) && (off < (FLASH_SPAN_OFFSET + FLASH_SPAN_LEN)))
    {
        return span_byte(off - FLASH_SPAN_OFFSET);
    }
    return 0xFFU;
}

/* Prints one step's result; returns 1 on failure. */
static uint32_t flash_step(selftest_print_t print, const char *step, spi_flash_status_t st,
                           uint32_t bad, uint32_t len, uint8_t got, uint8_t (*expect)(uint32_t))
{
    if (SPI_FLASH_OK != st)
    {
        print("[S] SPI0 flash: %-34s FAIL (%s)\r\n", step, flash_status_str(st));
        return 1U;
    }
    if (bad != len)
    {
        print("[S] SPI0 flash: %-34s FAIL at 0x%05lx: read 0x%02x, expected 0x%02x\r\n",
              step, (unsigned long)(SPI_FLASH_TEST_ADDR + bad), (unsigned)got,
              (unsigned)expect(bad));
        return 1U;
    }
    print("[S] SPI0 flash: %-34s PASS (busy for %lu status reads)\r\n",
          step, (unsigned long)spi_flash_last_busy_polls());
    return 0U;
}

uint32_t spi_flash_selftest(selftest_print_t print)
{
    static uint8_t span[FLASH_SPAN_LEN];
    uint8_t id[3] = { 0U, 0U, 0U };
    uint32_t failed = 0U;
    uint32_t bad;
    uint8_t got = 0U;
    spi_flash_status_t st;

    spi_flash_init(&g_mss_spi0_lo, FLASH_CLK_DIV);

    st = spi_flash_read_id(id);
    if (SPI_FLASH_OK != st)
    {
        print("[S] SPI0 flash: read ID FAIL (%s). Is the Renode model attached? "
              "See renode/README.md\r\n", flash_status_str(st));
        return 1U;
    }
    print("[S] SPI0 flash: JEDEC ID %02x %02x %02x\r\n",
          (unsigned)id[0], (unsigned)id[1], (unsigned)id[2]);

    /* 1. Erase: the whole sector reads 0xFF. */
    st = spi_flash_erase_sector(SPI_FLASH_TEST_ADDR);
    bad = SPI_FLASH_SECTOR_SIZE;
    if (SPI_FLASH_OK == st)
    {
        bad = flash_verify(SPI_FLASH_TEST_ADDR, SPI_FLASH_SECTOR_SIZE, expect_ff, &st, &got);
    }
    failed += flash_step(print, "erase sector -> 0xFF", st, bad, SPI_FLASH_SECTOR_SIZE, got, expect_ff);

    /* 2. Program page 0 with 0xA5. */
    (void)memset(s_flash_buf, 0xA5, sizeof(s_flash_buf));
    st = spi_flash_program(SPI_FLASH_TEST_ADDR, s_flash_buf, SPI_FLASH_PAGE_SIZE);
    bad = SPI_FLASH_PAGE_SIZE;
    if (SPI_FLASH_OK == st)
    {
        bad = flash_verify(SPI_FLASH_TEST_ADDR, SPI_FLASH_PAGE_SIZE, expect_a5, &st, &got);
    }
    failed += flash_step(print, "program 0xA5 -> 0xA5", st, bad, SPI_FLASH_PAGE_SIZE, got, expect_a5);

    /* 3. Program 0x5A over it: bits only clear, 0xA5 & 0x5A = 0x00. */
    (void)memset(s_flash_buf, 0x5A, sizeof(s_flash_buf));
    st = spi_flash_program(SPI_FLASH_TEST_ADDR, s_flash_buf, SPI_FLASH_PAGE_SIZE);
    bad = SPI_FLASH_PAGE_SIZE;
    if (SPI_FLASH_OK == st)
    {
        bad = flash_verify(SPI_FLASH_TEST_ADDR, SPI_FLASH_PAGE_SIZE, expect_00, &st, &got);
    }
    failed += flash_step(print, "program 0x5A over it -> 0x00", st, bad, SPI_FLASH_PAGE_SIZE, got, expect_00);

    /* 4. A write across a page boundary (split into two page programs); the
     *    rest of the sector must be unchanged. */
    for (uint32_t i = 0U; i < FLASH_SPAN_LEN; i++)
    {
        span[i] = span_byte(i);
    }
    st = spi_flash_program(SPI_FLASH_TEST_ADDR + FLASH_SPAN_OFFSET, span, FLASH_SPAN_LEN);
    bad = SPI_FLASH_SECTOR_SIZE;
    if (SPI_FLASH_OK == st)
    {
        bad = flash_verify(SPI_FLASH_TEST_ADDR, SPI_FLASH_SECTOR_SIZE, expect_after_span, &st, &got);
    }
    failed += flash_step(print, "program across a page boundary", st, bad, SPI_FLASH_SECTOR_SIZE, got,
                         expect_after_span);

    /* 5. Erase again: back to 0xFF. */
    st = spi_flash_erase_sector(SPI_FLASH_TEST_ADDR);
    bad = SPI_FLASH_SECTOR_SIZE;
    if (SPI_FLASH_OK == st)
    {
        bad = flash_verify(SPI_FLASH_TEST_ADDR, SPI_FLASH_SECTOR_SIZE, expect_ff, &st, &got);
    }
    failed += flash_step(print, "erase sector again -> 0xFF", st, bad, SPI_FLASH_SECTOR_SIZE, got, expect_ff);

    return failed;
}
