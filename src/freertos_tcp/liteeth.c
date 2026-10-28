/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Minimal LiteEth driver. See liteeth.h.
 */

#include <stddef.h>

#include "liteeth.h"

/* CSR offsets (LiteEth with 8-bit CSRs, as Renode 1.13's LiteX_Ethernet). */
#define CSR_WRITER_SLOT         (0x00U)
#define CSR_WRITER_LENGTH       (0x04U)     /* 4 subregisters, MSB first */
#define CSR_WRITER_EV_PENDING   (0x28U)
#define CSR_WRITER_EV_ENABLE    (0x2CU)
#define CSR_READER_START        (0x30U)
#define CSR_READER_READY        (0x34U)
#define CSR_READER_SLOT         (0x3CU)
#define CSR_READER_LENGTH       (0x40U)     /* 2 subregisters, MSB first */
#define CSR_READER_EV_PENDING   (0x4CU)
#define CSR_READER_EV_ENABLE    (0x50U)

/* MDIO bit-bang registers. */
#define MDIO_W                  (0x04U)
#define MDIO_R                  (0x08U)
#define MDIO_W_CLK              (1U << 0)
#define MDIO_W_OE               (1U << 1)
#define MDIO_W_DATA             (1U << 2)

#define RX_SLOTS                (2U)
#define TX_SLOTS                (2U)

/* Polls of ReaderReady before a send gives up. */
#define TX_READY_POLL_LIMIT     (100000U)

static uintptr_t s_csr;
static uintptr_t s_buf;
static uintptr_t s_mdio;
static uint32_t s_tx_slot = 0U;
static uint32_t s_rx_slot = 0U;
static uint32_t s_rx_len = 0U;
static liteeth_stats_t s_stats;

static inline volatile uint32_t *csr(uint32_t offset)
{
    return (volatile uint32_t *)(s_csr + offset);
}

static inline volatile uint32_t *slot_word(uint32_t slot, uint32_t offset)
{
    return (volatile uint32_t *)(s_buf + (slot * LITEETH_SLOT_SIZE) + offset);
}

static inline volatile uint32_t *mdio(uint32_t offset)
{
    return (volatile uint32_t *)(s_mdio + offset);
}

/* A multi-byte CSR spread over n 8-bit subregisters, MSB first. */
static uint32_t csr_read_multi(uint32_t offset, uint32_t n)
{
    uint32_t v = 0U;

    for (uint32_t i = 0U; i < n; i++)
    {
        v = (v << 8) | (*csr(offset + (4U * i)) & 0xFFU);
    }
    return v;
}

static void csr_write_multi(uint32_t offset, uint32_t n, uint32_t v)
{
    for (uint32_t i = 0U; i < n; i++)
    {
        *csr(offset + (4U * i)) = (v >> (8U * (n - 1U - i))) & 0xFFU;
    }
}

void liteeth_init(uintptr_t csr_base, uintptr_t buf_base, uintptr_t mdio_base)
{
    s_csr = csr_base;
    s_buf = buf_base;
    s_mdio = mdio_base;
    s_tx_slot = 0U;
    s_stats = (liteeth_stats_t){ 0 };

    /* With no slot latched, acknowledging the writer event releases all RX
     * slots. */
    *csr(CSR_WRITER_EV_PENDING) = 1U;
    *csr(CSR_READER_EV_PENDING) = 1U;
    *csr(CSR_READER_EV_ENABLE) = 0U;
    *csr(CSR_WRITER_EV_ENABLE) = 1U;
}

liteeth_status_t liteeth_send(const uint8_t *frame, uint32_t len)
{
    const uint32_t padded = (len < LITEETH_MIN_FRAME) ? LITEETH_MIN_FRAME : len;
    uint32_t n;

    if (padded > LITEETH_SLOT_SIZE)
    {
        return LITEETH_ERR_TOO_LONG;
    }
    for (n = 0U; (n < TX_READY_POLL_LIMIT) && (0U == (*csr(CSR_READER_READY) & 1U)); n++)
    {
    }
    if (n == TX_READY_POLL_LIMIT)
    {
        s_stats.tx_busy++;
        return LITEETH_ERR_TX_BUSY;
    }

    /* Word-wise copy (the buffer takes 32-bit accesses), little-endian. */
    for (uint32_t off = 0U; off < padded; off += 4U)
    {
        uint32_t w = 0U;

        for (uint32_t b = 0U; b < 4U; b++)
        {
            uint32_t i = off + b;
            w |= (uint32_t)((i < len) ? frame[i] : 0U) << (8U * b);
        }
        *slot_word(RX_SLOTS + s_tx_slot, off) = w;
    }

    *csr(CSR_READER_SLOT) = s_tx_slot;
    csr_write_multi(CSR_READER_LENGTH, 2U, padded);
    *csr(CSR_READER_START) = 1U;
    *csr(CSR_READER_EV_PENDING) = 1U;   /* not used: clear "sent" */

    s_tx_slot = (s_tx_slot + 1U) % TX_SLOTS;
    s_stats.tx++;
    return LITEETH_OK;
}

liteeth_status_t liteeth_receive_begin(uint32_t *len)
{
    if (0U == (*csr(CSR_WRITER_EV_PENDING) & 1U))
    {
        return LITEETH_NONE;
    }

    /* Reading the slot number latches it; acknowledging the event in
     * liteeth_receive_end() releases exactly that slot. */
    s_rx_slot = *csr(CSR_WRITER_SLOT) & 0xFFU;
    s_rx_len = csr_read_multi(CSR_WRITER_LENGTH, 4U);
    if ((s_rx_slot >= RX_SLOTS) || (s_rx_len > LITEETH_SLOT_SIZE))
    {
        s_rx_len = 0U;
        *len = 0U;
        return LITEETH_ERR_TOO_LONG;
    }
    *len = s_rx_len;
    return LITEETH_OK;
}

void liteeth_receive_copy(uint8_t *buf, uint32_t len)
{
    if (len > s_rx_len)
    {
        len = s_rx_len;
    }
    for (uint32_t off = 0U; off < len; off += 4U)
    {
        uint32_t w = *slot_word(s_rx_slot, off);

        for (uint32_t b = 0U; (b < 4U) && ((off + b) < len); b++)
        {
            buf[off + b] = (uint8_t)(w >> (8U * b));
        }
    }
}

void liteeth_receive_end(uint8_t copied)
{
    *csr(CSR_WRITER_EV_PENDING) = 1U;
    if (0U != copied)
    {
        s_stats.rx++;
    }
    else
    {
        s_stats.rx_dropped++;
    }
}

/*------------------------------------------------------------------------------
 * MDIO (clause 22), bit-banged. The PHY samples MDIO on the rising edge of
 * MDC and drives read data after it.
 */
static void mdio_bit_out(uint32_t bit)
{
    const uint32_t d = (0U != bit) ? MDIO_W_DATA : 0U;

    *mdio(MDIO_W) = MDIO_W_OE | d;
    *mdio(MDIO_W) = MDIO_W_OE | d | MDIO_W_CLK;
    *mdio(MDIO_W) = MDIO_W_OE | d;
}

/* One clock with the line released (turnaround): nothing to read. */
static void mdio_release_clock(void)
{
    *mdio(MDIO_W) = 0U;
    *mdio(MDIO_W) = MDIO_W_CLK;
    *mdio(MDIO_W) = 0U;
}

static uint32_t mdio_bit_in(void)
{
    uint32_t bit;

    /* Output disabled and driven low, so the clock edges of the data phase
     * look like zeros to anything listening. */
    *mdio(MDIO_W) = 0U;
    *mdio(MDIO_W) = MDIO_W_CLK;
    bit = *mdio(MDIO_R) & 1U;
    *mdio(MDIO_W) = 0U;
    return bit;
}

/* Preamble (32 ones), start 01, opcode, PHY and register address. */
static void mdio_header(uint32_t opcode, uint8_t phy, uint8_t reg)
{
    for (uint32_t i = 0U; i < 32U; i++)
    {
        mdio_bit_out(1U);
    }
    mdio_bit_out(0U);
    mdio_bit_out(1U);
    mdio_bit_out((opcode >> 1) & 1U);
    mdio_bit_out(opcode & 1U);
    for (int32_t i = 4; i >= 0; i--)
    {
        mdio_bit_out(((uint32_t)phy >> i) & 1U);
    }
    for (int32_t i = 4; i >= 0; i--)
    {
        mdio_bit_out(((uint32_t)reg >> i) & 1U);
    }
}

uint16_t liteeth_mdio_read(uint8_t phy, uint8_t reg)
{
    uint16_t v = 0U;

    mdio_header(2U, phy, reg);      /* read: opcode 10 */
    /* Turnaround: the master releases the line for two clocks (Renode's
     * model counts them as part of the 16-bit command word). The PHY drives
     * nothing yet, so they are not sampled. */
    mdio_release_clock();
    mdio_release_clock();
    for (uint32_t i = 0U; i < 16U; i++)
    {
        v = (uint16_t)((v << 1) | mdio_bit_in());
    }
    return v;
}

void liteeth_mdio_write(uint8_t phy, uint8_t reg, uint16_t value)
{
    mdio_header(1U, phy, reg);      /* write: opcode 01 */
    mdio_bit_out(1U);               /* turnaround 10 */
    mdio_bit_out(0U);
    for (int32_t i = 15; i >= 0; i--)
    {
        mdio_bit_out(((uint32_t)value >> i) & 1U);
    }
    *mdio(MDIO_W) = 0U;
}

void liteeth_get_stats(liteeth_stats_t *stats)
{
    *stats = s_stats;
}
