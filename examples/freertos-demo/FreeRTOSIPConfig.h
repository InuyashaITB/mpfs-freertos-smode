/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * FreeRTOS+TCP configuration of the demo.
 *
 * LIBRARY-FIXED: the project's top-level Makefile / CMakeLists.txt build
 * libfreertos_tcp with this file (FREERTOS_IP_CONFIG_DIR); it is installed
 * next to FreeRTOS_IP.h and the demo compiles against that copy. Changing it
 * means rebuilding the libraries (the top-level make does).
 *
 * IPv4 only, static addresses (no DHCP / DNS), TCP with sliding windows,
 * ping replies and outgoing pings. Network buffers come from the FreeRTOS
 * heap (BufferAllocation_2), so the heap must be large: the demo puts it in
 * DDR (FreeRTOSConfig.h, ddr_buffers.ld).
 *
 * LiteEth placement (NetworkInterface_LiteEth.h): the Renode demo's FIC3
 * addresses, PHY 1, PLIC source 120 (F2M_2).
 */

#ifndef FREERTOS_IP_CONFIG_H
#define FREERTOS_IP_CONFIG_H

#define ipconfigBYTE_ORDER                          pdFREERTOS_LITTLE_ENDIAN

#define ipconfigUSE_IPv4                            1
#define ipconfigUSE_IPv6                            0
#define ipconfigUSE_DHCP                            0
#define ipconfigUSE_DNS                             0
#define ipconfigUSE_LLMNR                           0
#define ipconfigUSE_NBNS                            0
#define ipconfigUSE_TCP                             1
#define ipconfigUSE_TCP_WIN                         1
#define ipconfigREPLY_TO_INCOMING_PINGS             1
#define ipconfigSUPPORT_OUTGOING_PINGS              1
#define ipconfigUSE_NETWORK_EVENT_HOOK              1

#define ipconfigNETWORK_MTU                         1500
#define ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS      32
#define ipconfigEVENT_QUEUE_LENGTH                  ( ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS + 5 )

/* The driver copies frames (no DMA): no zero-copy, checksums in software. */
#define ipconfigZERO_COPY_RX_DRIVER                 0
#define ipconfigZERO_COPY_TX_DRIVER                 0
#define ipconfigDRIVER_INCLUDED_TX_IP_CHECKSUM      0
#define ipconfigDRIVER_INCLUDED_RX_IP_CHECKSUM      0
#define ipconfigETHERNET_DRIVER_FILTERS_FRAME_TYPES 0
#define ipconfigETHERNET_DRIVER_FILTERS_PACKETS     0

#define ipconfigIP_TASK_PRIORITY                    ( configMAX_PRIORITIES - 2 )
#define ipconfigIP_TASK_STACK_SIZE_WORDS            ( configMINIMAL_STACK_SIZE * 8 )

#define ipconfigARP_CACHE_ENTRIES                   8
#define ipconfigMAX_ARP_RETRANSMISSIONS             5
#define ipconfigMAX_ARP_AGE                         150

#define ipconfigTCP_TIME_TO_LIVE                    64
#define ipconfigUDP_TIME_TO_LIVE                    64

#define ipconfigHAS_DEBUG_PRINTF                    0
#define ipconfigHAS_PRINTF                          0

/* LiteEth soft MAC (NetworkInterface_LiteEth.c). */
#define ipconfigLITEETH_CSR_BASE                    ( 0x40000000UL )
#define ipconfigLITEETH_BUF_BASE                    ( 0x40010000UL )
#define ipconfigLITEETH_MDIO_BASE                   ( 0x40001000UL )
#define ipconfigLITEETH_PHY_ADDR                    ( 1U )
#define ipconfigLITEETH_PLIC_IRQ                    ( 120U )

#endif /* FREERTOS_IP_CONFIG_H */
