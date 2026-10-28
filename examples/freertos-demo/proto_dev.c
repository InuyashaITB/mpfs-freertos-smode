/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Driver for the example-protocol SPI device. See proto_dev.h.
 */

#include <stddef.h>
#include <string.h>

#include "mpfs_hal/mss_hal.h"
#include "drivers/mss/mss_spi/mss_spi.h"
#include "spi_xfer.h"
#include "proto_dev.h"

#define REQUEST_START       (0xA5U)
#define RESPONSE_START      (0x5AU)

/* 1-byte reads before giving up on the 0x5A marker. */
#define MARKER_POLL_LIMIT   (1000U)

static mss_spi_instance_t *s_spi = NULL;
static uint8_t s_frame[4U + PROTO_MAX_PAYLOAD];

void proto_init(mss_spi_instance_t *spi, uint32_t clk_div)
{
    s_spi = spi;
    MSS_SPI_init(spi);
    MSS_SPI_configure_master_mode(spi, MSS_SPI_SLAVE_0, MSS_SPI_MODE0, clk_div, 8U, NULL);
}

uint8_t proto_crc8(const uint8_t *data, uint32_t len)
{
    uint8_t crc = 0U;

    for (uint32_t i = 0U; i < len; i++)
    {
        crc ^= data[i];
        for (uint32_t bit = 0U; bit < 8U; bit++)
        {
            crc = (0U != (crc & 0x80U)) ? (uint8_t)((crc << 1) ^ 0x07U) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

proto_result_t proto_transact(uint8_t cmd, const uint8_t *payload, uint8_t len,
                              proto_response_t *rsp, uint8_t corrupt_crc)
{
    uint8_t b = 0U;
    uint8_t hdr[2];
    uint8_t tail[PROTO_MAX_PAYLOAD + 1U];
    uint8_t crc_in[2U + PROTO_MAX_PAYLOAD];

    (void)memset(rsp, 0, sizeof(*rsp));
    if (len > PROTO_MAX_PAYLOAD)
    {
        return PROTO_ERR_BAD_RESPONSE;
    }

    /* Request: one transfer. */
    s_frame[0] = REQUEST_START;
    s_frame[1] = cmd;
    s_frame[2] = len;
    if (len > 0U)
    {
        (void)memcpy(&s_frame[3], payload, len);
    }
    s_frame[3U + len] = (uint8_t)(proto_crc8(&s_frame[1], 2U + len) ^ ((0U != corrupt_crc) ? 0x01U : 0x00U));
    if (SPI_XFER_OK != spi_xfer(s_spi, s_frame, 4U + len, NULL, 0U))
    {
        return PROTO_ERR_BUS;
    }

    /* Response: poll for the marker, one byte per transfer. */
    for (rsp->marker_polls = 0U; rsp->marker_polls < MARKER_POLL_LIMIT; rsp->marker_polls++)
    {
        if (SPI_XFER_OK != spi_xfer(s_spi, NULL, 0U, &b, 1U))
        {
            return PROTO_ERR_BUS;
        }
        if (RESPONSE_START == b)
        {
            break;
        }
    }
    if (RESPONSE_START != b)
    {
        return PROTO_ERR_NO_MARKER;
    }

    /* status | len, then payload | crc. */
    if (SPI_XFER_OK != spi_xfer(s_spi, NULL, 0U, hdr, 2U))
    {
        return PROTO_ERR_BUS;
    }
    if (hdr[1] > PROTO_MAX_PAYLOAD)
    {
        return PROTO_ERR_BAD_RESPONSE;
    }
    if (SPI_XFER_OK != spi_xfer(s_spi, NULL, 0U, tail, hdr[1] + 1U))
    {
        return PROTO_ERR_BUS;
    }
    crc_in[0] = hdr[0];
    crc_in[1] = hdr[1];
    (void)memcpy(&crc_in[2], tail, hdr[1]);
    if (proto_crc8(crc_in, 2U + hdr[1]) != tail[hdr[1]])
    {
        return PROTO_ERR_BAD_RESPONSE;
    }

    rsp->status = hdr[0];
    rsp->len = hdr[1];
    (void)memcpy(rsp->payload, tail, hdr[1]);
    return (PROTO_STATUS_OK == rsp->status) ? PROTO_OK : PROTO_NAK;
}

proto_result_t proto_ping(proto_response_t *rsp)
{
    return proto_transact(PROTO_CMD_PING, NULL, 0U, rsp, 0U);
}

proto_result_t proto_read_reg(uint8_t addr, uint8_t *value, proto_response_t *rsp)
{
    proto_result_t r = proto_transact(PROTO_CMD_READ_REG, &addr, 1U, rsp, 0U);

    if ((PROTO_OK == r) && (1U == rsp->len))
    {
        *value = rsp->payload[0];
    }
    else if (PROTO_OK == r)
    {
        r = PROTO_ERR_BAD_RESPONSE;
    }
    return r;
}

proto_result_t proto_write_reg(uint8_t addr, uint8_t value, proto_response_t *rsp)
{
    const uint8_t p[2] = { addr, value };

    return proto_transact(PROTO_CMD_WRITE_REG, p, 2U, rsp, 0U);
}

proto_result_t proto_get_events(proto_response_t *rsp)
{
    return proto_transact(PROTO_CMD_GET_EVENTS, NULL, 0U, rsp, 0U);
}

const char *proto_result_str(proto_result_t r)
{
    switch (r)
    {
        case PROTO_OK:                  return "ok";
        case PROTO_NAK:                 return "NAK";
        case PROTO_ERR_BUS:             return "no response on the bus";
        case PROTO_ERR_NO_MARKER:       return "no response marker (0x5A)";
        case PROTO_ERR_BAD_RESPONSE:    return "bad response (length or CRC)";
        default:                        return "?";
    }
}
