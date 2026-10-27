/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Minimal I2C EEPROM driver (AT24C256-style: 2-byte memory address, 64-byte
 * pages, write cycle signalled by NACKing the device address) on the vendor
 * MSS_I2C driver as master. Used against renode/models/I2cEeprom.cs on I2C0.
 *
 * MSS_I2C is interrupt-driven, so these calls need the scheduler running and
 * the controller's PLIC source enabled for the S context (eeprom_init() does
 * that). They wait with vTaskDelay() and every wait is bounded. Not
 * thread-safe: one task at a time.
 */

#ifndef I2C_EEPROM_H_
#define I2C_EEPROM_H_

#include <stdint.h>
#include "mpfs_hal/mss_hal.h"
#include "drivers/mss/mss_i2c/mss_i2c.h"

#define EEPROM_PAGE_SIZE    (64U)

typedef enum
{
    EEPROM_OK = 0,
    EEPROM_ERR_NACK,        /* no device answered (or it stayed busy) */
    EEPROM_ERR_TIMEOUT,     /* the transaction did not complete */
} eeprom_status_t;

/* MSS_I2C_init() as master (own address unused), PLIC priority and S-context
 * enable for the controller's interrupt. dev_addr: 7-bit device address. */
void eeprom_init(mss_i2c_instance_t *i2c, PLIC_IRQn_Type irq, uint8_t dev_addr);

/* Random read: 2-byte address, then len bytes (repeated START). */
eeprom_status_t eeprom_read(uint16_t addr, uint8_t *buf, uint16_t len);

/* Current-address read: continues where the last access ended. */
eeprom_status_t eeprom_read_current(uint8_t *buf, uint16_t len);

/* Writes len bytes, split at page boundaries. After each page it ACK-polls
 * (empty writes) until the write cycle is over. */
eeprom_status_t eeprom_write(uint16_t addr, const uint8_t *data, uint16_t len);

/* One write transaction exactly as given, with no page split: data past the
 * end of the page wraps to its start on a real part. Waits for the write
 * cycle. For demonstrating the wrap. */
eeprom_status_t eeprom_write_raw(uint16_t addr, const uint8_t *data, uint16_t len);

/* Address phases NACKed while ACK-polling during the last write call. */
uint32_t eeprom_last_busy_polls(void);

/* Any one-off transfer to another address on the same bus (e.g. to check
 * that an absent device is reported). Returns the MSS_I2C status. */
mss_i2c_status_t eeprom_probe(uint8_t dev_addr);

#endif /* I2C_EEPROM_H_ */
