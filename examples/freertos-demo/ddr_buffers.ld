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

/* The FreeRTOS heap (configAPPLICATION_ALLOCATED_HEAP = 1 in FreeRTOSConfig.h:
 * heap_4 uses the application's ucHeap[configTOTAL_HEAP_SIZE]). It lives in
 * buf0 from 16 MiB on, where nothing else in the demo goes (buf0 users: the
 * ddr task at offsets 0, 512 MiB and the last qword; grid A at 256 MiB). Like
 * the buffers it is a plain symbol, and it is within +/-2 GiB of the code, so
 * heap_4's PC-relative references to it link. */
DDR_HEAP_OFFSET = 0x01000000;       /* 16 MiB into buf0 */
DDR_HEAP_SIZE   = 0x00400000;       /* = configTOTAL_HEAP_SIZE */
ucHeap          = g_ddr_buffer_0 + DDR_HEAP_OFFSET;

ASSERT(DDR_HEAP_OFFSET + DDR_HEAP_SIZE <= 0x10000000,
       "FreeRTOS heap overlaps the Life grid at buf0 + 256 MiB")
