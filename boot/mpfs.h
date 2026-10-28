/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * The few PolarFire SoC and RISC-V definitions the boot code needs, in place
 * of the MPFS HAL. Usable from C and assembler.
 *
 * Boot contract (see README.md, "bare-bones branch"):
 *   - A bootloader (e.g. an HSS bare-metal payload, no OpenSBI) loads the ELF
 *     at its physical addresses and starts U54_1 in M-mode at _start with
 *     a0 = hart id. Clocks, L2 as LIM, and PMP entry 0 (unlocked) are its job.
 *   - The application provides the Sv39 root page table, mpfs_sv39_root_table:
 *     512 entries, 4 KiB aligned. The image is linked at its physical
 *     addresses, so the table must map the image and the CLINT mtime
 *     register 1:1 (VA = PA), readable/writable/executable by S-mode.
 */

#ifndef MPFS_H_
#define MPFS_H_

#ifdef __ASSEMBLER__
#define _UL(x)  x
#else
#define _UL(x)  x##UL
#endif

/* The hart that runs S-mode and FreeRTOS. */
#define MPFS_SMODE_HART         1

/* CLINT. */
#define CLINT_MTIMECMP_BASE     _UL(0x02004000)     /* + 8 * hart */
#define CLINT_MTIME             _UL(0x0200BFF8)

/* CSR bits. */
#define MSTATUS_SIE             (_UL(1) << 1)
#define MSTATUS_MIE             (_UL(1) << 3)
#define MSTATUS_SPIE            (_UL(1) << 5)
#define MSTATUS_MPIE            (_UL(1) << 7)
#define MSTATUS_SPP             (_UL(1) << 8)
#define MSTATUS_MPP             (_UL(3) << 11)
#define MSTATUS_MPP_S           (_UL(1) << 11)
#define MSTATUS_FS_INITIAL      (_UL(1) << 13)
#define MSTATUS_MPRV            (_UL(1) << 17)
#define MSTATUS_SUM             (_UL(1) << 18)
#define MSTATUS_TVM             (_UL(1) << 20)
#define MSTATUS_TW              (_UL(1) << 21)
#define MSTATUS_TSR             (_UL(1) << 22)

#define MIP_SSIP                (_UL(1) << 1)
#define MIP_STIP                (_UL(1) << 5)
#define MIP_MTIP                (_UL(1) << 7)
#define MIP_SEIP                (_UL(1) << 9)

#define MCAUSE_INT              (_UL(1) << 63)
#define MCAUSE_M_TIMER          _UL(7)
#define MCAUSE_S_ECALL          _UL(9)

/* Delegated to S-mode: S software/timer/external interrupts, and the
 * exceptions S-mode handles itself (not breakpoints: they stay with the
 * debugger, not ecall from S: that is the SBI). */
#define MIDELEG_S               (MIP_SSIP | MIP_STIP | MIP_SEIP)
#define MEDELEG_S               ((_UL(1) << 0) | (_UL(1) << 1) | (_UL(1) << 2) | (_UL(1) << 4) | \
                                 (_UL(1) << 5) | (_UL(1) << 6) | (_UL(1) << 7) | (_UL(1) << 8) | \
                                 (_UL(1) << 12) | (_UL(1) << 13) | (_UL(1) << 15))

/* PMP: one NAPOT entry over the whole address space, R | W | X. */
#define PMPCFG_NAPOT_RWX        _UL(0x1F)

/* Sv39: satp mode, PTE bits. A leaf at the root level maps a 1 GiB gigapage;
 * its PPN is (PA >> 12) in bits 53:10 (PA must be 1 GiB aligned). */
#define SATP_MODE_SV39          (_UL(8) << 60)
#define PTE_V                   (_UL(1) << 0)
#define PTE_R                   (_UL(1) << 1)
#define PTE_W                   (_UL(1) << 2)
#define PTE_X                   (_UL(1) << 3)
#define PTE_G                   (_UL(1) << 5)
#define PTE_A                   (_UL(1) << 6)
#define PTE_D                   (_UL(1) << 7)
#define PTE_GIGAPAGE(pa, flags) ((((pa) >> 12) << 10) | (flags))

/* SBI (v0.2 calling convention: a7 = extension, a6 = function). */
#define SBI_EXT_LEGACY_SET_TIMER    _UL(0x00)
#define SBI_EXT_BASE                _UL(0x10)
#define SBI_EXT_TIME                _UL(0x54494D45)
#define SBI_EXT_IPI                 _UL(0x735049)
#define SBI_SUCCESS                 0L
#define SBI_ERR_NOT_SUPPORTED       (-2L)

#ifndef __ASSEMBLER__
#include <stdint.h>

#define csr_read(csr)          ({ uintptr_t v_; __asm volatile ("csrr %0, " #csr : "=r"(v_)); v_; })
#define csr_write(csr, val)    __asm volatile ("csrw " #csr ", %0" :: "r"((uintptr_t)(val)) : "memory")
#define csr_set(csr, bits)     __asm volatile ("csrs " #csr ", %0" :: "r"((uintptr_t)(bits)) : "memory")
#define csr_clear(csr, bits)   __asm volatile ("csrc " #csr ", %0" :: "r"((uintptr_t)(bits)) : "memory")

/* Provided by the application. */
extern const uint64_t mpfs_sv39_root_table[512];
void s_mode_main(void);
#endif

#endif /* MPFS_H_ */
