/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * M-mode trap vector after the drop to S-mode: only SBI ecalls and the
 * machine timer interrupt come here. Swaps to the M-mode stack held in
 * mscratch, saves the caller-saved registers and calls m_trap(frame), which
 * may change frame->a0/a1 (SBI return values) and mepc.
 * Frame layout: m_frame_t in m_shim.c.
 */

#define REGBYTES 8

    .section .text.m_trap_vector, "ax", %progbits
    .align 4
    .globl m_trap_vector
m_trap_vector:
    csrrw sp, mscratch, sp          /* sp = M-mode stack, mscratch = interrupted sp */
    addi  sp, sp, -(16*REGBYTES)
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

    mv a0, sp
    call m_trap

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
    addi  sp, sp, (16*REGBYTES)
    csrrw sp, mscratch, sp
    mret
