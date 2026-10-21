/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * FreeRTOS configuration for the S-mode port on a PolarFire SoC U54.
 *
 * This file is also included by portASM.S, so anything that is not plain
 * #define of a number must be inside #ifndef __ASSEMBLER__.
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/*-----------------------------------------------------------
 * Clock / tick
 *----------------------------------------------------------*/

/* For the RISC-V ports configCPU_CLOCK_HZ is the rate of mtime, not the core
 * clock. On PolarFire SoC mtime is clocked by the 1 MHz RTC toggle clock
 * (LIBERO_SETTING_MSS_RTC_TOGGLE_CLK); Renode's CLINT is also 1 MHz. */
#define configCPU_CLOCK_HZ                          ( 1000000UL )
#define configTICK_RATE_HZ                          ( 1000UL )

/* CLINT on PolarFire SoC: base 0x02000000, mtimecmp[hart] at +0x4000,
 * mtime at +0xBFF8. The S-mode port only reads mtime; mtimecmp is written by
 * the M-mode shim on an SBI set_timer call. */
#define configMTIME_BASE_ADDRESS                    ( 0x0200BFF8UL )
#define configMTIMECMP_BASE_ADDRESS                 ( 0x02004000UL )

/*-----------------------------------------------------------
 * Port
 *----------------------------------------------------------*/

/* RV64GC with lp64d: the compiler may use FP registers anywhere, so FP state
 * is part of the task context (saved only when sstatus.FS is dirty). */
#define configENABLE_FPU                            1
#define configENABLE_VPU                            0

/* Dedicated interrupt stack (in words), used by the trap handler after the
 * task context has been saved. */
#define configISR_STACK_SIZE_WORDS                  ( 512 )

/* Yield through SBI IPI.send_ipi to this hart (M-mode sets mip.SSIP) instead
 * of writing sip.SSIP from S-mode. Needed on Renode 1.13 (SoftConsole
 * v2022.2): its RISC-V core does not re-check interrupts after an S-mode
 * write to sip, so a yield is only taken at the next timer edge, up to one
 * tick late. Blocking calls then return before the task has switched out
 * (vTaskDelay() returning at once, list corruption in vListInsert()).
 * Set to 0 on hardware. configSBI_HART_ID must match SMODE_HART. */
#define configPORT_YIELD_VIA_SBI_IPI                1
#define configSBI_HART_ID                           1

/*-----------------------------------------------------------
 * Kernel
 *----------------------------------------------------------*/
#define configUSE_PREEMPTION                        1
#define configUSE_TIME_SLICING                      1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION     1
#define configUSE_TICKLESS_IDLE                     0
#define configMAX_PRIORITIES                        ( 5 )
#define configMINIMAL_STACK_SIZE                    ( 256 )      /* words */
#define configMAX_TASK_NAME_LEN                     ( 12 )
#define configTICK_TYPE_WIDTH_IN_BITS               TICK_TYPE_WIDTH_64_BITS
#define configIDLE_SHOULD_YIELD                     1
#define configUSE_TASK_NOTIFICATIONS                1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES       1
#define configQUEUE_REGISTRY_SIZE                   0
#define configENABLE_BACKWARD_COMPATIBILITY         0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS     0
#define configUSE_MINI_LIST_ITEM                    1
#define configSTACK_DEPTH_TYPE                      size_t
#define configMESSAGE_BUFFER_LENGTH_TYPE            size_t
#define configHEAP_CLEAR_MEMORY_ON_FREE             1
#define configUSE_NEWLIB_REENTRANT                  0

/*-----------------------------------------------------------
 * Memory
 *----------------------------------------------------------*/
#define configSUPPORT_STATIC_ALLOCATION             0
#define configSUPPORT_DYNAMIC_ALLOCATION            1
#define configTOTAL_HEAP_SIZE                       ( 32U * 1024U )
#define configAPPLICATION_ALLOCATED_HEAP            0

/*-----------------------------------------------------------
 * Hooks / debug
 *----------------------------------------------------------*/
#define configUSE_IDLE_HOOK                         0
#define configUSE_TICK_HOOK                         0
#define configUSE_MALLOC_FAILED_HOOK                1
#define configCHECK_FOR_STACK_OVERFLOW              2
#define configUSE_DAEMON_TASK_STARTUP_HOOK          0
#define configGENERATE_RUN_TIME_STATS               0
#define configUSE_TRACE_FACILITY                    0
#define configUSE_STATS_FORMATTING_FUNCTIONS        0
#define configRECORD_STACK_HIGH_ADDRESS             1

/*-----------------------------------------------------------
 * Synchronisation / software timers
 *----------------------------------------------------------*/
#define configUSE_MUTEXES                           1
#define configUSE_RECURSIVE_MUTEXES                 0
#define configUSE_COUNTING_SEMAPHORES               1
#define configUSE_QUEUE_SETS                        0
#define configUSE_CO_ROUTINES                       0
#define configUSE_TIMERS                            0

/*-----------------------------------------------------------
 * API inclusion
 *----------------------------------------------------------*/
#define INCLUDE_vTaskPrioritySet                    1
#define INCLUDE_uxTaskPriorityGet                   1
#define INCLUDE_vTaskDelete                         1
#define INCLUDE_vTaskSuspend                        1
#define INCLUDE_xTaskDelayUntil                     1
#define INCLUDE_vTaskDelay                          1
#define INCLUDE_xTaskGetSchedulerState              1
#define INCLUDE_xTaskGetCurrentTaskHandle           1
#define INCLUDE_uxTaskGetStackHighWaterMark         1
#define INCLUDE_xTaskGetIdleTaskHandle              0
#define INCLUDE_eTaskGetState                       0
#define INCLUDE_xTaskAbortDelay                     0
#define INCLUDE_xTaskGetHandle                      0
#define INCLUDE_xTaskResumeFromISR                  1

#ifndef __ASSEMBLER__
    /* Assert: report on MMUART1 and park (implemented in smode/smode_irq.c). */
    void vAssertCalled( const char * pcFile, unsigned long ulLine );
    #define configASSERT( x )    if( ( x ) == 0 ) vAssertCalled( __FILE__, __LINE__ )
#endif

#endif /* FREERTOS_CONFIG_H */
