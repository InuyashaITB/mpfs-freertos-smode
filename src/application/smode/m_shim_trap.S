/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * M-mode trap vector used by a hart after it has been handed over to S-mode
 * (installed in mtvec by m_shim_enter_smode()).
 *
 * After the handoff, M-mode is only entered by traps from S-mode: SBI ecalls
 * and the machine timer interrupt. The HAL's trap_vector cannot be used for
 * this because it pushes onto whatever sp is current (the S-mode task stack)
 * and does not handle ecall from S-mode. This vector swaps to a private M-mode
 * stack held in mscratch, saves the caller-saved registers and calls
 * m_shim_trap_handler(frame). The handler may modify frame->a0/a1 (SBI return
 * values) and mepc.
 *
 * Frame layout must match m_shim_frame_t in m_shim.h.
 */

#define REGBYTES 8

    .section .text.m_shim_trap_vector, "ax", %progbits
    .align 4
    .globl m_shim_trap_vector
m_shim_trap_vector:
    csrrw sp, mscratch, sp          /* sp = M-mode stack, mscratch = interrupted sp */
    addi  sp, sp, -(18*REGBYTES)    /* 18 slots keeps 16-byte alignment */

    sd ra,  0*REGBYTES(sp)
    sd t0,  1*REGBYTES(sp)
    sd t1,  2*REGBYTES(sp)
    sd t2,  3*REGBYTES(sp)
    sd a0,  4*REGBYTES(sp)
    sd a1,  5*REGBYTES(sp)
    sd a2,  6*REGBYTES(sp)
    sd a3,  7*REGBYTES(sp)
    sd a4,  8*REGBYTES(sp)
    sd a5,  9*REGBYTES(sp)
    sd a6, 10*REGBYTES(sp)
    sd a7, 11*REGBYTES(sp)
    sd t3, 12*REGBYTES(sp)
    sd t4, 13*REGBYTES(sp)
    sd t5, 14*REGBYTES(sp)
    sd t6, 15*REGBYTES(sp)
    sd gp, 16*REGBYTES(sp)

    /* S-mode may run with gp pointing at the virtual alias of
     * __global_pointer$, but M-mode runs from the physical copy with the MMU
     * off: load M-mode's own gp (PC-relative, so the physical alias here). */
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

    mv a0, sp                       /* a0 = m_shim_frame_t* */
    call m_shim_trap_handler

    ld ra,  0*REGBYTES(sp)
    ld t0,  1*REGBYTES(sp)
    ld t1,  2*REGBYTES(sp)
    ld t2,  3*REGBYTES(sp)
    ld a0,  4*REGBYTES(sp)
    ld a1,  5*REGBYTES(sp)
    ld a2,  6*REGBYTES(sp)
    ld a3,  7*REGBYTES(sp)
    ld a4,  8*REGBYTES(sp)
    ld a5,  9*REGBYTES(sp)
    ld a6, 10*REGBYTES(sp)
    ld a7, 11*REGBYTES(sp)
    ld t3, 12*REGBYTES(sp)
    ld t4, 13*REGBYTES(sp)
    ld t5, 14*REGBYTES(sp)
    ld t6, 15*REGBYTES(sp)
    ld gp, 16*REGBYTES(sp)

    addi  sp, sp, (18*REGBYTES)
    csrrw sp, mscratch, sp          /* restore interrupted sp, mscratch = M-mode stack again */
    mret
