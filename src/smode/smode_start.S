/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * S-mode entry: turn the Sv39 MMU on and continue at the virtual (high) alias.
 *
 * m_shim_enter_smode() mret's here with:
 *   - S-mode, satp = 0 (MMU off), SIE = 0
 *   - pc and sp physical (the code runs from the physical copy of L2LIM)
 *   - gp = physical alias of __global_pointer$ (set up by the HAL in M-mode)
 *
 * Steps:
 *   1. stvec -> smode_early_trap, so a fault during the switch is reported.
 *   2. sv39_build() fills the root table, returns satp and VA-PA offset.
 *   3. Write satp. The next instruction is still fetched from its physical
 *      address, which the identity mapping of gigapage 0 covers.
 *   4. Move sp, pc and gp to the high alias.
 *   5. smode_os_init(): OS hook (libfreertos_smode installs its trap handler;
 *      without an OS lib the weak default keeps smode_early_trap).
 *   6. s_mode_main(), the application (never returns).
 */

    .section .text.smode_start, "ax", %progbits
    .align 4
    .globl smode_start
smode_start:
    la      t0, smode_early_trap
    csrw    stvec, t0

    call    sv39_build              /* a0 = satp, a1 = onchip VA - PA */

    csrw    satp, a0
    sfence.vma

    add     sp, sp, a1              /* stack -> virtual alias */
    la      t0, 1f
    add     t0, t0, a1
    jr      t0                      /* pc -> virtual alias */
1:
    .option push
    .option norelax
    la      gp, __global_pointer$   /* gp -> virtual alias (pc-relative, now at VA) */
    .option pop

    la      t0, smode_early_trap    /* re-point stvec at the virtual address */
    csrw    stvec, t0

    call    smode_os_init
    call    s_mode_main
2:
    wfi
    j       2b

/*
 * Trap handler used until FreeRTOS installs its own: report and park.
 * Position independent, so it works before and after the switch.
 */
    .align 4
    .globl smode_early_trap
smode_early_trap:
    csrr    a0, scause
    csrr    a1, sepc
    csrr    a2, stval
    call    smode_early_trap_report
3:
    wfi
    j       3b
