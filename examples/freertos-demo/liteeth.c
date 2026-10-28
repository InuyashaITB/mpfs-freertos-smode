/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Minimal LiteEth driver. See liteeth.h.
 */

#include <stddef.h>

#include "liteeth.h"

/* CSR offsets (Renode 1.13 LiteX_Ethernet / LiteEth with 8-bit CSRs). */
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

#define RX_SLOTS                (2U)
#define TX_SLOTS                (2U)

/* Polls of ReaderReady before a send gives up. */
#define TX_READY_POLL_LIMIT     (100000U)

static uint32_t s_tx_slot = 0U;
static liteeth_stats_t s_stats;

static inline volatile uint32_t *csr(uint32_t offset)
{
    return (volatile uint32_t *)(LITEETH_CSR_BASE + offset);
}

static inline volatile uint32_t *slot_word(uint32_t slot, uint32_t offset)
{
    return (volatile uint32_t *)(LITEETH_BUF_BASE + (slot * LITEETH_SLOT_SIZE) + offset);
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

void liteeth_init(void)
{
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

liteeth_status_t liteeth_receive(uint8_t *buf, uint32_t size, uint32_t *len)
{
    liteeth_status_t st = LITEETH_OK;
    uint32_t slot;
    uint32_t length;

    if (0U == (*csr(CSR_WRITER_EV_PENDING) & 1U))
    {
        return LITEETH_NONE;
    }

    /* Reading the slot number latches it; acknowledging the event below
     * releases exactly that slot. */
    slot = *csr(CSR_WRITER_SLOT) & 0xFFU;
    length = csr_read_multi(CSR_WRITER_LENGTH, 4U);

    if ((slot >= RX_SLOTS) || (length > LITEETH_SLOT_SIZE) || (length > size))
    {
        s_stats.rx_too_long++;
        st = LITEETH_ERR_TOO_LONG;
    }
    else
    {
        for (uint32_t off = 0U; off < length; off += 4U)
        {
            uint32_t w = *slot_word(slot, off);

            for (uint32_t b = 0U; (b < 4U) && ((off + b) < length); b++)
            {
                buf[off + b] = (uint8_t)(w >> (8U * b));
            }
        }
        *len = length;
        s_stats.rx++;
    }

    *csr(CSR_WRITER_EV_PENDING) = 1U;
    return st;
}

void liteeth_get_stats(liteeth_stats_t *stats)
{
    *stats = s_stats;
}
