/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * S-mode interrupt/exception glue for the FreeRTOS S-mode port.
 *
 * freertos_risc_v_application_interrupt_handler() is called by the port's
 * trap handler (on the ISR stack, task context already saved) for every
 * supervisor interrupt that is not the tick or a yield - in this project the
 * supervisor external interrupt from the PLIC. It claims from this hart's PLIC
 * supervisor context and dispatches through the HAL's ext_irq_handler_table,
 * so the usual PLIC_xxx_IRQHandler() names (and their legacy aliases such as
 * gpio1_bit16_or_gpio2_bit30_plic_30_IRQHandler) keep working.
 *
 * Handlers run in S-mode, so they must not call HAL functions that read
 * mhartid (PLIC_*, mss_config_clk_rst(), MSS_GPIO_enable_irq(), ...).
 * To unblock a task from a handler use the FromISR API and pass the
 * "higher priority task woken" result to vSmodeYieldFromISR().
 *
 * Also provides smode_os_init() (installs the FreeRTOS trap handler before
 * the boot code calls s_mode_main()) and weak defaults for the FreeRTOS
 * hooks the configuration needs; reports go to smode_diag_puts().
 */

#include "FreeRTOS.h"
#include "task.h"
#include "mpfs_hal/mss_hal.h"
#include "smode/smode_plic.h"
#include "smode/smode_irq.h"

#if (configPORT_YIELD_VIA_SBI_IPI == 1) && (configSBI_HART_ID != SMODE_HART)
#error "configSBI_HART_ID (FreeRTOSConfig.h) must equal SMODE_HART (smode_plic.h)"
#endif

/* HAL table of PLIC handlers (mss_mtrap.c). */
extern uint8_t (*ext_irq_handler_table[PLIC_NUM_SOURCES])(void);

static BaseType_t xYieldRequired = pdFALSE;

volatile uint64_t g_smode_ext_irqs = 0U;

void vSmodeYieldFromISR(BaseType_t xHigherPriorityTaskWoken)
{
    if (xHigherPriorityTaskWoken != pdFALSE)
    {
        xYieldRequired = pdTRUE;
    }
}

static void prvHalt(void) __attribute__((noreturn));
static void prvHalt(void)
{
    portDISABLE_INTERRUPTS();
    for (;;)
    {
        __asm volatile ("wfi");
    }
}

/*------------------------------------------------------------------------------
 * OS hook, called by smode_start.S right before s_mode_main()
 */
extern void freertos_risc_v_trap_handler(void);

void smode_os_init(void)
{
    /* Install the FreeRTOS trap handler (direct mode). */
    __asm volatile ("csrw stvec, %0" :: "r"((uintptr_t)&freertos_risc_v_trap_handler));
}

/*------------------------------------------------------------------------------
 * Port hooks
 */
void freertos_risc_v_application_interrupt_handler(uintptr_t scause, uintptr_t sepc)
{
    (void)sepc;

    if ((scause & MCAUSE_CAUSE) == IRQ_S_EXT)
    {
        uint32_t irq;

        xYieldRequired = pdFALSE;

        while (0U != (irq = SPLIC_claim()))
        {
            uint8_t disable = EXT_IRQ_KEEP_ENABLED;

            g_smode_ext_irqs++;

            if (irq < PLIC_NUM_SOURCES)
            {
                disable = ext_irq_handler_table[irq]();
            }

            SPLIC_complete(irq);

            if (EXT_IRQ_DISABLE == disable)
            {
                SPLIC_disable((PLIC_IRQn_Type)irq);
            }
        }

#if (configPORT_YIELD_VIA_SBI_IPI == 1)
        /* A task may have pended a yield (SSIP) that was still waiting when
         * this interrupt was taken (SEIP is taken first). On Renode, the PLIC
         * line dropping on claim clears the core's interrupt-check flag, so
         * that SSIP would wait for the next timer edge. Pend it again through
         * M-mode so it is taken on sret. */
        if (0U != (read_csr(sip) & MIP_SSIP))
        {
            portYIELD();
        }
#endif

        portYIELD_FROM_ISR(xYieldRequired);
        return;
    }

    /* Unexpected supervisor interrupt (nothing else is enabled in sie). */
    smode_fatal("S-mode: unexpected interrupt", scause, sepc, 0U);
}

void freertos_risc_v_application_exception_handler(uintptr_t scause, uintptr_t sepc_plus_4)
{
    uintptr_t stval;

    __asm volatile ("csrr %0, stval" : "=r"(stval));
    smode_fatal("S-mode exception", scause, sepc_plus_4 - 4U, stval);
}

/*------------------------------------------------------------------------------
 * FreeRTOS hooks. Weak, so the application can replace them.
 */
__attribute__((weak)) void vAssertCalled(const char *pcFile, unsigned long ulLine)
{
    portDISABLE_INTERRUPTS();
    smode_diag_puts("\r\n*** configASSERT failed: ");
    smode_diag_puts(pcFile);
    smode_diag_puts(":");
    smode_diag_putdec(ulLine);
    smode_diag_puts("\r\n");
    prvHalt();
}

__attribute__((weak)) void vApplicationMallocFailedHook(void)
{
    smode_diag_puts("\r\n*** FreeRTOS heap exhausted (configTOTAL_HEAP_SIZE)\r\n");
    prvHalt();
}

__attribute__((weak)) void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    smode_diag_puts("\r\n*** Stack overflow in task ");
    smode_diag_puts(pcTaskName);
    smode_diag_puts("\r\n");
    prvHalt();
}
