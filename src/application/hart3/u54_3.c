/*******************************************************************************
 * Copyright 2019-2022 Microchip FPGA Embedded Systems Solutions.
 *
 * SPDX-License-Identifier: MIT
 *
 * Application code running on U54_3.
 *
 * Not used in this project: the hart is parked in WFI with all interrupts
 * disabled as soon as the HAL hands it to u54_3().
 */

#include "mpfs_hal/mss_hal.h"
#include "inc/hart_park.h"

void u54_3(void)
{
    park_this_hart();
}

/* hart3 Software interrupt handler (unused while parked). */
void Software_h3_IRQHandler(void)
{
}
