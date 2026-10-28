/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * The whole image in L2LIM at physical 0x0800_0000 (255 KiB), linked and
 * loaded at the same (physical) addresses: the bootloader copies each
 * segment to its address and starts U54_1 at _start (= 0x0800_0000). The
 * bootloader must have configured enough L2 as LIM. S-mode runs with the
 * application's Sv39 table, which maps this region 1:1.
 */

OUTPUT_ARCH( "riscv" )
ENTRY( _start )

MEMORY
{
    l2lim (rwx) : ORIGIN = 0x08000000, LENGTH = 0x3FC00
}

M_STACK_SIZE = 4K;      /* M-mode: boot, then traps from S-mode */
S_STACK_SIZE = 4K;      /* S-mode until the scheduler starts */

SECTIONS
{
    .text : ALIGN(16)
    {
        KEEP( *(.text.entry) )
        *(.text .text.*)
    } > l2lim

    .rodata : ALIGN(16)
    {
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
    } > l2lim

    .data : ALIGN(16)
    {
        *(.data .data.*)
        . = ALIGN(8);
        __global_pointer$ = . + 0x800;
        *(.sdata .sdata.*)
    } > l2lim

    .bss (NOLOAD) : ALIGN(16)
    {
        __bss_start = .;
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > l2lim

    .stacks (NOLOAD) : ALIGN(16)
    {
        . += M_STACK_SIZE;
        __m_stack_top = .;
        . += S_STACK_SIZE;
        __s_stack_top = .;
    } > l2lim
}
