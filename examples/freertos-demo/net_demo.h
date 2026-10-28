/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Networking demo on FreeRTOS+TCP (libfreertos_tcp) and the LiteEth soft MAC.
 *
 * Every board runs the same code; the GPIO2_0 strap gives its id (0 or 1):
 *   MAC  02:4d:50:46:53:<id>      IP  192.168.10.<10 + id>/24
 * and the peer is the other id. Two tasks:
 *   echo server  TCP port 7, echoes whatever a client sends
 *   client       once the network is up: pings the peer, then connects to
 *                its echo server, sends NET_ECHO_BYTES and checks the echo
 * Results, the same on both boards (make sim checks them):
 *   [S] NET: ping board <peer>: PASS (...)
 *   [S] NET: TCP echo with board <peer>: PASS (...)
 * With no peer (a single board, as with the SoftConsole launcher) the client
 * reports "no reply from board <peer> (single board?)", which is not a
 * failure. In Renode the MAC model's address must equal the board's MAC (see
 * renode/README.md).
 */

#ifndef NET_DEMO_H_
#define NET_DEMO_H_

#include "FreeRTOS.h"

#define NET_ECHO_PORT       (7U)
#define NET_ECHO_BYTES      (4096U)

/* printf-style output (task context). */
typedef void (*net_print_t)(const char *fmt, ...);

/* Creates the interface, the end point, the IP task and the demo tasks.
 * Call before vTaskStartScheduler(). */
BaseType_t net_demo_start(net_print_t print);

#endif /* NET_DEMO_H_ */
