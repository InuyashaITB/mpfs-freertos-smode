/*
 * FreeRTOS Kernel V11.3.2 - RISC-V supervisor-mode (S-mode) port
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

/*
 * RISC-V S-mode port, derived from portable/GCC/RISC-V/portASM.S.
 *
 * The trap handler (freertos_risc_v_trap_handler) must be installed in stvec
 * (direct mode) before vTaskStartScheduler() is called, and M-mode firmware
 * must have delegated these interrupts to S-mode in mideleg:
 *   SSIP (1)  - used by portYIELD()
 *   STIP (5)  - tick, forwarded by M-mode from the machine timer
 *   SEIP (9)  - PLIC supervisor context, optional
 *
 * Differences from the M-mode port:
 *   - sstatus/sepc/scause/sret instead of mstatus/mepc/mcause/mret.
 *   - Initial task status: SPP=1 (return to S-mode) and SPIE=1.
 *   - Yield is a supervisor software interrupt (scause 1), not an ecall.
 *   - Tick is the supervisor timer interrupt (scause 5). The next compare
 *     value is programmed with an SBI TIME.set_timer ecall to M-mode, which
 *     writes mtimecmp and clears sip.STIP.
 *   - Everything else (supervisor external interrupt, all exceptions) is
 *     passed to the weak application handlers with a0 = scause, a1 = sepc.
 */

#include "FreeRTOSConfig.h"
#include "portContext.h"

/* SBI TIME extension, function set_timer (SBI v0.2+). */
#define portSBI_EXT_TIME          0x54494D45
#define portSBI_TIME_SET_TIMER    0

.global xPortStartFirstTask
.global pxPortInitialiseStack
.global freertos_risc_v_trap_handler

.extern vTaskSwitchContext
.extern xTaskIncrementTick
.extern pullNextTime
.extern uxTimerIncrementsForOneTick /* size_t type so 32-bit on 32-bit core and 64-bits on 64-bit core. */
.extern xTaskReturnAddress

.weak freertos_risc_v_application_exception_handler
.weak freertos_risc_v_application_interrupt_handler
/*-----------------------------------------------------------*/

/*
 * Program the next tick: sbi_set_timer( ullNextTime ), then advance
 * ullNextTime by one tick period. Clobbers a0-a1, a6-a7, t0-t2. Only used
 * from the trap handler after the task context has been saved.
 */
.macro portUPDATE_STIMER_COMPARE
    load_x t0, pullNextTime                 /* t0 = &ullNextTime. */
    ld a0, 0(t0)                            /* a0 = ullNextTime (64-bit). */
    li a7, portSBI_EXT_TIME
    li a6, portSBI_TIME_SET_TIMER
    ecall                                   /* M-mode writes mtimecmp, clears STIP. a0/a1 return error/value. */
    load_x t0, pullNextTime
    ld t2, 0(t0)
    load_x t1, uxTimerIncrementsForOneTick
    add t2, t2, t1
    sd t2, 0(t0)                            /* ullNextTime += one tick. */
    .endm
/*-----------------------------------------------------------*/

/*
 * Unlike other ports pxPortInitialiseStack() is written in assembly code as it
 * needs access to the portasmADDITIONAL_CONTEXT_SIZE constant.  The prototype
 * for the function is as per the other ports:
 * StackType_t *pxPortInitialiseStack( StackType_t *pxTopOfStack, TaskFunction_t pxCode, void *pvParameters );
 *
 * As per the standard RISC-V ABI pxTopOfStack is passed in in a0, pxCode in
 * a1, and pvParameters in a2.  The new top of stack is passed out in a0.
 *
 * The stack frame is identical to the M-mode port; only the status word
 * differs (sstatus with SPP=1, SPIE=1, SIE=0).
 */
.align 8
pxPortInitialiseStack:
    addi a0, a0, -portWORD_SIZE         /* Space for critical nesting count. */
    store_x x0, 0(a0)                   /* Critical nesting count starts at 0 for every task. */

#ifdef __riscv_32e
    addi a0, a0, -(6 * portWORD_SIZE)   /* Space for registers x10-x15. */
#else
    addi a0, a0, -(22 * portWORD_SIZE)  /* Space for registers x10-x31. */
#endif
    store_x a2, 0(a0)                   /* Task parameters (pvParameters parameter) goes into register x10/a0 on the stack. */

    addi a0, a0, -(6 * portWORD_SIZE)   /* Space for registers x5-x9 + taskReturnAddress (register x1). */
    load_x t0, xTaskReturnAddress
    store_x t0, 0(a0)                   /* Return address onto the stack. */

    csrr t0, sstatus                    /* Obtain current sstatus value. */
    andi t0, t0, ~0x2                   /* SIE=0: interrupts stay disabled until sret restores them from SPIE. */
    li t1, 0x120                        /* SPP=1 (bit 8, return to S-mode) | SPIE=1 (bit 5). */
    or t0, t0, t1

#if( configENABLE_FPU == 1 )
    /* Mark the FPU as clean in the sstatus value. */
    li t1, ~MSTATUS_FS_MASK
    and t0, t0, t1
    li t1, MSTATUS_FS_CLEAN
    or t0, t0, t1
#endif

#if( configENABLE_VPU == 1 )
    /* Mark the VPU as clean in the sstatus value. */
    li t1, ~MSTATUS_VS_MASK
    and t0, t0, t1
    li t1, MSTATUS_VS_CLEAN
    or t0, t0, t1
#endif

    addi a0, a0, -portWORD_SIZE
    store_x t0, 0(a0)                   /* sstatus onto the stack. */

    addi t0, x0, portasmADDITIONAL_CONTEXT_SIZE /* The number of chip specific additional registers. */
chip_specific_stack_frame:              /* First add any chip specific registers to the stack frame being created. */
    beq t0, x0, 1f                      /* No more chip specific registers to save. */
    addi a0, a0, -portWORD_SIZE         /* Make space for chip specific register. */
    store_x x0, 0(a0)                   /* Give the chip specific register an initial value of zero. */
    addi t0, t0, -1                     /* Decrement the count of chip specific registers remaining. */
    j chip_specific_stack_frame         /* Until no more chip specific registers. */
1:

    addi a0, a0, -portWORD_SIZE
    store_x a1, 0(a0)                   /* sret value (pxCode parameter) onto the stack. */
    ret
/*-----------------------------------------------------------*/

.align 8
xPortStartFirstTask:
    load_x  sp, pxCurrentTCB            /* Load pxCurrentTCB. */
    load_x  sp, 0( sp )                 /* Read sp from first TCB member. */

    load_x  x1, 0( sp ) /* Note for starting the scheduler the exception return address is used as the function return address. */

    portasmRESTORE_ADDITIONAL_REGISTERS /* Defined in freertos_risc_v_chip_specific_extensions.h to restore any registers unique to the RISC-V implementation. */

    load_x  x5, 1 * portWORD_SIZE( sp ) /* Initial sstatus into x5 (t0). */
    ori     x5, x5, 0x2                 /* Set SIE so the first task starts with interrupts enabled - required as returns with ret not sret. */
    csrw    sstatus, x5                 /* Interrupts enabled from here! */

    load_x  x7,  5  * portWORD_SIZE( sp )   /* t2 */
    load_x  x8,  6  * portWORD_SIZE( sp )   /* s0/fp */
    load_x  x9,  7  * portWORD_SIZE( sp )   /* s1 */
    load_x  x10, 8  * portWORD_SIZE( sp )   /* a0 */
    load_x  x11, 9  * portWORD_SIZE( sp )   /* a1 */
    load_x  x12, 10 * portWORD_SIZE( sp )   /* a2 */
    load_x  x13, 11 * portWORD_SIZE( sp )   /* a3 */
    load_x  x14, 12 * portWORD_SIZE( sp )   /* a4 */
    load_x  x15, 13 * portWORD_SIZE( sp )   /* a5 */
#ifndef __riscv_32e
    load_x  x16, 14 * portWORD_SIZE( sp )   /* a6 */
    load_x  x17, 15 * portWORD_SIZE( sp )   /* a7 */
    load_x  x18, 16 * portWORD_SIZE( sp )   /* s2 */
    load_x  x19, 17 * portWORD_SIZE( sp )   /* s3 */
    load_x  x20, 18 * portWORD_SIZE( sp )   /* s4 */
    load_x  x21, 19 * portWORD_SIZE( sp )   /* s5 */
    load_x  x22, 20 * portWORD_SIZE( sp )   /* s6 */
    load_x  x23, 21 * portWORD_SIZE( sp )   /* s7 */
    load_x  x24, 22 * portWORD_SIZE( sp )   /* s8 */
    load_x  x25, 23 * portWORD_SIZE( sp )   /* s9 */
    load_x  x26, 24 * portWORD_SIZE( sp )   /* s10 */
    load_x  x27, 25 * portWORD_SIZE( sp )   /* s11 */
    load_x  x28, 26 * portWORD_SIZE( sp )   /* t3 */
    load_x  x29, 27 * portWORD_SIZE( sp )   /* t4 */
    load_x  x30, 28 * portWORD_SIZE( sp )   /* t5 */
    load_x  x31, 29 * portWORD_SIZE( sp )   /* t6 */
#endif

    load_x  x5, portCRITICAL_NESTING_OFFSET * portWORD_SIZE( sp )    /* Obtain xCriticalNesting value for this task from task's stack. */
    load_x  x6, pxCriticalNesting           /* Load the address of xCriticalNesting into x6. */
    store_x x5, 0( x6 )                     /* Restore the critical nesting value for this task. */

    load_x  x5, 3 * portWORD_SIZE( sp )     /* Initial x5 (t0) value. */
    load_x  x6, 4 * portWORD_SIZE( sp )     /* Initial x6 (t1) value. */

    addi    sp, sp, portCONTEXT_SIZE
    ret
/*-----------------------------------------------------------*/

/* Default handlers: park the hart so the cause can be inspected in a
 * debugger. The application (smode_irq.c) overrides both. */
freertos_risc_v_application_exception_handler:
    csrr t0, scause     /* For viewing in the debugger only. */
    csrr t1, sepc       /* For viewing in the debugger only */
    csrr t2, sstatus    /* For viewing in the debugger only */
    j .
/*-----------------------------------------------------------*/

freertos_risc_v_application_interrupt_handler:
    csrr t0, scause     /* For viewing in the debugger only. */
    csrr t1, sepc       /* For viewing in the debugger only */
    csrr t2, sstatus    /* For viewing in the debugger only */
    j .
/*-----------------------------------------------------------*/

.section .text.freertos_risc_v_trap_handler
.align 8
freertos_risc_v_trap_handler:
    portcontextSAVE_CONTEXT_INTERNAL

    csrr a0, scause
    csrr a1, sepc

    bge a0, x0, synchronous_exception

asynchronous_interrupt:
    store_x a1, 0( sp )                 /* Asynchronous interrupt so save unmodified exception return address. */
    load_x sp, xISRStackTop             /* Switch to ISR stack. */
    j handle_interrupt

synchronous_exception:
    addi a1, a1, 4                      /* Synchronous so update exception return address to the instruction after the instruction that generated the exeption. */
    store_x a1, 0( sp )                 /* Save updated exception return address. */
    load_x sp, xISRStackTop             /* Switch to ISR stack. */
    j handle_exception

handle_interrupt:
    slli t0, a0, 1                      /* Drop the interrupt bit to get the cause number. */
    srli t0, t0, 1

    addi t1, x0, 5                      /* 5 == supervisor timer interrupt (tick). */
    beq t0, t1, supervisor_timer_interrupt

    addi t1, x0, 1                      /* 1 == supervisor software interrupt (yield). */
    beq t0, t1, supervisor_software_interrupt

application_interrupt_handler:          /* Supervisor external (PLIC) or anything else. a0 = scause, a1 = sepc. */
    call freertos_risc_v_application_interrupt_handler
    j processed_source

supervisor_timer_interrupt:
    portUPDATE_STIMER_COMPARE
    call xTaskIncrementTick
    beqz a0, processed_source           /* Don't switch context if incrementing tick didn't unblock a task. */
    call vTaskSwitchContext
    j processed_source

supervisor_software_interrupt:
    addi t0, x0, 2
    csrc sip, t0                        /* Clear SSIP - the yield request has been taken. */
    call vTaskSwitchContext
    j processed_source

handle_exception:
    /* No exception is used for normal operation in this port (yield is an
     * interrupt), so every exception is reported to the application.
     * a0 = scause, a1 = sepc + 4. */
    call freertos_risc_v_application_exception_handler
    j processed_source

processed_source:
    portcontextRESTORE_CONTEXT
/*-----------------------------------------------------------*/
