/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * ddr_buffers.ld - the demo's layout of the DDR window
 *
 * An implicit linker script: it is passed as an input file on the link line
 * (see Makefile, APP_LDSCRIPTS), not with -T, so it adds to the library's
 * mpfs-lim-sv39.ld instead of replacing it. It may use the memory regions
 * defined there.
 *
 * The library maps the whole ddr_cached_38bit_va window (3 GiB at
 * 0xFFFFFFC0_40000000 -> PA 0x10_0000_0000). This demo splits it into three
 * 1 GiB buffers, used through g_ddr_buffer[] (ddr_buffers.c/.h).
 *
 * The buffers are plain symbols, not an output section. Even a NOLOAD
 * section is allocated: it gets a PT_LOAD program header with a 3 GiB MemSiz,
 * 'size' reports it as bss, and Renode 1.13's LoadELF cannot handle it. There
 * is nothing to load or clear here anyway; contents are undefined after
 * reset.
 ******************************************************************************/

DDR_BUFFER_SIZE = 0x40000000;       /* = DDR_BUFFER_SIZE in ddr_buffers.h */

g_ddr_buffer_0  = ORIGIN(ddr_cached_38bit_va) + 0 * DDR_BUFFER_SIZE;
g_ddr_buffer_1  = ORIGIN(ddr_cached_38bit_va) + 1 * DDR_BUFFER_SIZE;
g_ddr_buffer_2  = ORIGIN(ddr_cached_38bit_va) + 2 * DDR_BUFFER_SIZE;

ASSERT(3 * DDR_BUFFER_SIZE <= LENGTH(ddr_cached_38bit_va),
       "DDR buffers do not fit in ddr_cached_38bit_va")
