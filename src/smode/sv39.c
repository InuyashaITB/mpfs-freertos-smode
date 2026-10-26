/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Sv39 gigapage page tables for the S-mode kernel. See sv39.h.
 *
 * sv39_build() runs in S-mode with the MMU still off, i.e. from the physical
 * copy of the image. Taking an address in C (medany, PC-relative) therefore
 * yields a physical address here, which is what satp needs.
 *
 * The layout comes from the linker script. The values are read from a data
 * table filled in by the linker (absolute 64-bit relocations): referencing
 * the linker symbols directly from code would be PC-relative, which gives the
 * wrong answer for absolute symbols while running from the physical copy.
 * 'volatile' stops the compiler from folding the table back into such code.
 */

#include "smode/sv39.h"

extern char __l2lim_start[];               /* ORIGIN(l2lim_va) */
extern char __l2lim_pa_start[];            /* ORIGIN(l2lim_pa) */
extern char __ddr_cached_38bit_va_start[];
extern char __ddr_cached_38bit_pa_start[];
extern char __ddr_cached_38bit_size[];

static const volatile uintptr_t s_layout[5] =
{
    (uintptr_t)__l2lim_start,
    (uintptr_t)__l2lim_pa_start,
    (uintptr_t)__ddr_cached_38bit_va_start,
    (uintptr_t)__ddr_cached_38bit_pa_start,
    (uintptr_t)__ddr_cached_38bit_size,
};

/* Root table: 512 x 8 bytes, page aligned, in .bss (zeroed at boot). */
uint64_t g_sv39_root[512] __attribute__((aligned(4096)));

void sv39_get_layout(sv39_layout_t *layout)
{
    layout->l2lim_va = s_layout[0];
    layout->l2lim_pa = s_layout[1];
    layout->ddr_va   = s_layout[2];
    layout->ddr_pa   = s_layout[3];
    layout->ddr_size = s_layout[4];
}

static inline uint32_t sv39_vpn2(uintptr_t va)
{
    return (uint32_t)((va >> 30) & 0x1FFU);
}

/* Leaf PTE for a 1 GiB page. A and D are pre-set so the hardware never has
 * to update them (implementations may instead raise a page fault). */
static inline uint64_t sv39_gigapage(uintptr_t pa, uint64_t perms)
{
    return ((uint64_t)(pa >> 12) << 10) | perms |
           SV39_PTE_V | SV39_PTE_G | SV39_PTE_A | SV39_PTE_D;
}

sv39_enable_t sv39_build(void)
{
    sv39_layout_t l;
    sv39_enable_t ret;

    sv39_get_layout(&l);

    const uintptr_t onchip_va_base = l.l2lim_va & ~(SV39_GIGAPAGE - 1U);
    const uintptr_t onchip_pa_base = l.l2lim_pa & ~(SV39_GIGAPAGE - 1U);

    for (uint32_t i = 0U; i < 512U; i++)
    {
        g_sv39_root[i] = 0U;
    }

    /* Identity map of physical gigapage 0: peripheral registers (the HAL
     * drivers use physical addresses) and the instructions right after satp
     * is written, which still execute from the physical copy. */
    g_sv39_root[sv39_vpn2(0U)] =
        sv39_gigapage(0U, SV39_PTE_R | SV39_PTE_W | SV39_PTE_X);

    /* High alias of the on-chip gigapage: code and data run here. */
    g_sv39_root[sv39_vpn2(onchip_va_base)] =
        sv39_gigapage(onchip_pa_base, SV39_PTE_R | SV39_PTE_W | SV39_PTE_X);

    /* 38-bit cached DDR, one gigapage per GiB, data only. */
    for (uintptr_t off = 0U; off < l.ddr_size; off += SV39_GIGAPAGE)
    {
        g_sv39_root[sv39_vpn2(l.ddr_va + off)] =
            sv39_gigapage(l.ddr_pa + off, SV39_PTE_R | SV39_PTE_W);
    }

    /* Make the table stores visible to the page-table walker. */
    __asm volatile ("sfence.vma" ::: "memory");

    ret.satp = SV39_SATP_MODE | ((uintptr_t)g_sv39_root >> 12);   /* physical here */
    ret.onchip_va_offset = onchip_va_base - onchip_pa_base;
    return ret;
}
