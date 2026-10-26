/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Diagnostic output and fatal-fault reporting shared by the M-mode shim, the
 * S-mode start-up code and the OS glue. The output itself is the
 * application's smode_diag_puts(); the weak default discards it, so the
 * library does not depend on any UART driver.
 *
 * Runs in M-mode and in S-mode: no floating point, no stored pointers.
 */

#include "smode/mpfs_smode.h"

volatile smode_fault_t g_smode_fault;

__attribute__((weak)) void smode_diag_puts(const char *s)
{
    (void)s;
}

/* Weak default: no OS, keep smode_early_trap as the S-mode trap handler. */
__attribute__((weak)) void smode_os_init(void)
{
}

/* Weak default: no M-mode bring-up beyond what the library does. */
__attribute__((weak)) void smode_m_prehook(void)
{
}

void smode_diag_puthex(uintptr_t v)
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
    smode_diag_puts(buf);
}

void smode_diag_putdec(unsigned long v)
{
    char buf[21];
    int i = 20;

    buf[i] = '\0';
    do
    {
        buf[--i] = (char)('0' + (v % 10U));
        v /= 10U;
    } while ((v != 0U) && (i > 0));
    smode_diag_puts(&buf[i]);
}

void smode_fatal(const char *what, uintptr_t scause, uintptr_t sepc, uintptr_t stval)
{
    uintptr_t sstatus;

    __asm volatile ("csrc sstatus, %0" :: "r"(2UL));       /* SIE = 0 */
    __asm volatile ("csrr %0, sstatus" : "=r"(sstatus));

    g_smode_fault.scause = scause;
    g_smode_fault.sepc = sepc;
    g_smode_fault.stval = stval;
    g_smode_fault.sstatus = sstatus;

    smode_diag_puts("\r\n*** ");
    smode_diag_puts(what);
    smode_diag_puts(" scause=");
    smode_diag_puthex(scause);
    smode_diag_puts(" sepc=");
    smode_diag_puthex(sepc);
    smode_diag_puts(" stval=");
    smode_diag_puthex(stval);
    smode_diag_puts(" sstatus=");
    smode_diag_puthex(sstatus);
    smode_diag_puts("\r\n");

    for (;;)
    {
        __asm volatile ("wfi");
    }
}

/* Called from smode_start.S on a trap before the OS installs its handler,
 * with the MMU either still off or just switched on. */
void smode_early_trap_report(uintptr_t scause, uintptr_t sepc, uintptr_t stval);
void smode_early_trap_report(uintptr_t scause, uintptr_t sepc, uintptr_t stval)
{
    smode_fatal("S-mode early trap (MMU bring-up)", scause, sepc, stval);
}
