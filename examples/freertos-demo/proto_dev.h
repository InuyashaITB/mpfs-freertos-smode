/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Driver for the example-protocol SPI device (renode/models/ProtoSpiDevice.cs
 * on SPI1; protocol in renode/README.md):
 *   request   0xA5 | cmd | len | payload | crc8      (one transfer)
 *   response  poll for 0x5A, then status | len | payload | crc8
 * CRC-8: polynomial 0x07, init 0x00.
 *
 * Polled, every wait bounded (spi_xfer.c). Not thread-safe: one task at a
 * time, or before the scheduler starts.
 */

#ifndef PROTO_DEV_H_
#define PROTO_DEV_H_

#include <stdint.h>
#include "drivers/mss/mss_spi/mss_spi.h"

#define PROTO_MAX_PAYLOAD       (32U)

#define PROTO_CMD_PING          (0x01U)
#define PROTO_CMD_ECHO          (0x02U)
#define PROTO_CMD_READ_REG      (0x03U)
#define PROTO_CMD_WRITE_REG     (0x04U)
#define PROTO_CMD_GET_EVENTS    (0x05U)

/* Response status codes. */
#define PROTO_STATUS_OK             (0x00U)
#define PROTO_STATUS_BAD_CRC        (0x81U)
#define PROTO_STATUS_UNKNOWN_CMD    (0x82U)
#define PROTO_STATUS_BAD_LEN        (0x83U)
#define PROTO_STATUS_BAD_ADDR       (0x84U)
#define PROTO_STATUS_READ_ONLY      (0x85U)

typedef enum
{
    PROTO_OK = 0,
    PROTO_NAK,                  /* the device answered with a NAK status */
    PROTO_ERR_BUS,              /* nothing received from the controller */
    PROTO_ERR_NO_MARKER,        /* no 0x5A within the poll limit */
    PROTO_ERR_BAD_RESPONSE,     /* length too large or CRC mismatch */
} proto_result_t;

typedef struct
{
    uint8_t status;                     /* PROTO_STATUS_... */
    uint8_t len;
    uint8_t payload[PROTO_MAX_PAYLOAD];
    uint32_t marker_polls;              /* bytes read before 0x5A */
} proto_response_t;

void proto_init(mss_spi_instance_t *spi, uint32_t clk_div);

uint8_t proto_crc8(const uint8_t *data, uint32_t len);

/* One request and its response. Returns PROTO_NAK (and fills rsp) if the
 * status is not OK. corrupt_crc sends a wrong CRC, for testing. */
proto_result_t proto_transact(uint8_t cmd, const uint8_t *payload, uint8_t len,
                              proto_response_t *rsp, uint8_t corrupt_crc);

proto_result_t proto_ping(proto_response_t *rsp);
proto_result_t proto_read_reg(uint8_t addr, uint8_t *value, proto_response_t *rsp);
proto_result_t proto_write_reg(uint8_t addr, uint8_t value, proto_response_t *rsp);
/* Up to PROTO_MAX_PAYLOAD queued event bytes in rsp->payload / rsp->len. */
proto_result_t proto_get_events(proto_response_t *rsp);

const char *proto_result_str(proto_result_t r);

#endif /* PROTO_DEV_H_ */
