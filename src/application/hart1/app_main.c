/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * U54_1 S-mode application: FreeRTOS demo.
 *
 * Entered from u54_1() via m_shim_enter_smode() with sstatus.SIE = 0 on a
 * small boot stack. Everything in this file runs in S-mode: no machine CSRs
 * (mhartid, mstatus, ...) and no HAL calls that use them.
 *
 * Tasks
 *   console (prio 2): prints a status line every second; polls MMUART1 and on
 *                     '1' / '2' / '3' drives GPIO2_26 / 27 / 28 high. Those
 *                     are looped back to GPIO2_30 / GPIO2_31 / F2M_0, so the
 *                     PLIC raises a supervisor external interrupt whose
 *                     handler (below) queues an event back to this task.
 *   blink   (prio 1): binary counter on the four LEDs (GPIO2_16..19), 250 ms.
 *   spin    (prio 0): never blocks, shares the idle priority; its counter
 *                     only advances if tick-driven time slicing works.
 *   ddr     (prio 1): once a second writes a pattern at the start, middle and
 *                     last qword of each 1 GiB DDR buffer through its virtual
 *                     address, reads it back, and has M-mode (no translation)
 *                     read the expected physical address to confirm the
 *                     Sv39 mapping.
 *
 * By the time s_mode_main() runs, smode_start.S has turned the Sv39 MMU on and
 * this code executes at its virtual (high) addresses.
 */

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "mpfs_hal/mss_hal.h"
#include "drivers/mss/mss_gpio/mss_gpio.h"
#include "drivers/mss/mss_mmuart/mss_uart.h"
#include "smode/m_shim.h"
#include "smode/smode_irq.h"
#include "smode/sv39.h"
#include "smode/sbi.h"
#include "ddr/ddr_buffers.h"

extern void freertos_risc_v_trap_handler(void);

typedef enum
{
    EVT_GPIO2_30 = 1,
    EVT_GPIO2_31 = 2,
    EVT_F2M_0    = 3,
} app_event_t;

static QueueHandle_t xEventQueue = NULL;
static volatile uint64_t ullSpinCount = 0U;
static volatile uint32_t ulBlinkCount = 0U;
static volatile uint32_t ulDdrPass = 0U;
static volatile uint32_t ulDdrFail = 0U;

/*------------------------------------------------------------------------------
 * Console helpers. Only the console task prints after the scheduler starts.
 */
static void prvPrint(const char *s)
{
    MSS_UART_polled_tx_string(&g_mss_uart1_lo, (const uint8_t *)s);
}

static uintptr_t prvReadSstatus(void)
{
    uintptr_t v;
    __asm volatile ("csrr %0, sstatus" : "=r"(v));
    return v;
}

static uintptr_t prvReadSatp(void)
{
    uintptr_t v;
    __asm volatile ("csrr %0, satp" : "=r"(v));
    return v;
}

/*------------------------------------------------------------------------------
 * PLIC handlers (S-mode). These override the weak handlers in the HAL and are
 * dispatched by smode_irq.c from the PLIC supervisor context.
 *
 * Inputs are level-high, so the driving output is dropped before the GPIO
 * interrupt is cleared, otherwise the level would re-trigger immediately.
 */
static void prvPostEvent(app_event_t evt)
{
    BaseType_t xWoken = pdFALSE;
    uint8_t ucEvt = (uint8_t)evt;

    (void)xQueueSendFromISR(xEventQueue, &ucEvt, &xWoken);
    vSmodeYieldFromISR(xWoken);
}

uint8_t gpio1_bit16_or_gpio2_bit30_plic_30_IRQHandler(void)
{
    MSS_GPIO_set_output(GPIO2_LO, MSS_GPIO_26, 0U);
    MSS_GPIO_clear_irq(GPIO2_LO, MSS_GPIO_30);
    prvPostEvent(EVT_GPIO2_30);
    return EXT_IRQ_KEEP_ENABLED;
}

uint8_t gpio1_bit17_or_gpio2_bit31_plic_31_IRQHandler(void)
{
    MSS_GPIO_set_output(GPIO2_LO, MSS_GPIO_27, 0U);
    MSS_GPIO_clear_irq(GPIO2_LO, MSS_GPIO_31);
    prvPostEvent(EVT_GPIO2_31);
    return EXT_IRQ_KEEP_ENABLED;
}

uint8_t fabric_f2h_0_plic_IRQHandler(void)
{
    MSS_GPIO_set_output(GPIO2_LO, MSS_GPIO_28, 0U);
    prvPostEvent(EVT_F2M_0);
    return EXT_IRQ_KEEP_ENABLED;
}

/*------------------------------------------------------------------------------
 * Tasks
 */
static void vConsoleTask(void *pvParameters)
{
    (void)pvParameters;
    char line[160];
    uint8_t rx[16];
    TickType_t xLastStatus = xTaskGetTickCount();

    prvPrint("[S] scheduler running. Press 1, 2 or 3 to raise GPIO2_30, "
             "GPIO2_31 or F2M_0 interrupts.\r\n");

    for (;;)
    {
        uint8_t ucEvt;

        if (pdPASS == xQueueReceive(xEventQueue, &ucEvt, pdMS_TO_TICKS(20)))
        {
            static const char * const names[] = { "?", "GPIO2_30", "GPIO2_31", "F2M_0" };
            snprintf(line, sizeof(line), "[S] PLIC S-context interrupt: %s (tick %lu)\r\n",
                     names[(ucEvt <= 3U) ? ucEvt : 0U],
                     (unsigned long)xTaskGetTickCount());
            prvPrint(line);
        }

        size_t n = MSS_UART_get_rx(&g_mss_uart1_lo, rx, sizeof(rx));
        for (size_t i = 0U; i < n; i++)
        {
            switch (rx[i])
            {
                case '1': MSS_GPIO_set_output(GPIO2_LO, MSS_GPIO_26, 1U); break;
                case '2': MSS_GPIO_set_output(GPIO2_LO, MSS_GPIO_27, 1U); break;
                case '3': MSS_GPIO_set_output(GPIO2_LO, MSS_GPIO_28, 1U); break;
                default: break;
            }
        }

        if ((xTaskGetTickCount() - xLastStatus) >= pdMS_TO_TICKS(1000))
        {
            xLastStatus += pdMS_TO_TICKS(1000);
            snprintf(line, sizeof(line),
                     "[S] tick=%lu sstatus=0x%lx spin=%lu blink=%lu "
                     "mtimer->stip=%lu sbi=%lu ext=%lu ddr_ok=%lu ddr_bad=%lu heap_free=%lu\r\n",
                     (unsigned long)xTaskGetTickCount(),
                     (unsigned long)prvReadSstatus(),
                     (unsigned long)ullSpinCount,
                     (unsigned long)ulBlinkCount,
                     (unsigned long)g_m_shim_timer_irqs,
                     (unsigned long)g_m_shim_sbi_calls,
                     (unsigned long)g_smode_ext_irqs,
                     (unsigned long)ulDdrPass,
                     (unsigned long)ulDdrFail,
                     (unsigned long)xPortGetFreeHeapSize());
            prvPrint(line);
        }
    }
}

static void vBlinkTask(void *pvParameters)
{
    (void)pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();

    for (;;)
    {
        ulBlinkCount++;
        for (uint32_t bit = 0U; bit < 4U; bit++)
        {
            MSS_GPIO_set_output(GPIO2_LO, (mss_gpio_id_t)(MSS_GPIO_16 + bit),
                                (uint8_t)((ulBlinkCount >> bit) & 1U));
        }
        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(250));
    }
}

/* Write through the virtual address, read back, and compare with what M-mode
 * sees at the physical address the page table should map it to. */
static void prvDdrCheck(uint32_t buf, size_t offset, uint64_t value, const sv39_layout_t *l)
{
    volatile uint64_t *va = (volatile uint64_t *)(void *)(g_ddr_buffer[buf] + offset);
    uint64_t pa = (uint64_t)l->ddr_pa + ((uintptr_t)va - l->ddr_va);

    *va = value;
    uint64_t readback = *va;
    sbi_ret_t phys = sbi_read_phys_u64(pa);

    if ((readback == value) && (0 == phys.error) && ((uint64_t)phys.value == value))
    {
        ulDdrPass++;
    }
    else
    {
        char line[160];
        ulDdrFail++;
        snprintf(line, sizeof(line),
                 "[S] DDR MISMATCH buf%lu VA 0x%lx PA 0x%lx wrote 0x%lx read 0x%lx phys 0x%lx (err %ld)\r\n",
                 (unsigned long)buf, (unsigned long)(uintptr_t)va, (unsigned long)pa,
                 (unsigned long)value, (unsigned long)readback,
                 (unsigned long)phys.value, (long)phys.error);
        prvPrint(line);
    }
}

static void vDdrTask(void *pvParameters)
{
    (void)pvParameters;
    static const size_t offsets[3] = { 0U, DDR_BUFFER_SIZE / 2U, DDR_BUFFER_SIZE - 8U };
    sv39_layout_t l;
    uint64_t seq = 0U;

    sv39_get_layout(&l);

    for (;;)
    {
        for (uint32_t b = 0U; b < DDR_BUFFER_COUNT; b++)
        {
            for (uint32_t i = 0U; i < 3U; i++)
            {
                uint64_t value = 0xD0D0000000000000ULL | (seq << 8) | ((uint64_t)b << 4) | i;
                prvDdrCheck(b, offsets[i], value, &l);
            }
        }
        seq++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void vSpinTask(void *pvParameters)
{
    (void)pvParameters;

    for (;;)
    {
        ullSpinCount++;
    }
}

/*------------------------------------------------------------------------------
 * S-mode entry
 */
void s_mode_main(void)
{
    /* Install the FreeRTOS trap handler (direct mode). */
    __asm volatile ("csrw stvec, %0" :: "r"((uintptr_t)&freertos_risc_v_trap_handler));

    {
        char line[128];
        sv39_layout_t l;

        sv39_get_layout(&l);

        snprintf(line, sizeof(line), "[S] s_mode_main: sstatus=0x%lx (SPP/SIE clear, FS on)\r\n",
                 (unsigned long)prvReadSstatus());
        prvPrint(line);
        snprintf(line, sizeof(line), "[S] Sv39 on: satp=0x%lx, running at 0x%lx (s_mode_main)\r\n",
                 (unsigned long)prvReadSatp(), (unsigned long)(uintptr_t)&s_mode_main);
        prvPrint(line);
        snprintf(line, sizeof(line), "[S]   L2LIM VA 0x%lx -> PA 0x%lx\r\n",
                 (unsigned long)l.l2lim_va, (unsigned long)l.l2lim_pa);
        prvPrint(line);
        for (uint32_t b = 0U; b < DDR_BUFFER_COUNT; b++)
        {
            uintptr_t va = (uintptr_t)g_ddr_buffer[b];
            snprintf(line, sizeof(line), "[S]   DDR buf%lu VA 0x%lx -> PA 0x%lx (%lu MiB)\r\n",
                     (unsigned long)b, (unsigned long)va,
                     (unsigned long)(l.ddr_pa + (va - l.ddr_va)),
                     (unsigned long)(DDR_BUFFER_SIZE >> 20));
            prvPrint(line);
        }
    }

    BaseType_t xOk = pdPASS;

    xEventQueue = xQueueCreate(8, sizeof(uint8_t));
    configASSERT(xEventQueue != NULL);

    xOk &= xTaskCreate(vConsoleTask, "console", 1024, NULL, 2, NULL);
    xOk &= xTaskCreate(vBlinkTask, "blink", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xOk &= xTaskCreate(vSpinTask, "spin", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY, NULL);
    xOk &= xTaskCreate(vDdrTask, "ddr", 512, NULL, 1, NULL);
    configASSERT(xOk == pdPASS);

    prvPrint("[S] starting scheduler\r\n");
    vTaskStartScheduler();

    /* Only reached if there was not enough heap for the idle task. */
    prvPrint("[S] vTaskStartScheduler returned!\r\n");
    for (;;)
    {
    }
}
