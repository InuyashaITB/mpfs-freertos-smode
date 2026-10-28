/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * S-mode entry (mret target of m_main()), entered at its physical address
 * with the MMU off. Turns on Sv39 with the application's root page table
 * (mpfs_sv39_root_table, required: the link fails without it), jumps to the
 * virtual alias of the image in the high half, then sets gp, the S-mode
 * stack and the FreeRTOS trap vector there and calls s_mode_main().
 *
 * The table must map L2LIM both 1:1 (the instructions right after the satp
 * write still run at the physical address) and at the alias (see mpfs.h).
 */

#include "mpfs.h"

    .section .text.s_start, "ax", %progbits
    .align 4
    .globl s_start
s_start:
    la      t0, s_park                  /* a fault before the FreeRTOS vector */
    csrw    stvec, t0                   /* (e.g. a table without the alias) parks */
    la      t0, mpfs_sv39_root_table    /* PC-relative: the table's PA */
    slli    t1, t0, 52                  /* low 12 bits */
    bnez    t1, s_park                  /* not 4 KiB aligned */
    srli    t0, t0, 12                  /* root PPN */
    li      t1, SATP_MODE_SV39
    or      t0, t0, t1
    sfence.vma
    csrw    satp, t0
    sfence.vma

    /* Continue at the same code in the high-half alias. */
    li      t1, MPFS_ONCHIP_VA_OFFSET
    la      t0, 1f
    add     t0, t0, t1
    jr      t0
1:
    /* From here every PC-relative address is virtual. */
    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop
    la      sp, __s_stack_top
    la      t0, freertos_risc_v_trap_handler
    csrw    stvec, t0
    call    s_mode_main                 /* starts the scheduler, does not return */

s_park:
    wfi
    j       s_park
