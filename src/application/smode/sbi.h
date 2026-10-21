/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Minimal SBI (RISC-V Supervisor Binary Interface) definitions shared by the
 * M-mode shim (m_shim.c) and S-mode code.
 *
 * Calling convention (SBI v0.2+): a7 = extension ID (EID), a6 = function ID
 * (FID), a0..a5 = arguments. On return a0 = error code, a1 = value.
 *
 * Only what this project needs is implemented by the shim:
 *   BASE   (0x10)        get_spec_version, get_impl_id, probe_extension
 *   TIME   (0x54494D45)  set_timer
 *   IPI    (0x735049)    send_ipi, to the calling hart only (sets SSIP)
 *   legacy (0x00)        set_timer
 *   legacy (0x01)        console_putchar (to MMUART1, polled)
 */

#ifndef SMODE_SBI_H_
#define SMODE_SBI_H_

#define SBI_EXT_LEGACY_SET_TIMER        0x00UL
#define SBI_EXT_LEGACY_CONSOLE_PUTCHAR  0x01UL
#define SBI_EXT_BASE                    0x10UL
#define SBI_EXT_TIME                    0x54494D45UL
#define SBI_EXT_IPI                     0x735049UL

#define SBI_BASE_GET_SPEC_VERSION       0UL
#define SBI_BASE_GET_IMPL_ID            1UL
#define SBI_BASE_GET_IMPL_VERSION       2UL
#define SBI_BASE_PROBE_EXTENSION        3UL

#define SBI_TIME_SET_TIMER              0UL

#define SBI_IPI_SEND_IPI                0UL

#define SBI_SUCCESS                     0L
#define SBI_ERR_NOT_SUPPORTED           (-2L)
#define SBI_ERR_INVALID_PARAM           (-3L)

#ifndef __ASSEMBLER__
#include <stdint.h>

typedef struct
{
    long error;
    long value;
} sbi_ret_t;

/* S-mode helper: issue an SBI call. Only valid when running in S-mode. */
static inline sbi_ret_t sbi_call(uintptr_t eid, uintptr_t fid,
                                 uintptr_t arg0, uintptr_t arg1)
{
    register uintptr_t a0 __asm__ ("a0") = arg0;
    register uintptr_t a1 __asm__ ("a1") = arg1;
    register uintptr_t a6 __asm__ ("a6") = fid;
    register uintptr_t a7 __asm__ ("a7") = eid;

    __asm__ volatile ("ecall"
                      : "+r"(a0), "+r"(a1)
                      : "r"(a6), "r"(a7)
                      : "memory");

    sbi_ret_t ret = { (long)a0, (long)a1 };
    return ret;
}

static inline void sbi_console_putchar(char c)
{
    (void)sbi_call(SBI_EXT_LEGACY_CONSOLE_PUTCHAR, 0UL, (uintptr_t)(uint8_t)c, 0UL);
}
#endif /* __ASSEMBLER__ */

#endif /* SMODE_SBI_H_ */
