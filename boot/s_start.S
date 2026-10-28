/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * S-mode entry (mret target of m_main()). Turns on Sv39 with the
 * application's root page table (mpfs_sv39_root_table, required: the link
 * fails without it), sets the S-mode stack and the FreeRTOS trap vector, and
 * calls the application's s_mode_main(). The image is linked at its physical
 * addresses, so the table must map it 1:1 (see mpfs.h); execution simply
 * continues at the same addresses once satp is written.
 */

#include "mpfs.h"

    .section .text.s_start, "ax", %progbits
    .align 4
    .globl s_start
s_start:
    la      t0, mpfs_sv39_root_table
    slli    t1, t0, 52                  /* low 12 bits */
    bnez    t1, s_park                  /* not 4 KiB aligned */
    srli    t0, t0, 12                  /* root PPN */
    li      t1, SATP_MODE_SV39
    or      t0, t0, t1
    sfence.vma
    csrw    satp, t0
    sfence.vma

    la      sp, __s_stack_top
    la      t0, freertos_risc_v_trap_handler
    csrw    stvec, t0
    call    s_mode_main                 /* starts the scheduler, does not return */

s_park:
    wfi
    j       s_park
