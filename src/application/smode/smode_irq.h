/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * S-mode interrupt/exception glue for the FreeRTOS S-mode port.
 */

#ifndef SMODE_IRQ_H_
#define SMODE_IRQ_H_

#include <stdint.h>
#include "FreeRTOS.h"

/* Last S-mode fault, for inspection in the debugger. */
typedef struct
{
    uintptr_t scause;
    uintptr_t sepc;
    uintptr_t stval;
    uintptr_t sstatus;
} smode_fault_t;

extern volatile smode_fault_t g_smode_fault;
extern volatile uint64_t g_smode_ext_irqs;

/* Call from a PLIC handler with the result of a ...FromISR() call; the
 * context switch (if any) happens when the PLIC dispatch loop finishes. */
void vSmodeYieldFromISR(BaseType_t xHigherPriorityTaskWoken);

/* Port hooks (called from portASM.S). */
void freertos_risc_v_application_interrupt_handler(uintptr_t scause, uintptr_t sepc);
void freertos_risc_v_application_exception_handler(uintptr_t scause, uintptr_t sepc_plus_4);

#endif /* SMODE_IRQ_H_ */
