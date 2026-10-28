/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Minimal driver for a LiteEth soft MAC in the fabric (renode: LiteX_Ethernet
 * on FIC3, see renode/README.md). No DMA: frames are copied through slot
 * buffers in the MAC.
 *   CSRs        0x4000_0000  (old LiteX layout: 8-bit subregisters, 4-byte
 *                             stride, multi-byte values MSB first)
 *   buffers     0x4001_0000  2 RX ("writer") slots, then 2 TX ("reader")
 *                             slots, 0x800 bytes each, 32-bit accesses
 *   IRQ         PLIC 120 (F2M_2), high while a received frame is pending
 * The window is identity mapped by the library's Sv39 table (gigapage 1).
 *
 * Polled, every wait bounded. Not thread-safe: one task at a time.
 */

#ifndef LITEETH_H_
#define LITEETH_H_

#include <stdint.h>

#define LITEETH_CSR_BASE        (0x40000000UL)
#define LITEETH_BUF_BASE        (0x40010000UL)

#define LITEETH_MIN_FRAME       (60U)       /* without FCS; shorter frames are padded */
#define LITEETH_SLOT_SIZE       (0x800U)

typedef enum
{
    LITEETH_OK = 0,
    LITEETH_NONE,               /* nothing received */
    LITEETH_ERR_TOO_LONG,       /* frame larger than the slot / the buffer */
    LITEETH_ERR_TX_BUSY,        /* transmitter never became ready */
} liteeth_status_t;

typedef struct
{
    uint32_t tx;
    uint32_t rx;
    uint32_t rx_too_long;
    uint32_t tx_busy;
} liteeth_stats_t;

/* Releases every RX slot and enables the RX interrupt. */
void liteeth_init(void);

/* Sends one frame (destination MAC first, no FCS; the MAC appends it).
 * Frames shorter than 60 bytes are zero-padded. */
liteeth_status_t liteeth_send(const uint8_t *frame, uint32_t len);

/* Takes the oldest received frame, if any, and hands its slot back to the
 * MAC. *len is the length the MAC reports: it includes the 4-byte FCS, and
 * Renode 1.13's model adds (length % 60) more zero bytes, so take the real
 * length from the payload. */
liteeth_status_t liteeth_receive(uint8_t *buf, uint32_t size, uint32_t *len);

void liteeth_get_stats(liteeth_stats_t *stats);

#endif /* LITEETH_H_ */
