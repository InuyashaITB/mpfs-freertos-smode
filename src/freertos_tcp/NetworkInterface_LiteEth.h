/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * FreeRTOS+TCP network interface for a LiteEth soft MAC (liteeth.c), for the
 * S-mode FreeRTOS port on PolarFire SoC.
 *
 * Board settings, in FreeRTOSIPConfig.h (defaults: the Renode demo's FIC3
 * placement, see renode/README.md):
 *   ipconfigLITEETH_CSR_BASE    0x40000000   CSRs
 *   ipconfigLITEETH_BUF_BASE    0x40010000   slot buffers
 *   ipconfigLITEETH_MDIO_BASE   0x40001000   bit-banged MDIO
 *   ipconfigLITEETH_PHY_ADDR    1            PHY address on MDIO
 *   ipconfigLITEETH_PLIC_IRQ    120          PLIC source of the MAC's IRQ
 *                                            (F2M_2)
 * The addresses must be mapped: the library's Sv39 table identity maps the
 * fabric windows (gigapage 1, 0x4000_0000-0x7FFF_FFFF).
 *
 * Use:
 *   static NetworkInterface_t xInterface;
 *   static NetworkEndPoint_t xEndPoint;
 *   pxLiteEth_FillInterfaceDescriptor(0, &xInterface);
 *   FreeRTOS_FillEndPoint(&xInterface, &xEndPoint, ip, mask, gw, dns, mac);
 *   FreeRTOS_IPInit_Multi();
 * and route the MAC's PLIC source to liteeth_netif_irq_handler(), e.g.
 *   uint8_t fabric_f2h_2_plic_IRQHandler(void)
 *   { return liteeth_netif_irq_handler(); }
 * The interface enables that source for the S context itself.
 */

#ifndef NETWORK_INTERFACE_LITEETH_H_
#define NETWORK_INTERFACE_LITEETH_H_

#include <stdint.h>

#include "FreeRTOS.h"
#include "FreeRTOS_IP.h"
#include "liteeth.h"

NetworkInterface_t *pxLiteEth_FillInterfaceDescriptor(BaseType_t xEMACIndex,
                                                      NetworkInterface_t *pxInterface);

/* PLIC handler body for the MAC's interrupt: wakes the receive task and
 * returns EXT_IRQ_DISABLE (the line stays high until the frames are taken;
 * the task enables the source again). */
uint8_t liteeth_netif_irq_handler(void);

typedef struct
{
    uint32_t phy_id;            /* ID1 << 16 | ID2; 0 or 0xFFFFFFFF: no PHY */
    BaseType_t link_up;         /* last link state read (pdTRUE without a PHY) */
    liteeth_stats_t mac;        /* frame counters */
    uint32_t rx_no_buffer;      /* frames dropped: no network buffer */
} liteeth_netif_info_t;

void liteeth_netif_get_info(liteeth_netif_info_t *info);

#endif /* NETWORK_INTERFACE_LITEETH_H_ */
