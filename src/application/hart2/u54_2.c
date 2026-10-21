/*******************************************************************************
 * Copyright 2019-2022 Microchip FPGA Embedded Systems Solutions.
 *
 * SPDX-License-Identifier: MIT
 *
 * Application code running on U54_2.
 *
 * Not used in this project: the hart is parked in WFI with all interrupts
 * disabled as soon as the HAL hands it to u54_2().
 */

#include "mpfs_hal/mss_hal.h"
#include "inc/hart_park.h"

void u54_2(void)
{
    park_this_hart();
}

/* hart2 Software interrupt handler (unused while parked). */
void Software_h2_IRQHandler(void)
{
}
