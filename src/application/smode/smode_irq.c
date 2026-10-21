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
 */

#include "FreeRTOS.h"
#include "task.h"
#include "mpfs_hal/mss_hal.h"
#include "drivers/mss/mss_mmuart/mss_uart.h"
#include "smode/smode_plic.h"
#include "smode/smode_irq.h"

#if (configPORT_YIELD_VIA_SBI_IPI == 1) && (configSBI_HART_ID != SMODE_HART)
#error "configSBI_HART_ID (FreeRTOSConfig.h) must equal SMODE_HART (smode_plic.h)"
#endif

/* HAL table of PLIC handlers (mss_mtrap.c). */
extern uint8_t (*ext_irq_handler_table[PLIC_NUM_SOURCES])(void);

static BaseType_t xYieldRequired = pdFALSE;

volatile smode_fault_t g_smode_fault;
volatile uint64_t g_smode_ext_irqs = 0U;

void vSmodeYieldFromISR(BaseType_t xHigherPriorityTaskWoken)
{
    if (xHigherPriorityTaskWoken != pdFALSE)
    {
        xYieldRequired = pdTRUE;
    }
}

/*------------------------------------------------------------------------------
 * Polled output (safe with interrupts disabled).
 */
static void prvPuts(const char *s)
{
    MSS_UART_polled_tx_string(&g_mss_uart1_lo, (const uint8_t *)s);
}

static void prvPutHex(uintptr_t v)
{
    char buf[19];
    static const char hex[] = "0123456789abcdef";

    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; i++)
    {
        buf[2 + i] = hex[(v >> (60 - (4 * i))) & 0xFU];
    }
    buf[18] = '\0';
    prvPuts(buf);
}

static void prvPutDec(unsigned long v)
{
    char buf[21];
    int i = 20;

    buf[i] = '\0';
    do
    {
        buf[--i] = (char)('0' + (v % 10U));
        v /= 10U;
    } while ((v != 0U) && (i > 0));
    prvPuts(&buf[i]);
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
    g_smode_fault.scause = scause;
    g_smode_fault.sepc = sepc;
    g_smode_fault.stval = 0U;
    prvPuts("\r\n*** S-mode: unexpected interrupt scause=");
    prvPutHex(scause);
    prvPuts("\r\n");
    prvHalt();
}

void freertos_risc_v_application_exception_handler(uintptr_t scause, uintptr_t sepc_plus_4)
{
    g_smode_fault.scause = scause;
    g_smode_fault.sepc = sepc_plus_4 - 4U;
    __asm volatile ("csrr %0, stval" : "=r"(g_smode_fault.stval));
    __asm volatile ("csrr %0, sstatus" : "=r"(g_smode_fault.sstatus));

    prvPuts("\r\n*** S-mode exception scause=");
    prvPutHex(g_smode_fault.scause);
    prvPuts(" sepc=");
    prvPutHex(g_smode_fault.sepc);
    prvPuts(" stval=");
    prvPutHex(g_smode_fault.stval);
    prvPuts(" sstatus=");
    prvPutHex(g_smode_fault.sstatus);
    prvPuts("\r\n");
    prvHalt();
}

/* Called from smode_start.S before FreeRTOS installs its trap handler, with
 * the MMU either still off or just switched on. */
void smode_early_trap_report(uintptr_t scause, uintptr_t sepc, uintptr_t stval)
{
    g_smode_fault.scause = scause;
    g_smode_fault.sepc = sepc;
    g_smode_fault.stval = stval;

    prvPuts("\r\n*** S-mode early trap (MMU bring-up) scause=");
    prvPutHex(scause);
    prvPuts(" sepc=");
    prvPutHex(sepc);
    prvPuts(" stval=");
    prvPutHex(stval);
    prvPuts("\r\n");
    prvHalt();
}

/*------------------------------------------------------------------------------
 * FreeRTOS hooks
 */
void vAssertCalled(const char *pcFile, unsigned long ulLine)
{
    portDISABLE_INTERRUPTS();
    prvPuts("\r\n*** configASSERT failed: ");
    prvPuts(pcFile);
    prvPuts(":");
    prvPutDec(ulLine);
    prvPuts("\r\n");
    prvHalt();
}

void vApplicationMallocFailedHook(void)
{
    prvPuts("\r\n*** FreeRTOS heap exhausted (configTOTAL_HEAP_SIZE)\r\n");
    prvHalt();
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    prvPuts("\r\n*** Stack overflow in task ");
    prvPuts(pcTaskName);
    prvPuts("\r\n");
    prvHalt();
}
