/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * The whole image in L2LIM (255 KiB), linked at the virtual alias of L2LIM in
 * the high half and loaded at its physical address:
 *
 *   VA 0xFFFFFFC0_0800_0000  =  PA 0x0800_0000 + MPFS_ONCHIP_VA_OFFSET
 *
 * The bootloader copies each segment to its physical address (p_paddr) and
 * starts U54_1 in M-mode at _start_pa (0x0800_0000); e_entry is the virtual
 * _start. M-mode runs this physical copy with the MMU off: -mcmodel=medany
 * addresses everything PC-relative, so the code works at either address.
 * S-mode jumps to the alias once Sv39 is on (s_start.S). Absolute addresses
 * stored in data (function pointers, ...) are therefore virtual and only valid
 * in S-mode.
 */

OUTPUT_ARCH( "riscv" )
ENTRY( _start )

/* MPFS_ONCHIP_VA_OFFSET in mpfs.h. */
ONCHIP_VA_OFFSET = 0xFFFFFFC000000000;

/* L2LIM at its alias (VMA); each section is loaded at VMA - ONCHIP_VA_OFFSET,
 * i.e. at PA 0x0800_0000 onwards, so both views stay in step. */
MEMORY
{
    l2lim_va (rwx) : ORIGIN = 0xFFFFFFC008000000, LENGTH = 0x3FC00
}

M_STACK_SIZE = 4K;      /* M-mode: boot, then traps from S-mode */
S_STACK_SIZE = 4K;      /* S-mode until the scheduler starts */

SECTIONS
{
    .text : AT(ADDR(.text) - ONCHIP_VA_OFFSET) ALIGN(16)
    {
        KEEP( *(.text.entry) )
        *(.text .text.*)
    } > l2lim_va

    .rodata : AT(ADDR(.rodata) - ONCHIP_VA_OFFSET) ALIGN(16)
    {
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
    } > l2lim_va

    .data : AT(ADDR(.data) - ONCHIP_VA_OFFSET) ALIGN(16)
    {
        *(.data .data.*)
        . = ALIGN(8);
        __global_pointer$ = . + 0x800;
        *(.sdata .sdata.*)
    } > l2lim_va

    .bss (NOLOAD) : AT(ADDR(.bss) - ONCHIP_VA_OFFSET) ALIGN(16)
    {
        __bss_start = .;
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > l2lim_va

    .stacks (NOLOAD) : AT(ADDR(.stacks) - ONCHIP_VA_OFFSET) ALIGN(16)
    {
        . += M_STACK_SIZE;
        __m_stack_top = .;
        . += S_STACK_SIZE;
        __s_stack_top = .;
    } > l2lim_va

    /* Physical entry point for the bootloader. */
    _start_pa = LOADADDR(.text);

    ASSERT(ADDR(.text)   - LOADADDR(.text)   == ONCHIP_VA_OFFSET, ".text: VMA - LMA != ONCHIP_VA_OFFSET")
    ASSERT(ADDR(.data)   - LOADADDR(.data)   == ONCHIP_VA_OFFSET, ".data: VMA - LMA != ONCHIP_VA_OFFSET")
    ASSERT(ADDR(.stacks) - LOADADDR(.stacks) == ONCHIP_VA_OFFSET, ".stacks: VMA - LMA != ONCHIP_VA_OFFSET")
}
