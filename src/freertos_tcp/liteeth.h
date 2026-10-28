/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Minimal driver for a LiteEth soft MAC in the fabric (in Renode: the
 * built-in LiteX_Ethernet, see renode/README.md). No DMA: frames are copied
 * through slot buffers in the MAC. Used by the FreeRTOS+TCP NetworkInterface
 * (NetworkInterface_LiteEth.c); nothing here depends on FreeRTOS.
 *
 *   CSRs     old LiteX layout: 8-bit subregisters, 4-byte stride,
 *            multi-byte values MSB first
 *   buffers  2 RX ("writer") slots, then 2 TX ("reader") slots, 0x800 bytes
 *            each, 32-bit accesses
 *   MDIO     bit-banged: W register (bit 0 MDC, bit 1 output enable,
 *            bit 2 MDIO out) at +0x4, R register (bit 0 MDIO in) at +0x8
 *   IRQ      level, high while a received frame is pending
 *
 * Every wait is bounded. Not thread-safe: the RX side (receive_*) and the TX
 * side (send) touch separate registers and may run in different tasks; the
 * MDIO functions must not run concurrently with each other.
 */

#ifndef LITEETH_H_
#define LITEETH_H_

#include <stdint.h>

#define LITEETH_MIN_FRAME       (60U)       /* without FCS; shorter frames are padded */
#define LITEETH_SLOT_SIZE       (0x800U)

typedef enum
{
    LITEETH_OK = 0,
    LITEETH_NONE,               /* nothing received */
    LITEETH_ERR_TOO_LONG,       /* frame larger than a slot */
    LITEETH_ERR_TX_BUSY,        /* transmitter never became ready */
} liteeth_status_t;

typedef struct
{
    uint32_t tx;
    uint32_t rx;
    uint32_t rx_dropped;        /* oversized, or released without a copy */
    uint32_t tx_busy;
} liteeth_stats_t;

/* Base addresses of the three register windows. Releases every RX slot and
 * enables the RX interrupt. */
void liteeth_init(uintptr_t csr_base, uintptr_t buf_base, uintptr_t mdio_base);

/* Sends one frame (destination MAC first, no FCS; the MAC appends it).
 * Frames shorter than 60 bytes are zero-padded. */
liteeth_status_t liteeth_send(const uint8_t *frame, uint32_t len);

/* Receiving is three steps, so the caller can size its buffer first:
 *   liteeth_receive_begin()  LITEETH_NONE, or the pending frame's length
 *   liteeth_receive_copy()   copy (part of) it
 *   liteeth_receive_end()    hand the slot back to the MAC (always call it
 *                            after a successful begin, copied or not)
 * The length is what the MAC reports: it includes the 4-byte FCS, and Renode
 * 1.13's model adds (length % 60) more zero bytes, so protocols must take
 * their length from the payload (IP, ARP and FreeRTOS+TCP do). */
liteeth_status_t liteeth_receive_begin(uint32_t *len);
void liteeth_receive_copy(uint8_t *buf, uint32_t len);
void liteeth_receive_end(uint8_t copied);

/* Clause 22 MDIO through the bit-banged interface. A read from an absent
 * PHY returns 0xFFFF on hardware (pulled-up MDIO) and 0 in Renode. */
uint16_t liteeth_mdio_read(uint8_t phy, uint8_t reg);
void liteeth_mdio_write(uint8_t phy, uint8_t reg, uint16_t value);

void liteeth_get_stats(liteeth_stats_t *stats);

#endif /* LITEETH_H_ */
