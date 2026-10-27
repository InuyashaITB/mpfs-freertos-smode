/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * U54_1 S-mode application: FreeRTOS demo, built against libmpfs_smode_boot.a
 * and libfreertos_smode.a.
 *
 * The libraries enter s_mode_main() in S-mode with the Sv39 MMU on, the
 * FreeRTOS trap handler installed and sstatus.SIE = 0, on a small boot stack
 * (see smode/mpfs_smode.h). The M-mode peripheral set-up this demo needs is in
 * m_prehook.c. Everything in this file runs in S-mode: no machine CSRs
 * (mhartid, mstatus, ...) and no HAL calls that use them.
 *
 * Tasks
 *   console (prio 2): prints a status line every second; polls MMUART1 and on
 *                     '1' / '2' / '3' drives GPIO2_26 / 27 / 28 high. Those
 *                     are looped back to GPIO2_30 / GPIO2_31 / F2M_0, so the
 *                     PLIC raises a supervisor external interrupt whose
 *                     handler (below) queues an event back to this task.
 *   blink   (prio 1): binary counter on the four LEDs (GPIO2_16..19), 250 ms.
 *   spin    (prio 0): "DDR Life": Conway's Game of Life played in far DDR.
 *                     The two generation grids live 1 GiB apart (buf0 and
 *                     buf1) and the history ring in the last MiB of buf2.
 *                     Never sleeps and shares the idle priority, so its
 *                     generation counter only advances if tick-driven time
 *                     slicing works. Every 10 s it pretty-prints the board,
 *                     stats and the VA -> PA of each region (see below).
 *   ddr     (prio 1): once a second writes a pattern at the start, middle and
 *                     last qword of each 1 GiB DDR buffer through its virtual
 *                     address, reads it back, and has M-mode (no translation)
 *                     read the expected physical address to confirm the
 *                     Sv39 mapping.
 *   heartbeat (prio 1): every 5 s prints uptime and heap/stack headroom.
 *
 * By the time s_mode_main() runs, the library's smode_start has turned the
 * Sv39 MMU on and this code executes at its virtual (high) addresses.
 *
 * Console output: after the scheduler starts, every line goes through
 * prvPrintf(), which formats and transmits under xConsoleMutex so lines from
 * different tasks never interleave (MMUART1 is written with polled I/O).
 * ISRs must not print; they post events to the console task instead.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "mpfs_hal/mss_hal.h"
#include "drivers/mss/mss_gpio/mss_gpio.h"
#include "drivers/mss/mss_mmuart/mss_uart.h"
#include "smode/mpfs_smode.h"
#include "smode/m_shim.h"
#include "smode/smode_irq.h"
#include "smode/smode_plic.h"
#include "smode/sv39.h"
#include "smode/sbi.h"
#include "ddr_buffers.h"
#include "periph_selftest.h"

typedef enum
{
    EVT_GPIO2_30 = 1,
    EVT_GPIO2_31 = 2,
    EVT_F2M_0    = 3,
} app_event_t;

#define HEARTBEAT_PERIOD_MS     (5000U)

static QueueHandle_t xEventQueue = NULL;
static SemaphoreHandle_t xConsoleMutex = NULL;
static volatile uint64_t ullSpinCount = 0U;
static volatile uint32_t ulBlinkCount = 0U;
static volatile uint32_t ulDdrPass = 0U;
static volatile uint32_t ulDdrFail = 0U;

/*------------------------------------------------------------------------------
 * Console output (task context only).
 *
 * Before the scheduler starts there is only one thread, so no locking. After
 * that, formatting and transmission happen under xConsoleMutex. That also
 * serialises newlib's formatter, which is not reentrant here
 * (configUSE_NEWLIB_REENTRANT = 0).
 */
static BaseType_t prvConsoleLock(void)
{
    const BaseType_t xLocked = (xConsoleMutex != NULL) &&
        (taskSCHEDULER_RUNNING == xTaskGetSchedulerState());

    if (xLocked)
    {
        (void)xSemaphoreTake(xConsoleMutex, portMAX_DELAY);
    }
    return xLocked;
}

static void prvConsoleUnlock(BaseType_t xLocked)
{
    if (xLocked)
    {
        (void)xSemaphoreGive(xConsoleMutex);
    }
}

/* Format and transmit one line. The caller holds the console lock (or the
 * scheduler is not running yet), so a multi-line block stays together. */
static void prvPrintfLocked(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void prvPrintfLocked(const char *fmt, ...)
{
    static char line[192];
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    MSS_UART_polled_tx_string(&g_mss_uart1_lo, (const uint8_t *)line);
}

static void prvPrintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void prvPrintf(const char *fmt, ...)
{
    static char line[192];
    const BaseType_t xLocked = prvConsoleLock();
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    MSS_UART_polled_tx_string(&g_mss_uart1_lo, (const uint8_t *)line);

    prvConsoleUnlock(xLocked);
}

static void prvPrint(const char *s)
{
    prvPrintf("%s", s);
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
            prvPrintf("[S] PLIC S-context interrupt: %s (tick %lu)\r\n",
                      names[(ucEvt <= 3U) ? ucEvt : 0U],
                      (unsigned long)xTaskGetTickCount());
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
            prvPrintf("[S] tick=%lu sstatus=0x%lx spin=%lu blink=%lu "
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
        ulDdrFail++;
        prvPrintf("[S] DDR MISMATCH buf%lu VA 0x%lx PA 0x%lx wrote 0x%lx read 0x%lx phys 0x%lx (err %ld)\r\n",
                  (unsigned long)buf, (unsigned long)(uintptr_t)va, (unsigned long)pa,
                  (unsigned long)value, (unsigned long)readback,
                  (unsigned long)phys.value, (long)phys.error);
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

static void vHeartbeatTask(void *pvParameters)
{
    (void)pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();
    uint32_t ulBeat = 0U;

    for (;;)
    {
        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(HEARTBEAT_PERIOD_MS));
        ulBeat++;

        const TickType_t xNow = xTaskGetTickCount();
        const unsigned long ulSeconds = (unsigned long)(xNow / configTICK_RATE_HZ);

        prvPrintf("[S] heartbeat #%lu: uptime %02lu:%02lu:%02lu (tick %lu), "
                  "heap free %lu (min %lu), stack headroom %lu words\r\n",
                  (unsigned long)ulBeat,
                  ulSeconds / 3600U, (ulSeconds / 60U) % 60U, ulSeconds % 60U,
                  (unsigned long)xNow,
                  (unsigned long)xPortGetFreeHeapSize(),
                  (unsigned long)xPortGetMinimumEverFreeHeapSize(),
                  (unsigned long)uxTaskGetStackHighWaterMark(NULL));
    }
}

/*------------------------------------------------------------------------------
 * DDR Life (spin task)
 *
 * Conway's Game of Life on a 64 x 32 torus, with all of its state in the
 * 38-bit cached DDR window, i.e. in memory only reachable through the Sv39
 * mapping (and, for buf2, beyond the +/-2 GiB reach of PC-relative code, so
 * everything goes through g_ddr_buffer[]):
 *
 *   buf0 + 256 MiB   grid A  \  generations ping-pong between them, so every
 *   buf1 + 768 MiB   grid B  /  step reads one GiB and writes another
 *   buf2 + 1023 MiB  history ring: FNV-1a hash and population of the last
 *                    LIFE_HISTORY generations (top of the 3 GiB window)
 *
 * The offsets stay clear of the qwords the ddr task probes (start, middle and
 * last qword of each buffer). DDR is NOLOAD and not cleared at boot, so every
 * byte used here is written before it is read.
 *
 * A board that repeats one of its last LIFE_MAX_PERIOD generations (still
 * life, oscillator or extinction) has "settled". It keeps running for
 * LIFE_SETTLED_GENS more generations, then is reseeded.
 */
#define LIFE_W                  (64U)
#define LIFE_H                  (32U)
#define LIFE_CELLS              (LIFE_W * LIFE_H)
#define LIFE_HISTORY            (64U)       /* power of two */
#define LIFE_MAX_PERIOD         (32U)
#define LIFE_SETTLED_GENS       (500U)
#define LIFE_PRINT_PERIOD_MS    (10000U)
#define LIFE_SPARK_LEN          (LIFE_W - 16U)

#define LIFE_GRID0_OFFSET       (256UL << 20)                       /* in buf0 */
#define LIFE_GRID1_OFFSET       (768UL << 20)                       /* in buf1 */
#define LIFE_HIST_OFFSET        (DDR_BUFFER_SIZE - (1UL << 20))     /* in buf2 */

typedef struct
{
    uint64_t hash;
    uint32_t pop;
    uint32_t reserved;
} life_hist_t;

typedef struct
{
    uint8_t     *grid[2];       /* VA of grid A (buf0) and B (buf1) */
    life_hist_t *hist;          /* VA of the history ring (buf2) */
    uint32_t     cur;           /* grid holding the current generation */
    uint64_t     rng;
    uint32_t     seed;          /* seed number, 1.. */
    uint32_t     gen;           /* generation within this seed */
    uint32_t     pop;
    uint32_t     births;
    uint32_t     deaths;
    uint32_t     period;        /* 0 = still evolving */
    uint32_t     settled_gen;
    uint32_t     last_seed;     /* outcome of the previous seed, for the report */
    uint32_t     last_gen;
    uint32_t     last_period;
    uint32_t     last_pop;
} life_t;

static uint64_t prvLifeRand(life_t *s)
{
    /* xorshift64* */
    s->rng ^= s->rng >> 12;
    s->rng ^= s->rng << 25;
    s->rng ^= s->rng >> 27;
    return s->rng * 0x2545F4914F6CDD1DULL;
}

static uint64_t prvLifeHash(const uint8_t *g)
{
    uint64_t h = 0xCBF29CE484222325ULL;     /* FNV-1a */

    for (uint32_t i = 0U; i < LIFE_CELLS; i++)
    {
        h = (h ^ g[i]) * 0x100000001B3ULL;
    }
    return h;
}

static void prvLifeSeed(life_t *s)
{
    uint8_t *g = s->grid[s->cur];
    uint32_t pop = 0U;

    s->seed++;
    s->rng = 0x9E3779B97F4A7C15ULL * s->seed;
    for (uint32_t i = 0U; i < LIFE_CELLS; i++)
    {
        g[i] = ((prvLifeRand(s) >> 61) < 3U) ? 1U : 0U;     /* ~37 % alive */
        pop += g[i];
    }

    s->gen = 0U;
    s->pop = pop;
    s->births = pop;
    s->deaths = 0U;
    s->period = 0U;
    s->hist[0].hash = prvLifeHash(g);
    s->hist[0].pop = pop;
}

/* One generation, current grid -> the other one. */
static void prvLifeStep(life_t *s)
{
    const uint8_t *c = s->grid[s->cur];
    uint8_t *n = s->grid[s->cur ^ 1U];
    uint32_t pop = 0U, births = 0U, deaths = 0U;

    for (uint32_t y = 0U; y < LIFE_H; y++)
    {
        const uint8_t *up = &c[((y + LIFE_H - 1U) % LIFE_H) * LIFE_W];
        const uint8_t *mid = &c[y * LIFE_W];
        const uint8_t *dn = &c[((y + 1U) % LIFE_H) * LIFE_W];

        for (uint32_t x = 0U; x < LIFE_W; x++)
        {
            const uint32_t l = (x + LIFE_W - 1U) % LIFE_W;
            const uint32_t r = (x + 1U) % LIFE_W;
            const uint32_t nb = up[l] + up[x] + up[r] + mid[l] + mid[r] + dn[l] + dn[x] + dn[r];
            const uint8_t was = mid[x];
            const uint8_t now = ((3U == nb) || ((0U != was) && (2U == nb))) ? 1U : 0U;

            n[y * LIFE_W + x] = now;
            pop += now;
            births += (uint32_t)(now & (uint8_t)~was);
            deaths += (uint32_t)(was & (uint8_t)~now);
        }
    }

    s->cur ^= 1U;
    s->gen++;
    s->pop = pop;
    s->births = births;
    s->deaths = deaths;
}

/* Record the new generation in the history ring and check whether it repeats
 * one of the last LIFE_MAX_PERIOD generations. */
static void prvLifeTrack(life_t *s)
{
    const uint64_t h = prvLifeHash(s->grid[s->cur]);
    const uint32_t lookback = (s->gen < LIFE_MAX_PERIOD) ? s->gen : LIFE_MAX_PERIOD;

    if (0U == s->period)
    {
        for (uint32_t p = 1U; p <= lookback; p++)
        {
            const life_hist_t *e = &s->hist[(s->gen - p) % LIFE_HISTORY];

            if ((e->hash == h) && (e->pop == s->pop))
            {
                s->period = p;
                s->settled_gen = s->gen - p;
                break;
            }
        }
    }

    s->hist[s->gen % LIFE_HISTORY].hash = h;
    s->hist[s->gen % LIFE_HISTORY].pop = s->pop;
}

static uint64_t prvDdrPa(const sv39_layout_t *l, const volatile void *va)
{
    return (uint64_t)l->ddr_pa + ((uintptr_t)va - l->ddr_va);
}

static void prvLifePrint(const life_t *s, const sv39_layout_t *l, unsigned long ulGensPerS)
{
    static const char shades[] = " .:-=+*#%@";
    static const char dashes[] = "----------------------------------------------------------------";
    char row[LIFE_W + 1U];
    char spark[LIFE_SPARK_LEN + 1U];
    const uint8_t *g = s->grid[s->cur];
    const uint32_t n = (s->gen < LIFE_SPARK_LEN) ? (s->gen + 1U) : LIFE_SPARK_LEN;
    uint32_t pmin = 0xFFFFFFFFU, pmax = 0U;

    /* Population sparkline over the last n generations, read back from buf2. */
    for (uint32_t i = 0U; i < n; i++)
    {
        const uint32_t p = s->hist[(s->gen - (n - 1U) + i) % LIFE_HISTORY].pop;
        pmin = (p < pmin) ? p : pmin;
        pmax = (p > pmax) ? p : pmax;
    }
    for (uint32_t i = 0U; i < n; i++)
    {
        const uint32_t p = s->hist[(s->gen - (n - 1U) + i) % LIFE_HISTORY].pop;
        spark[i] = (pmax == pmin) ? shades[5] :
                   shades[1U + (((p - pmin) * (sizeof(shades) - 3U)) / (pmax - pmin))];
    }
    spark[n] = '\0';

    /* Have M-mode (no translation) read, at the physical address the page
     * table maps it to, the first qword of the board that has live cells, so
     * the comparison is not just zeros. */
    const volatile uint64_t *q = (const volatile uint64_t *)(const void *)g;
    uint32_t qi = 0U;

    while ((qi < ((LIFE_CELLS / 8U) - 1U)) && (0U == q[qi]))
    {
        qi++;
    }

    const uint64_t pa = prvDdrPa(l, &q[qi]);
    const uint64_t via_va = q[qi];
    const sbi_ret_t via_pa = sbi_read_phys_u64(pa);

    char title[LIFE_W + 1U];
    const int tlen = snprintf(title, sizeof(title), "- DDR Life - seed #%lu - generation %lu ",
                              (unsigned long)s->seed, (unsigned long)s->gen);

    const BaseType_t xLocked = prvConsoleLock();

    prvPrintfLocked("\r\n[S] .%s%.*s.\r\n", title, (int)LIFE_W - tlen, dashes);
    for (uint32_t y = 0U; y < LIFE_H; y++)
    {
        for (uint32_t x = 0U; x < LIFE_W; x++)
        {
            row[x] = (0U != g[y * LIFE_W + x]) ? '#' : ' ';
        }
        row[LIFE_W] = '\0';
        prvPrintfLocked("[S] |%s|\r\n", row);
    }
    prvPrintfLocked("[S] '%s'\r\n", dashes);

    prvPrintfLocked("[S]   population %lu (+%lu born, -%lu died)   %lu gens/s, %lu total\r\n",
                    (unsigned long)s->pop, (unsigned long)s->births, (unsigned long)s->deaths,
                    ulGensPerS, (unsigned long)ullSpinCount);
    prvPrintfLocked("[S]   last %2lu gens [%s] %lu..%lu cells\r\n",
                    (unsigned long)n, spark, (unsigned long)pmin, (unsigned long)pmax);
    if (0U == s->period)
    {
        prvPrintfLocked("[S]   state      evolving\r\n");
    }
    else
    {
        prvPrintfLocked("[S]   state      settled at gen %lu: %s, period %lu; reseed in %lu gens\r\n",
                        (unsigned long)s->settled_gen,
                        (0U == s->pop) ? "extinct" : (1U == s->period) ? "still life" : "oscillator",
                        (unsigned long)s->period,
                        (unsigned long)(LIFE_SETTLED_GENS - (s->gen - s->settled_gen)));
    }
    if (0U != s->last_seed)
    {
        prvPrintfLocked("[S]   previous   seed #%lu settled at gen %lu, period %lu, %lu cells\r\n",
                        (unsigned long)s->last_seed, (unsigned long)s->last_gen,
                        (unsigned long)s->last_period, (unsigned long)s->last_pop);
    }
    prvPrintfLocked("[S]   grid A %c   buf0 VA 0x%lx -> PA 0x%lx\r\n", (0U == s->cur) ? '*' : ' ',
                    (unsigned long)(uintptr_t)s->grid[0], (unsigned long)prvDdrPa(l, s->grid[0]));
    prvPrintfLocked("[S]   grid B %c   buf1 VA 0x%lx -> PA 0x%lx\r\n", (1U == s->cur) ? '*' : ' ',
                    (unsigned long)(uintptr_t)s->grid[1], (unsigned long)prvDdrPa(l, s->grid[1]));
    prvPrintfLocked("[S]   history    buf2 VA 0x%lx -> PA 0x%lx\r\n",
                    (unsigned long)(uintptr_t)s->hist, (unsigned long)prvDdrPa(l, s->hist));
    prvPrintfLocked("[S]   check      row %lu col %lu: S-mode VA reads 0x%016lx,\r\n"
                    "[S]              M-mode PA 0x%lx reads 0x%016lx: %s\r\n\r\n",
                    (unsigned long)((qi * 8U) / LIFE_W), (unsigned long)((qi * 8U) % LIFE_W),
                    (unsigned long)via_va, (unsigned long)pa, (unsigned long)via_pa.value,
                    ((0 == via_pa.error) && ((uint64_t)via_pa.value == via_va)) ? "match" : "MISMATCH");

    prvConsoleUnlock(xLocked);
}

static void vSpinTask(void *pvParameters)
{
    (void)pvParameters;
    static life_t s;
    sv39_layout_t l;

    sv39_get_layout(&l);

    s.grid[0] = (uint8_t *)(void *)(g_ddr_buffer[0] + LIFE_GRID0_OFFSET);
    s.grid[1] = (uint8_t *)(void *)(g_ddr_buffer[1] + LIFE_GRID1_OFFSET);
    s.hist = (life_hist_t *)(void *)(g_ddr_buffer[2] + LIFE_HIST_OFFSET);
    prvLifeSeed(&s);

    TickType_t xLastPrint = xTaskGetTickCount();
    uint64_t ullGensAtPrint = ullSpinCount;

    for (;;)
    {
        prvLifeStep(&s);
        prvLifeTrack(&s);
        ullSpinCount++;

        if ((0U != s.period) && ((s.gen - s.settled_gen) >= LIFE_SETTLED_GENS))
        {
            s.last_seed = s.seed;
            s.last_gen = s.settled_gen;
            s.last_period = s.period;
            s.last_pop = s.pop;
            prvLifeSeed(&s);
        }

        const TickType_t xNow = xTaskGetTickCount();
        if ((xNow - xLastPrint) >= pdMS_TO_TICKS(LIFE_PRINT_PERIOD_MS))
        {
            const unsigned long ulGensPerS = (unsigned long)
                (((ullSpinCount - ullGensAtPrint) * configTICK_RATE_HZ) / (xNow - xLastPrint));

            prvLifePrint(&s, &l, ulGensPerS);
            xLastPrint = xNow;
            ullGensAtPrint = ullSpinCount;
        }
    }
}

/*------------------------------------------------------------------------------
 * S-mode entry
 */
/* PLIC sources used by this demo, enabled for the S context below. The GPIO
 * side (input mode, level, interrupt enable) is set up in m_prehook.c. */
static const PLIC_IRQn_Type g_smode_irqs[] =
{
    GPIO1_BIT16_or_GPIO2_BIT30_PLIC_30,
    GPIO1_BIT17_or_GPIO2_BIT31_PLIC_31,
    FABRIC_F2H_0_PLIC,
};

void s_mode_main(void)
{
    /* The library left every PLIC source disabled for this context. Nothing
     * is taken until the scheduler starts (sstatus.SIE = 0). */
    for (uint32_t i = 0U; i < (sizeof(g_smode_irqs) / sizeof(g_smode_irqs[0])); i++)
    {
        PLIC_SetPriority(g_smode_irqs[i], 2U);
        SPLIC_enable(g_smode_irqs[i]);
    }

    {
        sv39_layout_t l;

        sv39_get_layout(&l);

        prvPrintf("[S] s_mode_main: sstatus=0x%lx (SPP/SIE clear, FS on)\r\n",
                  (unsigned long)prvReadSstatus());
        prvPrintf("[S] Sv39 on: satp=0x%lx, running at 0x%lx (s_mode_main)\r\n",
                  (unsigned long)prvReadSatp(), (unsigned long)(uintptr_t)&s_mode_main);
        prvPrintf("[S]   L2LIM VA 0x%lx -> PA 0x%lx\r\n",
                  (unsigned long)l.l2lim_va, (unsigned long)l.l2lim_pa);
        for (uint32_t b = 0U; b < DDR_BUFFER_COUNT; b++)
        {
            uintptr_t va = (uintptr_t)g_ddr_buffer[b];
            prvPrintf("[S]   DDR buf%lu VA 0x%lx -> PA 0x%lx (%lu MiB)\r\n",
                      (unsigned long)b, (unsigned long)va,
                      (unsigned long)(l.ddr_pa + (va - l.ddr_va)),
                      (unsigned long)(DDR_BUFFER_SIZE >> 20));
        }
    }

    {
        spi_loopback_result_t r;

        prvPrintf("[S] SPI1 loopback: starting\r\n");
        spi_loopback_selftest(&r);
        if ((0U != r.tx_timeouts) || (0U != r.rx_timeouts))
        {
            prvPrintf("[S] SPI1 loopback: no response, %lu of %lu frames timed out "
                      "(TX done %lu, RX ready %lu). Is the Renode model attached? "
                      "See renode/README.md\r\n",
                      (unsigned long)(r.tx_timeouts + r.rx_timeouts), (unsigned long)r.frames,
                      (unsigned long)r.tx_timeouts, (unsigned long)r.rx_timeouts);
        }
        if (0U != r.errors)
        {
            prvPrintf("[S] SPI1 loopback: %lu of %lu frames wrong, first sent 0x%02lx got 0x%02lx "
                      "(expected the complement)\r\n",
                      (unsigned long)r.errors, (unsigned long)r.frames,
                      (unsigned long)r.first_bad_tx, (unsigned long)r.first_bad_rx);
        }
        if ((0U == r.errors) && (0U == r.tx_timeouts) && (0U == r.rx_timeouts))
        {
            prvPrintf("[S] SPI1 loopback (S-mode, MSS_SPI driver): %lu frames OK\r\n",
                      (unsigned long)r.frames);
        }
    }

    BaseType_t xOk = pdPASS;

    xEventQueue = xQueueCreate(8, sizeof(uint8_t));
    configASSERT(xEventQueue != NULL);
    xConsoleMutex = xSemaphoreCreateMutex();
    configASSERT(xConsoleMutex != NULL);

    xOk &= xTaskCreate(vConsoleTask, "console", 1024, NULL, 2, NULL);
    xOk &= xTaskCreate(vBlinkTask, "blink", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xOk &= xTaskCreate(vSpinTask, "spin", 768, NULL, tskIDLE_PRIORITY, NULL);
    xOk &= xTaskCreate(vDdrTask, "ddr", 512, NULL, 1, NULL);
    xOk &= xTaskCreate(vHeartbeatTask, "heartbeat", 512, NULL, 1, NULL);
    configASSERT(xOk == pdPASS);

    prvPrint("[S] starting scheduler\r\n");
    vTaskStartScheduler();

    /* Only reached if there was not enough heap for the idle task. */
    prvPrint("[S] vTaskStartScheduler returned!\r\n");
    for (;;)
    {
    }
}
