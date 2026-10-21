/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * 3 x 1 GiB buffers in the 38-bit cached DDR window. See ddr_buffers.h.
 *
 * Section names start with ".bss." so compilers emit them as NOBITS (no 1 GiB
 * of zeros in the object file). The linker script collects them into the
 * .ddr_cached_38bit output section in name order (0, 1, 2), i.e. at
 * ORIGIN(ddr_cached_38bit_va) + 0, + 1 GiB and + 2 GiB. The section is
 * NOLOAD: contents are undefined after reset, nothing clears 3 GiB at boot.
 *
 * Do not reference the arrays from code in other files (see the header).
 */

#include "ddr/ddr_buffers.h"

#define DDR_SECTION(n)  __attribute__((section(".bss.ddr_cached_38bit." #n), aligned(4096)))

char g_ddr_buffer_0[DDR_BUFFER_SIZE] DDR_SECTION(0);
char g_ddr_buffer_1[DDR_BUFFER_SIZE] DDR_SECTION(1);
char g_ddr_buffer_2[DDR_BUFFER_SIZE] DDR_SECTION(2);

/* Full 64-bit addresses, written by the linker (R_RISCV_64): no range limit. */
char * const g_ddr_buffer[DDR_BUFFER_COUNT] =
{
    g_ddr_buffer_0,
    g_ddr_buffer_1,
    g_ddr_buffer_2,
};
