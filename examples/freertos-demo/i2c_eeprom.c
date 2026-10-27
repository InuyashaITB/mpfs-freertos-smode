/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Minimal I2C EEPROM driver. See i2c_eeprom.h.
 */

#include <stddef.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "mpfs_hal/mss_hal.h"
#include "drivers/mss/mss_i2c/mss_i2c.h"
#include "smode/smode_plic.h"
#include "i2c_eeprom.h"

/* Ticks to wait for one transaction (a page write at 100 kHz is < 10 ms). */
#define XFER_TIMEOUT_TICKS  (pdMS_TO_TICKS(100U))

/* ACK polls before a write cycle counts as stuck (real parts: 5 ms). */
#define ACK_POLL_LIMIT      (100U)

/* Own address on the bus: required by MSS_I2C_init(), unused as master. */
#define OWN_ADDRESS         (0x10U)

static mss_i2c_instance_t *s_i2c = NULL;
static uint8_t s_dev = 0U;
static uint8_t s_buf[2U + EEPROM_PAGE_SIZE];
static uint32_t s_last_busy_polls = 0U;

/* Waits for the transaction the driver is running. MSS_I2C_wait_complete()
 * spins forever unless MSS_I2C_system_tick() is called, so poll the status
 * with a bounded number of ticks instead. */
static mss_i2c_status_t wait_done(void)
{
    mss_i2c_status_t st = MSS_I2C_get_status(s_i2c);

    for (TickType_t t = 0U; (MSS_I2C_IN_PROGRESS == st) && (t < XFER_TIMEOUT_TICKS); t++)
    {
        vTaskDelay(1U);
        st = MSS_I2C_get_status(s_i2c);
    }
    return st;
}

static eeprom_status_t to_status(mss_i2c_status_t st)
{
    switch (st)
    {
        case MSS_I2C_SUCCESS:   return EEPROM_OK;
        case MSS_I2C_FAILED:    return EEPROM_ERR_NACK;
        default:                return EEPROM_ERR_TIMEOUT;
    }
}

/* ACK polling: an empty write is ACKed once the write cycle is over. */
static eeprom_status_t wait_write_cycle(void)
{
    for (uint32_t n = 0U; n < ACK_POLL_LIMIT; n++)
    {
        MSS_I2C_write(s_i2c, s_dev, NULL, 0U, MSS_I2C_RELEASE_BUS);
        mss_i2c_status_t st = wait_done();

        if (MSS_I2C_SUCCESS == st)
        {
            return EEPROM_OK;
        }
        if (MSS_I2C_FAILED != st)
        {
            return EEPROM_ERR_TIMEOUT;
        }
        s_last_busy_polls++;
    }
    return EEPROM_ERR_NACK;
}

static eeprom_status_t write_transaction(uint16_t addr, const uint8_t *data, uint16_t len)
{
    eeprom_status_t st;

    s_buf[0] = (uint8_t)(addr >> 8);
    s_buf[1] = (uint8_t)addr;
    (void)memcpy(&s_buf[2], data, len);
    MSS_I2C_write(s_i2c, s_dev, s_buf, (uint16_t)(2U + len), MSS_I2C_RELEASE_BUS);
    st = to_status(wait_done());
    if (EEPROM_OK == st)
    {
        st = wait_write_cycle();
    }
    return st;
}

void eeprom_init(mss_i2c_instance_t *i2c, PLIC_IRQn_Type irq, uint8_t dev_addr)
{
    s_i2c = i2c;
    s_dev = dev_addr;
    MSS_I2C_init(i2c, OWN_ADDRESS, MSS_I2C_PCLK_DIV_256);
    PLIC_SetPriority(irq, 2U);
    SPLIC_enable(irq);
}

eeprom_status_t eeprom_read(uint16_t addr, uint8_t *buf, uint16_t len)
{
    uint8_t offset[2] = { (uint8_t)(addr >> 8), (uint8_t)addr };

    MSS_I2C_write_read(s_i2c, s_dev, offset, 2U, buf, len, MSS_I2C_RELEASE_BUS);
    return to_status(wait_done());
}

eeprom_status_t eeprom_read_current(uint8_t *buf, uint16_t len)
{
    MSS_I2C_read(s_i2c, s_dev, buf, len, MSS_I2C_RELEASE_BUS);
    return to_status(wait_done());
}

eeprom_status_t eeprom_write(uint16_t addr, const uint8_t *data, uint16_t len)
{
    eeprom_status_t st = EEPROM_OK;

    s_last_busy_polls = 0U;
    while ((len > 0U) && (EEPROM_OK == st))
    {
        uint16_t room = (uint16_t)(EEPROM_PAGE_SIZE - (addr % EEPROM_PAGE_SIZE));
        uint16_t n = (len < room) ? len : room;

        st = write_transaction(addr, data, n);
        addr = (uint16_t)(addr + n);
        data += n;
        len = (uint16_t)(len - n);
    }
    return st;
}

eeprom_status_t eeprom_write_raw(uint16_t addr, const uint8_t *data, uint16_t len)
{
    s_last_busy_polls = 0U;
    if (len > EEPROM_PAGE_SIZE)
    {
        return EEPROM_ERR_NACK;
    }
    return write_transaction(addr, data, len);
}

uint32_t eeprom_last_busy_polls(void)
{
    return s_last_busy_polls;
}

mss_i2c_status_t eeprom_probe(uint8_t dev_addr)
{
    uint8_t offset[2] = { 0U, 0U };

    MSS_I2C_write(s_i2c, dev_addr, offset, 2U, MSS_I2C_RELEASE_BUS);
    return wait_done();
}
