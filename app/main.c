/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Bare-bones hello-world: one FreeRTOS task in S-mode on U54_1 that counts,
 * and on each count writes and reads back a word in DDR through the Sv39
 * mapping below. No UART, no peripherals; watch g_count, g_ddr_ok and
 * g_ddr_bad with a debugger.
 */

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "mpfs.h"

/* DDR as this application sees it: 1 GiB of the cached window, mapped at the
 * gigapage after the L2LIM alias. */
#define DDR_VA          (MPFS_ONCHIP_VA_OFFSET + 0x40000000UL)  /* 0xFFFFFFC0_4000_0000 */
#define DDR_SIZE        (0x40000000UL)

/* Sv39 root table, required by the boot code (mpfs.h). Three 1 GiB gigapages:
 *   entry 0    VA 0x0000_0000-0x3FFF_FFFF, 1:1 onto PA 0 (L2LIM with this image
 *              for the switch to the alias, the CLINT mtime)
 *   entry 256  VA 0xFFFFFFC0_0000_0000-..., onto PA 0: the high-half alias the
 *              kernel runs from (VA = PA + MPFS_ONCHIP_VA_OFFSET)
 *   entry 257  VA 0xFFFFFFC0_4000_0000-..., onto PA 0x10_0000_0000: DDR (data,
 *              not executable)
 * Supervisor access, global, accessed and dirty preset. */
#define ONCHIP_FLAGS    (PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A | PTE_D)
#define DDR_FLAGS       (PTE_V | PTE_R | PTE_W | PTE_G | PTE_A | PTE_D)
#define SV39_VPN2(va)   (((va) >> 30) & 0x1FFU)

const uint64_t mpfs_sv39_root_table[512] __attribute__((aligned(4096))) =
{
    [0] = PTE_GIGAPAGE(0x00000000UL, ONCHIP_FLAGS),
    [SV39_VPN2(MPFS_ONCHIP_VA_OFFSET)] = PTE_GIGAPAGE(0x00000000UL, ONCHIP_FLAGS),
    [SV39_VPN2(DDR_VA)] = PTE_GIGAPAGE(MPFS_DDR_CACHED_PA, DDR_FLAGS),
};

volatile uint64_t g_count;
volatile uint64_t g_ddr_ok;         /* DDR words written and read back intact */
volatile uint64_t g_ddr_bad;        /* ... read back different */
volatile uint64_t g_ddr_last_va;    /* the last DDR address written */

/* One word per count, 1 MiB further each time: the whole 1 GiB in 1024
 * counts. The value is the count with a fixed pattern mixed in. */
static void ddr_check(uint64_t count)
{
    volatile uint64_t *word = (volatile uint64_t *)(DDR_VA + ((count * 0x100000UL) % DDR_SIZE));
    const uint64_t value = count ^ 0xA5A5A5A55A5A5A5AUL;

    *word = value;
    g_ddr_last_va = (uint64_t)(uintptr_t)word;
    if (*word == value)
    {
        g_ddr_ok++;
    }
    else
    {
        g_ddr_bad++;
    }
}

static void count_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        g_count++;
        ddr_check(g_count);
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
