/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * FreeRTOS configuration for the bare-bones S-mode hello-world on U54_1:
 * one task, no hooks, no timers, heap_4. Also included by the port's
 * portASM.S, so anything that is not a plain #define of a number must be
 * inside #ifndef __ASSEMBLER__.
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* Clock / tick. For the RISC-V ports configCPU_CLOCK_HZ is the mtime rate:
 * 1 MHz on PolarFire SoC (RTC toggle clock) and in Renode. */
#define configCPU_CLOCK_HZ                          ( 1000000UL )
#define configTICK_RATE_HZ                          ( 1000UL )

/* CLINT: the port reads mtime from S-mode (the page table must map it);
 * mtimecmp is written by the M-mode shim on SBI set_timer. */
#define configMTIME_BASE_ADDRESS                    ( 0x0200BFF8UL )
#define configMTIMECMP_BASE_ADDRESS                 ( 0x02004000UL )

/* Port. FP state is part of the task context (lp64d). Yield through SBI
 * IPI.send_ipi to this hart (needed on Renode 1.13; 0 on hardware writes
 * sip.SSIP directly). */
#define configENABLE_FPU                            1
#define configENABLE_VPU                            0
#define configISR_STACK_SIZE_WORDS                  ( 512 )
#define configPORT_YIELD_VIA_SBI_IPI                1
#define configSBI_HART_ID                           1

/* Kernel. */
#define configUSE_PREEMPTION                        1
#define configUSE_TIME_SLICING                      1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION     1
#define configUSE_TICKLESS_IDLE                     0
#define configMAX_PRIORITIES                        ( 2 )
#define configMINIMAL_STACK_SIZE                    ( 256 )      /* words */
#define configMAX_TASK_NAME_LEN                     ( 8 )
#define configIDLE_SHOULD_YIELD                     1
#define configUSE_TASK_NOTIFICATIONS                0
#define configQUEUE_REGISTRY_SIZE                   0
#define configENABLE_BACKWARD_COMPATIBILITY         0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS     0

/* 64-bit ticks. Kernels before V10.6 have no TICK_TYPE_WIDTH_* and need
 * configUSE_16_BIT_TICKS instead; V10.6 and later refuse both. */
#ifdef TICK_TYPE_WIDTH_64_BITS
    #define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_64_BITS
#else
    #define configUSE_16_BIT_TICKS                  0
#endif

/* Memory: heap_4 in .bss. */
#define configSUPPORT_STATIC_ALLOCATION             0
#define configSUPPORT_DYNAMIC_ALLOCATION            1
#define configTOTAL_HEAP_SIZE                       ( 16U * 1024U )

/* No hooks, no statistics, no optional objects. */
#define configUSE_IDLE_HOOK                         0
#define configUSE_TICK_HOOK                         0
#define configUSE_MALLOC_FAILED_HOOK                0
#define configCHECK_FOR_STACK_OVERFLOW              0
#define configUSE_DAEMON_TASK_STARTUP_HOOK          0
#define configGENERATE_RUN_TIME_STATS               0
#define configUSE_TRACE_FACILITY                    0
#define configUSE_MUTEXES                           0
#define configUSE_RECURSIVE_MUTEXES                 0
#define configUSE_COUNTING_SEMAPHORES               0
#define configUSE_QUEUE_SETS                        0
#define configUSE_CO_ROUTINES                       0
#define configUSE_TIMERS                            0

#define INCLUDE_vTaskDelay                          1

/* Older kernels (before V11.0): defaults newer kernels provide. The port's
 * portYIELD_FROM_ISR() uses the V11 ISR trace hooks. */
#ifndef traceISR_EXIT
    #define traceISR_EXIT()
#endif
#ifndef traceISR_EXIT_TO_SCHEDULER
    #define traceISR_EXIT_TO_SCHEDULER()
#endif

/* Assert: park the hart (inspect with a debugger). */
#define configASSERT( x )    if( ( x ) == 0 ) { for( ; ; ) { } }

#endif /* FREERTOS_CONFIG_H */
