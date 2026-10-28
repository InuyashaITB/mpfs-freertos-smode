/*
 * FreeRTOS Kernel V11.3.2 - RISC-V supervisor-mode (S-mode) port
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 * Chip specific extensions for the PolarFire SoC U54 (RV64GC) running in
 * S-mode. The U54 has no additional registers that need saving, so this is
 * the same as RISCV_no_extensions.
 */

#ifndef __FREERTOS_RISC_V_EXTENSIONS_H__
#define __FREERTOS_RISC_V_EXTENSIONS_H__

#define portasmADDITIONAL_CONTEXT_SIZE    0 /* Must be even number on 32-bit cores. */

.macro portasmSAVE_ADDITIONAL_REGISTERS
    /* No additional registers to save, so this macro does nothing. */
    .endm

.macro portasmRESTORE_ADDITIONAL_REGISTERS
    /* No additional registers to restore, so this macro does nothing. */
    .endm

#endif /* __FREERTOS_RISC_V_EXTENSIONS_H__ */
