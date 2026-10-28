/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Self-tests for the simulated peripherals. See periph_selftest.h.
 */

#include <stdio.h>
#include <string.h>

#include "mpfs_hal/mss_hal.h"
#include "drivers/mss/mss_spi/mss_spi.h"
#include "spi_flash.h"
#include "proto_dev.h"
#include "i2c_eeprom.h"
#include "periph_selftest.h"

/*------------------------------------------------------------------------------
 * Phase 4: example-protocol SPI device
 */
#define PROTO_CLK_DIV       (8U)
#define PROTO_ECHO_LEN      (24U)
#define PROTO_TEST_REG      (5U)
#define PROTO_TEST_VALUE    (0x3CU)
#define PROTO_ABSENT_REG    (0x20U)
#define PROTO_BAD_CMD       (0x7EU)

/* Prints one step; returns 1 on failure. */
static uint32_t proto_step(selftest_print_t print, const char *step, proto_result_t r,
                           uint32_t ok, const proto_response_t *rsp)
{
    if ((PROTO_OK != r) && (PROTO_NAK != r))
    {
        print("[S] SPI1 proto: %-38s FAIL (%s)\r\n", step, proto_result_str(r));
        return 1U;
    }
    print("[S] SPI1 proto: %-38s %s (status 0x%02x, %lu marker polls)\r\n",
          step, (0U != ok) ? "PASS" : "FAIL", (unsigned)rsp->status,
          (unsigned long)rsp->marker_polls);
    return (0U != ok) ? 0U : 1U;
}

/* A NAK with the given status is the expected outcome. */
static uint32_t is_nak(proto_result_t r, const proto_response_t *rsp, uint8_t status)
{
    return ((PROTO_NAK == r) && (status == rsp->status)) ? 1U : 0U;
}

uint32_t spi_proto_selftest(selftest_print_t print)
{
    static proto_response_t rsp;
    uint8_t echo[PROTO_ECHO_LEN];
    uint8_t value = 0U;
    uint32_t failed = 0U;
    proto_result_t r;

    /* MSS_SPI_init() calls PLIC_DisableIRQ(), which in S-mode must use the
     * S context instead of reading mhartid (mss_plic.h). */
    proto_init(&g_mss_spi1_lo, PROTO_CLK_DIV);

    r = proto_ping(&rsp);
    if ((PROTO_ERR_BUS == r) || (PROTO_ERR_NO_MARKER == r))
    {
        print("[S] SPI1 proto: PING FAIL (%s). Is the Renode model attached? "
              "See renode/README.md\r\n", proto_result_str(r));
        return 1U;
    }
    failed += proto_step(print, "PING -> ID c5, version 01", r,
                         (PROTO_OK == r) && (2U == rsp.len) && (0xC5U == rsp.payload[0]) &&
                         (0x01U == rsp.payload[1]), &rsp);

    for (uint32_t i = 0U; i < PROTO_ECHO_LEN; i++)
    {
        echo[i] = (uint8_t)((i * 11U) ^ 0x5AU);
    }
    r = proto_transact(PROTO_CMD_ECHO, echo, PROTO_ECHO_LEN, &rsp, 0U);
    failed += proto_step(print, "ECHO 24 bytes", r,
                         (PROTO_OK == r) && (PROTO_ECHO_LEN == rsp.len) &&
                         (0 == memcmp(rsp.payload, echo, PROTO_ECHO_LEN)), &rsp);

    r = proto_write_reg(PROTO_TEST_REG, PROTO_TEST_VALUE, &rsp);
    if (PROTO_OK == r)
    {
        r = proto_read_reg(PROTO_TEST_REG, &value, &rsp);
    }
    failed += proto_step(print, "WRITE_REG 5 = 3c, READ_REG 5 -> 3c", r,
                         (PROTO_OK == r) && (PROTO_TEST_VALUE == value), &rsp);

    r = proto_read_reg(PROTO_ABSENT_REG, &value, &rsp);
    failed += proto_step(print, "READ_REG 0x20 -> NAK BAD_ADDR", r,
                         is_nak(r, &rsp, PROTO_STATUS_BAD_ADDR), &rsp);

    r = proto_write_reg(0U, 0x00U, &rsp);
    failed += proto_step(print, "WRITE_REG 0 (ID) -> NAK READ_ONLY", r,
                         is_nak(r, &rsp, PROTO_STATUS_READ_ONLY), &rsp);

    r = proto_transact(PROTO_CMD_ECHO, echo, 4U, &rsp, 1U);
    failed += proto_step(print, "corrupted CRC -> NAK BAD_CRC", r,
                         is_nak(r, &rsp, PROTO_STATUS_BAD_CRC), &rsp);

    r = proto_transact(PROTO_BAD_CMD, NULL, 0U, &rsp, 0U);
    failed += proto_step(print, "command 0x7e -> NAK UNKNOWN_CMD", r,
                         is_nak(r, &rsp, PROTO_STATUS_UNKNOWN_CMD), &rsp);

    r = proto_get_events(&rsp);
    failed += proto_step(print, "GET_EVENTS -> none queued", r,
                         (PROTO_OK == r) && (0U == rsp.len), &rsp);

    return failed;
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

/*------------------------------------------------------------------------------
 * Phase 3: I2C EEPROM
 */
#define EE_WRAP_ADDR        (0x003CU)   /* 4 bytes before the end of page 0 */
#define EE_WRAP_LEN         (8U)
#define EE_MARK_ADDR        (0x0040U)   /* start of page 1: must stay unchanged */
#define EE_SPLIT_ADDR       (0x0070U)   /* pages 1, 2 and 3 */
#define EE_SPLIT_LEN        (100U)
#define EE_RANDOM_ADDR      (0x0080U)

static const char *eeprom_status_str(eeprom_status_t st)
{
    switch (st)
    {
        case EEPROM_OK:             return "ok";
        case EEPROM_ERR_NACK:       return "NACK (no device, or it stayed busy)";
        case EEPROM_ERR_TIMEOUT:    return "transaction did not complete";
        default:                    return "?";
    }
}

static uint8_t ee_split_byte(uint32_t i) { return (uint8_t)((i * 13U) + 0x21U); }

/* Prints one step; returns 1 on failure. */
static uint32_t ee_step(selftest_print_t print, const char *step, eeprom_status_t st,
                        uint32_t ok, const char *detail)
{
    if (EEPROM_OK != st)
    {
        print("[S] I2C0 EEPROM: %-36s FAIL (%s)\r\n", step, eeprom_status_str(st));
        return 1U;
    }
    print("[S] I2C0 EEPROM: %-36s %s%s\r\n", step, (0U != ok) ? "PASS" : "FAIL", detail);
    return (0U != ok) ? 0U : 1U;
}

uint32_t i2c_eeprom_selftest(selftest_print_t print)
{
    static const uint8_t mark[4] = { 0x11U, 0x22U, 0x33U, 0x44U };
    static uint8_t data[EE_SPLIT_LEN];
    static uint8_t rd[EE_SPLIT_LEN];
    static char detail[64];
    uint8_t wrap[EE_WRAP_LEN];
    uint32_t failed = 0U;
    uint32_t ok;
    eeprom_status_t st;

    eeprom_init(&g_mss_i2c0_lo, I2C0_MAIN_PLIC, EEPROM_I2C_ADDR);

    /* 1. One raw transaction of 8 bytes at 0x3C: the last 4 wrap to 0x00.
     *    Page 1 (marked first) must be unchanged. */
    st = eeprom_write(EE_MARK_ADDR, mark, sizeof(mark));
    if (EEPROM_ERR_NACK == st)
    {
        print("[S] I2C0 EEPROM: no device at 0x%02x (%s). Is the Renode model attached? "
              "See renode/README.md\r\n", (unsigned)EEPROM_I2C_ADDR, eeprom_status_str(st));
        return 1U;
    }
    for (uint32_t i = 0U; i < EE_WRAP_LEN; i++)
    {
        wrap[i] = (uint8_t)(0xA0U + i);
    }
    if (EEPROM_OK == st)
    {
        st = eeprom_write_raw(EE_WRAP_ADDR, wrap, EE_WRAP_LEN);
    }
    ok = 0U;
    if (EEPROM_OK == st)
    {
        st = eeprom_read(EE_WRAP_ADDR, rd, 4U);
    }
    if (EEPROM_OK == st)
    {
        ok = (0 == memcmp(rd, wrap, 4U)) ? 1U : 0U;
        st = eeprom_read(0x0000U, rd, 4U);
    }
    if (EEPROM_OK == st)
    {
        ok &= (0 == memcmp(rd, &wrap[4], 4U)) ? 1U : 0U;
        st = eeprom_read(EE_MARK_ADDR, rd, 4U);
    }
    if (EEPROM_OK == st)
    {
        ok &= (0 == memcmp(rd, mark, 4U)) ? 1U : 0U;
    }
    failed += ee_step(print, "raw write at 0x3C wraps to 0x00", st, ok, "");

    /* 2. The same kind of write done right: split at page boundaries, ACK
     *    polling after each page. */
    for (uint32_t i = 0U; i < EE_SPLIT_LEN; i++)
    {
        data[i] = ee_split_byte(i);
    }
    ok = 0U;
    st = eeprom_write(EE_SPLIT_ADDR, data, EE_SPLIT_LEN);
    uint32_t busy = eeprom_last_busy_polls();
    if (EEPROM_OK == st)
    {
        (void)memset(rd, 0, sizeof(rd));
        st = eeprom_read(EE_SPLIT_ADDR, rd, EE_SPLIT_LEN);
    }
    if (EEPROM_OK == st)
    {
        ok = ((0 == memcmp(rd, data, EE_SPLIT_LEN)) && (busy > 0U)) ? 1U : 0U;
    }
    (void)snprintf(detail, sizeof(detail), " (3 pages, %lu busy ACK polls)", (unsigned long)busy);
    failed += ee_step(print, "split write of 100 bytes at 0x70", st, ok, detail);

    /* 3. Random read of 4 bytes, then a current-address read of 4 more. */
    ok = 0U;
    st = eeprom_read(EE_RANDOM_ADDR, rd, 4U);
    if (EEPROM_OK == st)
    {
        st = eeprom_read_current(&rd[4], 4U);
    }
    if (EEPROM_OK == st)
    {
        ok = (0 == memcmp(rd, &data[EE_RANDOM_ADDR - EE_SPLIT_ADDR], 8U)) ? 1U : 0U;
    }
    failed += ee_step(print, "random + current-address read", st, ok, "");

    /* 4. No device at 0x51: reported as a NACK, not a hang. */
    {
        mss_i2c_status_t probe = eeprom_probe(EEPROM_ABSENT_ADDR);

        ok = (MSS_I2C_FAILED == probe) ? 1U : 0U;
        (void)snprintf(detail, sizeof(detail), " (status %u, expected %u = NACK)",
                       (unsigned)probe, (unsigned)MSS_I2C_FAILED);
        failed += ee_step(print, "absent device 0x51 is NACKed", EEPROM_OK, ok, detail);
    }

    return failed;
}
