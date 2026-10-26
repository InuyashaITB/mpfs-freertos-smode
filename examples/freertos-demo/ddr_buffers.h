/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Three 1 GiB buffers in the 38-bit cached DDR window, mapped by the Sv39 MMU
 * (symbols g_ddr_buffer_0..2 defined in mpfs-lim-sv39.ld). Only usable in
 * S-mode after the MMU is on.
 *
 * Access the buffers through g_ddr_buffer[], not by their symbol names.
 * With -mcmodel=medany every symbol reference from code is PC-relative and
 * must be within +/-2 GiB of the instruction. The DDR window starts 896 MiB
 * above the code, so the buffers start 0.875, 1.875 and 2.875 GiB away from
 * it; the third is out of range and naming it in code fails to link with
 * "relocation truncated to fit: R_RISCV_PCREL_HI20". The pointer table holds
 * full 64-bit addresses (filled by the linker), so it works for all three.
 * The symbols are global so they show up in the map file and debugger.
 */

#ifndef DDR_BUFFERS_H_
#define DDR_BUFFERS_H_

#include <stdint.h>

#define DDR_BUFFER_COUNT    (3U)
#define DDR_BUFFER_SIZE     (1UL << 30)     /* 1 GiB each, = DDR_BUFFER_SIZE in the .ld */

/* Virtual addresses of the buffers (defined in ddr_buffers.c). */
extern char * const g_ddr_buffer[DDR_BUFFER_COUNT];

#endif /* DDR_BUFFERS_H_ */
