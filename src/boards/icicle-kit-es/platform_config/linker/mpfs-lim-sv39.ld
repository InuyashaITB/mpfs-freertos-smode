/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * mpfs-lim-sv39.ld
 *
 * LIM image (loaded into L2LIM by the debugger) that runs in S-mode with the
 * Sv39 MMU on, with a 3 GiB window of the 38-bit cached DDR mapped for it.
 * How that window is used is up to the application (see the DDR window
 * comment in SECTIONS).
 * Based on the HAL's platform_config_reference/linker/mpfs-lim.ld.
 *
 * Address map (gigapage = 1 GiB):
 *
 *   virtual (run, S-mode)                    physical (load)
 *   0xFFFFFFC0_00000000 .. +1 GiB  -------->  0x00_00000000  on-chip gigapage 0
 *   0xFFFFFFC0_08000000  l2lim_va  -------->  0x00_08000000  l2lim_pa
 *   0xFFFFFFC0_40000000 .. +3 GiB  -------->  0x10_00000000  ddr_cached_38bit_pa
 *   0x00000000_00000000 .. +1 GiB  -------->  0x00_00000000  identity: peripherals
 *                                                            and MMU switch-over
 *
 * Every section is linked at its virtual address and loaded at its physical
 * address ( > xxx_va AT> xxx_pa ). The virtual layout is what the program
 * uses; the physical layout is what the debugger writes.
 *
 * M-mode (HAL start-up, M-mode shim) runs the same code from its physical
 * copy with the MMU off. That works because -mcmodel=medany addresses
 * everything PC-relatively and the whole on-chip gigapage is aliased with one
 * offset (ONCHIP_VA_OFFSET), so PC-relative distances are the same in both
 * views. Rules that follow:
 *   1. every symbol the code refers to must be a virtual address, including
 *      load addresses (see __sc_load);
 *   2. absolute pointers stored in data (function pointer tables, ...) hold
 *      virtual addresses and are only valid in S-mode with the MMU on;
 *   3. l2lim_va and l2lim_pa must advance in lockstep: only sections that go
 *      to both of them may use them, loaded sections come first, and the
 *      ASSERTs at the end check VMA - LMA == ONCHIP_VA_OFFSET.
 ******************************************************************************/

OUTPUT_ARCH( "riscv" )
ENTRY(_start)

MEMORY
{
    /* ---- physical address space: where the image is loaded ------------- */
    l2lim_pa            (rwx) : ORIGIN = 0x0000000008000000, LENGTH = 0x0003FC00  /* 255 KiB */
    ram_code_load_pa    (r)   : ORIGIN = 0x000000000803FC00, LENGTH = 0x400       /* 1 KiB, last of L2LIM */
    ddr_cached_38bit_pa (rw)  : ORIGIN = 0x0000001000000000, LENGTH = 0xC0000000  /* 3 GiB */

    /* ---- virtual address space (Sv39 upper half): where it runs -------- */
    l2lim_va            (rwx) : ORIGIN = 0xFFFFFFC008000000, LENGTH = 0x0003FC00  /* 255 KiB */
    ddr_cached_38bit_va (rw)  : ORIGIN = 0xFFFFFFC040000000, LENGTH = 0xC0000000  /* 3 GiB */

    /* ---- on-chip windows used only by the M-mode HAL (aliases as well) -- */
    switch_code_dtim_va (rx)  : ORIGIN = 0xFFFFFFC001001C00, LENGTH = 0x400       /* PA 0x01001C00 */
    scratchpad_va       (rw)  : ORIGIN = 0xFFFFFFC00A000000, LENGTH = 0x40000     /* PA 0x0A000000 */
}

SV39_GIGAPAGE    = 0x40000000;
ONCHIP_VA_OFFSET = ORIGIN(l2lim_va) - ORIGIN(l2lim_pa);   /* VA - PA of the on-chip alias */

/* Layout rules for the gigapage mapping built in smode/sv39.c. */
ASSERT(ONCHIP_VA_OFFSET % SV39_GIGAPAGE == 0,
       "l2lim_va - l2lim_pa must be a multiple of 1 GiB (gigapage alias)")
ASSERT(ORIGIN(ram_code_load_pa) + LENGTH(ram_code_load_pa) <= SV39_GIGAPAGE,
       "L2LIM must lie in physical gigapage 0")
ASSERT(ORIGIN(switch_code_dtim_va) - ONCHIP_VA_OFFSET == 0x01001C00,
       "switch_code_dtim_va must be the alias of 0x01001C00")
ASSERT(ORIGIN(scratchpad_va) - ONCHIP_VA_OFFSET == 0x0A000000,
       "scratchpad_va must be the alias of 0x0A000000")
ASSERT(ORIGIN(ddr_cached_38bit_va) % SV39_GIGAPAGE == 0 &&
       ORIGIN(ddr_cached_38bit_pa) % SV39_GIGAPAGE == 0 &&
       LENGTH(ddr_cached_38bit_va) % SV39_GIGAPAGE == 0 &&
       LENGTH(ddr_cached_38bit_va) == LENGTH(ddr_cached_38bit_pa),
       "DDR regions must be whole, aligned gigapages of equal size")
ASSERT(ORIGIN(ddr_cached_38bit_va) - ORIGIN(l2lim_va) <= SV39_GIGAPAGE,
       "ddr_cached_38bit_va must start within 1 GiB of l2lim_va")
ASSERT(ORIGIN(ddr_cached_38bit_va) >= ONCHIP_VA_OFFSET + SV39_GIGAPAGE,
       "ddr_cached_38bit_va must not overlap the on-chip alias gigapage")

HEAP_SIZE               = 8k;
SIZE_OF_COMMON_HART_MEM = 4k;
STACK_SIZE_E51_APPLICATION   = 8k;
STACK_SIZE_U54_1_APPLICATION = 8k;
STACK_SIZE_U54_2_APPLICATION = 8k;
STACK_SIZE_U54_3_APPLICATION = 8k;
STACK_SIZE_U54_4_APPLICATION = 8k;

SECTIONS
{
    /* Region bounds used by the HAL and by smode/sv39.c. */
    __l2lim_start               = ORIGIN(l2lim_va);
    __l2lim_end                 = ORIGIN(l2lim_va) + LENGTH(l2lim_va);
    __l2lim_pa_start            = ORIGIN(l2lim_pa);
    __ddr_cached_38bit_va_start = ORIGIN(ddr_cached_38bit_va);
    __ddr_cached_38bit_pa_start = ORIGIN(ddr_cached_38bit_pa);
    __ddr_cached_38bit_size     = LENGTH(ddr_cached_38bit_va);

    /* DDR window: smode/sv39.c maps all of ddr_cached_38bit_va (gigapages,
     * RW) from the symbols above. Nothing here places anything in it; how it
     * is carved up belongs to the application: add a linker script fragment
     * of symbols (examples/freertos-demo/ddr_buffers.ld), or compute
     * addresses at run time from sv39_get_layout(). Do not make it an output
     * section: even a NOLOAD section gets a PT_LOAD program header with a
     * GiB-sized MemSiz, and Renode 1.13's LoadELF cannot handle it. */

    /* ===================== loaded sections ============================== */

    .text : ALIGN(0x10)
    {
        __text_start = .;
        *(.text.init)                   /* _start, must be first */
        . = ALIGN(0x10);
        *(.text .text.* .gnu.linkonce.t.*)
        *(.plt)
        . = ALIGN(0x10);

        KEEP (*crtbegin.o(.ctors))
        KEEP (*(EXCLUDE_FILE (*crtend.o) .ctors))
        KEEP (*(SORT(.ctors.*)))
        KEEP (*crtend.o(.ctors))
        KEEP (*crtbegin.o(.dtors))
        KEEP (*(EXCLUDE_FILE (*crtend.o) .dtors))
        KEEP (*(SORT(.dtors.*)))
        KEEP (*crtend.o(.dtors))

        *(.rodata .rodata.* .gnu.linkonce.r.*)
        *(.sdata2 .sdata2.* .gnu.linkonce.s2.*)
        *(.gcc_except_table)
        *(.eh_frame_hdr)
        *(.eh_frame)

        KEEP (*(.init))
        KEEP (*(.fini))

        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP (*(.preinit_array))
        PROVIDE_HIDDEN (__preinit_array_end = .);
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP (*(SORT(.init_array.*)))
        KEEP (*(.init_array))
        PROVIDE_HIDDEN (__init_array_end = .);
        PROVIDE_HIDDEN (__fini_array_start = .);
        KEEP (*(.fini_array))
        KEEP (*(SORT(.fini_array.*)))
        PROVIDE_HIDDEN (__fini_array_end = .);

        *(.srodata.cst16) *(.srodata.cst8) *(.srodata.cst4) *(.srodata.cst2)
        *(.srodata*)

        . = ALIGN(0x10);
        __text_end = .;
    } > l2lim_va AT> l2lim_pa

    .sdata : ALIGN(0x10)
    {
        __sdata_start = .;
        /* gp-relative reach is +/- 2 KiB, so point gp into the middle */
        __global_pointer$ = . + 0x800;
        *(.sdata .sdata.* .gnu.linkonce.s.*)
        . = ALIGN(0x10);
        __sdata_end = .;
    } > l2lim_va AT> l2lim_pa

    .data : ALIGN(0x10)
    {
        __data_start = .;
        *(.got.plt) *(.got)
        *(.shdata)
        *(.data .data.* .gnu.linkonce.d.*)
        . = ALIGN(0x10);
        __data_end = .;
    } > l2lim_va AT> l2lim_pa

    /* HAL clock-switch code: runs from DTIM, copied there from its load image
     * (last KiB of L2LIM) by copy_switch_code(). */
    .ram_code : ALIGN(4)
    {
        __sc_start = .;
        *(.ram_codetext .ram_codetext*)
        *(.ram_coderodata .ram_coderodata*)
        . = ALIGN(4);
        __sc_end = .;
    } > switch_code_dtim_va AT> ram_code_load_pa

    /* ===================== not loaded =================================== */

    /* HAL: L2 scratchpad. Unused, but the HAL checks it lies in the Zero
     * Device window. */
    .l2_scratchpad (NOLOAD) : ALIGN(0x10)
    {
        __l2_scratchpad_vma_start = .;
        *(.l2_scratchpad)
        . = ALIGN(0x10);
        __l2_scratchpad_vma_end = .;
    } > scratchpad_va

    /* Zero-initialised data, cleared by the HAL's init_memory(). */
    .sbss (NOLOAD) : ALIGN(0x10)
    {
        __sbss_start = .;
        *(.sbss .sbss.* .gnu.linkonce.sb.*)
        *(.scommon)
        . = ALIGN(0x10);
        __sbss_end = .;
    } > l2lim_va AT> l2lim_pa

    .bss (NOLOAD) : ALIGN(0x10)
    {
        __bss_start = .;
        *(.shbss)
        *(.bss .bss.* .gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(0x10);
        __bss_end = .;
    } > l2lim_va AT> l2lim_pa

    _end = .;

    .heap (NOLOAD) : ALIGN(0x10)
    {
        __heap_start = .;
        . += HEAP_SIZE;
        __heap_end = .;
        . = ALIGN(0x10);
        _heap_end = __heap_end;
    } > l2lim_va AT> l2lim_pa

    /* Per-hart start-up stacks with the HLS at the top (see the HAL). */
    .stack (NOLOAD) : ALIGN(0x1000)
    {
        __stack_bottom_h0$ = .;
        . += STACK_SIZE_E51_APPLICATION;
        __app_stack_top_h0 = .;
        __stack_top_h0$ = .;

        __stack_bottom_h1$ = .;
        . += STACK_SIZE_U54_1_APPLICATION;
        __app_stack_top_h1 = .;
        __stack_top_h1$ = .;

        __stack_bottom_h2$ = .;
        . += STACK_SIZE_U54_2_APPLICATION;
        __app_stack_top_h2 = .;
        __stack_top_h2$ = .;

        __stack_bottom_h3$ = .;
        . += STACK_SIZE_U54_3_APPLICATION;
        __app_stack_top_h3 = .;
        __stack_top_h3$ = .;

        __stack_bottom_h4$ = .;
        . += STACK_SIZE_U54_4_APPLICATION;
        __app_stack_top_h4 = .;
        __stack_top_h4$ = .;
    } > l2lim_va AT> l2lim_pa

    .app_hart_common (NOLOAD) :
    {
        __app_hart_common_start = .;
        . += SIZE_OF_COMMON_HART_MEM;
        __app_hart_common_end = .;
    } > l2lim_va AT> l2lim_pa

    /* ===================== derived symbols / checks ===================== */

    /* The debugger writes every section straight to its physical home, so
     * text/sdata/data need no copying: their load symbol equals the run
     * symbol and the HAL's copy_section() skips them. .ram_code is copied
     * from L2LIM to DTIM; its load symbol is the alias of its LMA (rule 1). */
    __text_load  = __text_start;
    __sdata_load = __sdata_start;
    __data_load  = __data_start;
    __sc_load    = LOADADDR(.ram_code) + ONCHIP_VA_OFFSET;

    /* Physical entry point: harts start here with the MMU off. */
    _start_pa = LOADADDR(.text);

    ASSERT(ADDR(.text)  - LOADADDR(.text)  == ONCHIP_VA_OFFSET, ".text: VMA - LMA != ONCHIP_VA_OFFSET")
    ASSERT(ADDR(.sdata) - LOADADDR(.sdata) == ONCHIP_VA_OFFSET, ".sdata: VMA - LMA != ONCHIP_VA_OFFSET")
    ASSERT(ADDR(.data)  - LOADADDR(.data)  == ONCHIP_VA_OFFSET, ".data: VMA - LMA != ONCHIP_VA_OFFSET")
}
