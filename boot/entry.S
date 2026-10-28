/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Image entry, M-mode. The bootloader starts U54_1 here (a0 = hart id) with
 * the image already loaded at its physical addresses; any other hart that
 * arrives is parked. Sets up gp, the FPU, the M-mode stack and .bss, then
 * calls m_main() (m_shim.c), which drops to S-mode.
 */

#include "mpfs.h"

    .section .text.entry, "ax", %progbits
    .globl _start
_start:
    csrw    mie, zero
    csrci   mstatus, MSTATUS_MIE
    csrr    t0, mhartid
    li      t1, MPFS_SMODE_HART
    bne     t0, t1, park

    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop

    li      t0, MSTATUS_FS_INITIAL      /* FPU on, for M- and S-mode */
    csrs    mstatus, t0
    fscsr   zero

    la      sp, __m_stack_top

    la      t0, __bss_start
    la      t1, __bss_end
1:  bgeu    t0, t1, 2f
    sd      zero, 0(t0)
    addi    t0, t0, 8
    j       1b
2:
    call    m_main

park:
    wfi
    j       park
