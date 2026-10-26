/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Sv39 page tables for the S-mode kernel: gigapage (1 GiB) mappings only.
 *
 * Mappings (built from the linker script, see mpfs-lim-sv39.ld):
 *   identity   VA 0x0         -> PA 0x0             1 GiB  RWX  peripherals, switch-over
 *   on-chip    l2lim_va & ~1G -> l2lim_pa & ~1G     1 GiB  RWX  code/data (high alias)
 *   DDR        ddr_va         -> ddr_pa             N GiB  RW
 */

#ifndef SMODE_SV39_H_
#define SMODE_SV39_H_

#include <stdint.h>

#define SV39_GIGAPAGE           (1UL << 30)
#define SV39_SATP_MODE          (8UL << 60)

/* PTE bits */
#define SV39_PTE_V              (1UL << 0)
#define SV39_PTE_R              (1UL << 1)
#define SV39_PTE_W              (1UL << 2)
#define SV39_PTE_X              (1UL << 3)
#define SV39_PTE_U              (1UL << 4)
#define SV39_PTE_G              (1UL << 5)
#define SV39_PTE_A              (1UL << 6)
#define SV39_PTE_D              (1UL << 7)

/* Layout taken from the linker script. */
typedef struct
{
    uintptr_t l2lim_va;         /* ORIGIN(l2lim_va) */
    uintptr_t l2lim_pa;         /* ORIGIN(l2lim_pa) */
    uintptr_t ddr_va;           /* ORIGIN(ddr_cached_38bit_va) */
    uintptr_t ddr_pa;           /* ORIGIN(ddr_cached_38bit_pa) */
    uintptr_t ddr_size;         /* LENGTH(ddr_cached_38bit_va) */
} sv39_layout_t;

/* Returned in a0/a1 to smode_start.S. */
typedef struct
{
    uintptr_t satp;             /* value to write to satp */
    uintptr_t onchip_va_offset; /* VA - PA of the on-chip alias */
} sv39_enable_t;

/* Build the root table. Called from smode_start.S in S-mode with the MMU off
 * (executing from the physical copy). */
sv39_enable_t sv39_build(void);

/* Copy of the layout (valid in any mode). */
void sv39_get_layout(sv39_layout_t *layout);

/* Root page table, for inspection. */
extern uint64_t g_sv39_root[512];

#endif /* SMODE_SV39_H_ */
