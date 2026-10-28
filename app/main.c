/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Bare-bones hello-world: one FreeRTOS task in S-mode on U54_1 that counts.
 * No UART, no peripherals; watch g_count with a debugger.
 */

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "mpfs.h"

/* Sv39 root table, required by the boot code (mpfs.h): one 1 GiB gigapage
 * mapping 0x0000_0000-0x3FFF_FFFF 1:1 (L2LIM with this image, the CLINT),
 * supervisor read/write/execute, accessed and dirty preset. */
const uint64_t mpfs_sv39_root_table[512] __attribute__((aligned(4096))) =
{
    [0] = PTE_GIGAPAGE(0x00000000UL, PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A | PTE_D),
};

volatile uint64_t g_count;

static void count_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        g_count++;
        vTaskDelay(1);
    }
}

void s_mode_main(void)
{
    (void)xTaskCreate(count_task, "count", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    vTaskStartScheduler();
    for (;;)
    {
    }
}
