/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * 3 x 1 GiB buffers in the 38-bit cached DDR window. See ddr_buffers.h.
 *
 * The buffers are not C objects: the linker script (mpfs-lim-sv39.ld) defines
 * g_ddr_buffer_0..2 as absolute symbols at ORIGIN(ddr_cached_38bit_va) + 0,
 * + 1 GiB and + 2 GiB. No section, so the ELF has no 3 GiB load segment and
 * 'size' does not count them. Contents are undefined after reset; nothing
 * clears 3 GiB at boot.
 *
 * Do not reference these symbols from code in other files (see the header).
 */

#include "ddr_buffers.h"

extern char g_ddr_buffer_0[];
extern char g_ddr_buffer_1[];
extern char g_ddr_buffer_2[];

/* Full 64-bit addresses, written by the linker (R_RISCV_64): no range limit. */
char * const g_ddr_buffer[DDR_BUFFER_COUNT] =
{
    g_ddr_buffer_0,
    g_ddr_buffer_1,
    g_ddr_buffer_2,
};
