/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * FreeRTOS+TCP network interface for a LiteEth soft MAC. See
 * NetworkInterface_LiteEth.h.
 *
 * Receive: the MAC's interrupt (level, PLIC) wakes prvRxTask through
 * liteeth_netif_irq_handler(), which disables the source. The task copies
 * every pending frame into a network buffer, hands it to the IP task, and
 * enables the source again. The PLIC re-latches the still-high line as pending
 * when the interrupt is completed, so one more wake-up may find nothing.
 *
 * Transmit: the IP task copies the frame into a TX slot (liteeth_send).
 */

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "FreeRTOS_IP.h"
#include "FreeRTOS_IP_Private.h"
#include "NetworkInterface.h"
#include "NetworkBufferManagement.h"

#include "mpfs_hal/mss_hal.h"
#include "smode/smode_irq.h"
#include "smode/smode_plic.h"

#include "NetworkInterface_LiteEth.h"

#ifndef ipconfigLITEETH_CSR_BASE
    #define ipconfigLITEETH_CSR_BASE        ( 0x40000000UL )
#endif
#ifndef ipconfigLITEETH_BUF_BASE
    #define ipconfigLITEETH_BUF_BASE        ( 0x40010000UL )
#endif
#ifndef ipconfigLITEETH_MDIO_BASE
    #define ipconfigLITEETH_MDIO_BASE       ( 0x40001000UL )
#endif
#ifndef ipconfigLITEETH_PHY_ADDR
    #define ipconfigLITEETH_PHY_ADDR        ( 1U )
#endif
#ifndef ipconfigLITEETH_PLIC_IRQ
    #define ipconfigLITEETH_PLIC_IRQ        ( 120U )    /* F2M_2 */
#endif
#ifndef ipconfigLITEETH_PLIC_PRIORITY
    #define ipconfigLITEETH_PLIC_PRIORITY   ( 2U )
#endif
#ifndef ipconfigLITEETH_RX_TASK_STACK_WORDS
    #define ipconfigLITEETH_RX_TASK_STACK_WORDS     ( configMINIMAL_STACK_SIZE * 4U )
#endif
#ifndef ipconfigLITEETH_RX_TASK_PRIORITY
    #define ipconfigLITEETH_RX_TASK_PRIORITY        ( configMAX_PRIORITIES - 1 )
#endif

/* Clause 22 PHY registers. */
#define PHY_REG_BMSR            ( 1U )
#define PHY_REG_ID1             ( 2U )
#define PHY_REG_ID2             ( 3U )
#define PHY_BMSR_LINK           ( 1U << 2 )

/* Wait for a network buffer before a received frame is dropped. */
#define RX_BUFFER_WAIT_TICKS    ( pdMS_TO_TICKS( 20U ) )

/* Wake-ups with no interrupt, so a lost edge cannot stall reception. */
#define RX_POLL_TICKS           ( pdMS_TO_TICKS( 100U ) )

static NetworkInterface_t * pxMyInterface = NULL;
static TaskHandle_t xRxTask = NULL;
static uint32_t ulPhyId = 0U;
static BaseType_t xLinkUp = pdFALSE;
static volatile uint32_t ulRxNoBuffer = 0U;

static BaseType_t prvHasPhy( void )
{
    return ( ( ulPhyId != 0U ) && ( ulPhyId != 0xFFFFFFFFUL ) ) ? pdTRUE : pdFALSE;
}

static BaseType_t prvReadLink( void )
{
    if( prvHasPhy() == pdFALSE )
    {
        return pdTRUE;  /* no PHY to ask: the link is assumed up */
    }

    /* The link bit latches low: read twice for the current state. */
    ( void ) liteeth_mdio_read( ipconfigLITEETH_PHY_ADDR, PHY_REG_BMSR );
    return ( ( liteeth_mdio_read( ipconfigLITEETH_PHY_ADDR, PHY_REG_BMSR ) & PHY_BMSR_LINK ) != 0U ) ? pdTRUE : pdFALSE;
}

uint8_t liteeth_netif_irq_handler( void )
{
    BaseType_t xWoken = pdFALSE;

    if( xRxTask != NULL )
    {
        vTaskNotifyGiveFromISR( xRxTask, &xWoken );
    }
    vSmodeYieldFromISR( xWoken );
    return ( uint8_t ) EXT_IRQ_DISABLE;
}

static void prvRxTask( void * pvParameters )
{
    ( void ) pvParameters;

    for( ; ; )
    {
        uint32_t ulLength = 0U;
        liteeth_status_t xStatus;

        ( void ) ulTaskNotifyTake( pdTRUE, RX_POLL_TICKS );

        while( ( xStatus = liteeth_receive_begin( &ulLength ) ) != LITEETH_NONE )
        {
            NetworkBufferDescriptor_t * pxBuffer = NULL;
            uint8_t ucCopied = 0U;

            if( ( xStatus == LITEETH_OK ) && ( ulLength >= ipSIZE_OF_ETH_HEADER ) )
            {
                pxBuffer = pxGetNetworkBufferWithDescriptor( ulLength, RX_BUFFER_WAIT_TICKS );
            }

            if( pxBuffer != NULL )
            {
                liteeth_receive_copy( pxBuffer->pucEthernetBuffer, ulLength );
                ucCopied = 1U;
            }
            else if( xStatus == LITEETH_OK )
            {
                ulRxNoBuffer++;
            }

            liteeth_receive_end( ucCopied );

            if( pxBuffer != NULL )
            {
                IPStackEvent_t xRxEvent;

                pxBuffer->xDataLength = ulLength;
                pxBuffer->pxInterface = pxMyInterface;
                pxBuffer->pxEndPoint = FreeRTOS_MatchingEndpoint( pxMyInterface, pxBuffer->pucEthernetBuffer );

                if( pxBuffer->pxEndPoint == NULL )
                {
                    vReleaseNetworkBufferAndDescriptor( pxBuffer );
                    continue;
                }

                xRxEvent.eEventType = eNetworkRxEvent;
                xRxEvent.pvData = ( void * ) pxBuffer;

                if( xSendEventStructToIPTask( &xRxEvent, 0 ) == pdFAIL )
                {
                    vReleaseNetworkBufferAndDescriptor( pxBuffer );
                    iptraceETHERNET_RX_EVENT_LOST();
                }
                else
                {
                    iptraceNETWORK_INTERFACE_RECEIVE();
                }
            }
        }

        SPLIC_enable( ( PLIC_IRQn_Type ) ipconfigLITEETH_PLIC_IRQ );
    }
}

static BaseType_t xLiteEth_Initialise( NetworkInterface_t * pxInterface )
{
    ( void ) pxInterface;

    if( xRxTask == NULL )
    {
        liteeth_init( ipconfigLITEETH_CSR_BASE, ipconfigLITEETH_BUF_BASE, ipconfigLITEETH_MDIO_BASE );

        ulPhyId = ( ( uint32_t ) liteeth_mdio_read( ipconfigLITEETH_PHY_ADDR, PHY_REG_ID1 ) << 16 ) |
                  ( uint32_t ) liteeth_mdio_read( ipconfigLITEETH_PHY_ADDR, PHY_REG_ID2 );

        if( xTaskCreate( prvRxTask, "EthRx", ipconfigLITEETH_RX_TASK_STACK_WORDS, NULL,
                         ipconfigLITEETH_RX_TASK_PRIORITY, &xRxTask ) != pdPASS )
        {
            return pdFAIL;
        }

        PLIC_SetPriority( ( PLIC_IRQn_Type ) ipconfigLITEETH_PLIC_IRQ, ipconfigLITEETH_PLIC_PRIORITY );
        SPLIC_enable( ( PLIC_IRQn_Type ) ipconfigLITEETH_PLIC_IRQ );
    }

    /* The IP task calls this again until it returns pdPASS. */
    xLinkUp = prvReadLink();
    return xLinkUp;
}

static BaseType_t xLiteEth_Output( NetworkInterface_t * pxInterface,
                                   NetworkBufferDescriptor_t * const pxBuffer,
                                   BaseType_t xReleaseAfterSend )
{
    BaseType_t xReturn = pdFAIL;

    ( void ) pxInterface;

    if( liteeth_send( pxBuffer->pucEthernetBuffer, ( uint32_t ) pxBuffer->xDataLength ) == LITEETH_OK )
    {
        iptraceNETWORK_INTERFACE_TRANSMIT();
        xReturn = pdPASS;
    }

    if( xReleaseAfterSend != pdFALSE )
    {
        vReleaseNetworkBufferAndDescriptor( pxBuffer );
    }

    return xReturn;
}

static BaseType_t xLiteEth_GetPhyLinkStatus( NetworkInterface_t * pxInterface )
{
    ( void ) pxInterface;

    xLinkUp = prvReadLink();
    return xLinkUp;
}

NetworkInterface_t * pxLiteEth_FillInterfaceDescriptor( BaseType_t xEMACIndex,
                                                        NetworkInterface_t * pxInterface )
{
    static char pcName[] = "liteeth0";

    configASSERT( pxMyInterface == NULL );
    pxMyInterface = pxInterface;

    memset( pxInterface, 0, sizeof( *pxInterface ) );
    pxInterface->pcName = pcName;
    pxInterface->pvArgument = ( void * ) xEMACIndex;
    pxInterface->pfInitialise = xLiteEth_Initialise;
    pxInterface->pfOutput = xLiteEth_Output;
    pxInterface->pfGetPhyLinkStatus = xLiteEth_GetPhyLinkStatus;

    FreeRTOS_AddNetworkInterface( pxInterface );

    return pxInterface;
}

void liteeth_netif_get_info( liteeth_netif_info_t * info )
{
    info->phy_id = ulPhyId;
    info->link_up = xLinkUp;
    liteeth_get_stats( &info->mac );
    info->rx_no_buffer = ulRxNoBuffer;
}
