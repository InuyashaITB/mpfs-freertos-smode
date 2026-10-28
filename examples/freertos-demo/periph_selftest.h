/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Self-tests for the simulated peripherals (branch sim-peripherals). The SPI
 * ones run from s_mode_main() in S-mode before the scheduler starts (polled,
 * no interrupts); the I2C one needs interrupts and runs in a task. Every wait
 * is bounded, so a missing or misbehaving simulated device shows up as a
 * failure instead of hanging the boot.
 */

#ifndef PERIPH_SELFTEST_H_
#define PERIPH_SELFTEST_H_

#include <stdint.h>

/* printf-style output for the self-tests that report step by step. */
typedef void (*selftest_print_t)(const char *fmt, ...);

/* Phase 4: the example-protocol device on MSS SPI1 against renode/models/
 * ProtoSpiDevice.cs, through proto_dev.c: PING, ECHO, WRITE_REG/READ_REG,
 * the NAK cases (bad address, read-only register, bad CRC, unknown command)
 * and GET_EVENTS with nothing queued. Prints PASS/FAIL per step and returns
 * the number of failed steps (or 1 if the device did not answer). */
uint32_t spi_proto_selftest(selftest_print_t print);

/* Phase 2: SPI NOR flash on MSS SPI0 against renode/models/NorFlash.cs,
 * through spi_flash.c. Reads the JEDEC ID, then on the sector at
 * SPI_FLASH_TEST_ADDR: erase -> all 0xFF; program 0xA5 -> 0xA5; program 0x5A
 * over it -> 0x00 (programming only clears bits); a write that crosses a
 * page boundary; erase -> 0xFF again. Prints PASS/FAIL per step and returns
 * the number of failed steps (or 1 if the device did not answer). */
#define SPI_FLASH_TEST_ADDR     (0x00010000U)
uint32_t spi_flash_selftest(selftest_print_t print);

/* Phase 3: I2C EEPROM at 0x50 on MSS I2C0 against renode/models/
 * I2cEeprom.cs (behind renode/models/MpfsI2c.cs), through i2c_eeprom.c and
 * the interrupt-driven MSS_I2C driver. Needs the scheduler running: call it
 * from a task. Steps: one raw write across a page boundary (wraps to the
 * start of the page); a split write over three pages with ACK polling;
 * random read then current-address read; an absent device (0x51) is
 * reported as a NACK. Prints PASS/FAIL per step and returns the number of
 * failed steps (or 1 if the device did not answer). */
#define EEPROM_I2C_ADDR         (0x50U)
#define EEPROM_ABSENT_ADDR      (0x51U)
uint32_t i2c_eeprom_selftest(selftest_print_t print);

/* Phase 5a: raw Ethernet frames between two boards through the LiteEth soft
 * MAC (liteeth.c). Symmetric: every board broadcasts HELLO frames
 * (EtherType 0x88B5) and answers each HELLO from a peer with a unicast ACK.
 * The board id comes from the GPIO2_0 strap, the MAC is 02:4d:50:46:53:<id>.
 * Non-blocking: start it once, then call poll often (and when the MAC's
 * interrupt fires) with the current time in ms. Prints PASS once a HELLO and
 * an ACK came from a peer, "no peer answered" after ETH_PEER_TIMEOUT_MS
 * (a single board), FAIL on a malformed frame or a peer with our id. */
#define ETH_PEER_TIMEOUT_MS     (3000U)
void eth_raw_test_start(selftest_print_t print, uint32_t now_ms);
void eth_raw_test_poll(selftest_print_t print, uint32_t now_ms);

#endif /* PERIPH_SELFTEST_H_ */
