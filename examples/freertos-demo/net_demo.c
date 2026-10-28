/*******************************************************************************
 * SPDX-License-Identifier: MIT
 *
 * Networking demo: ping and TCP echo between two boards. See net_demo.h.
 */

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_Routing.h"

#include "mpfs_hal/mss_hal.h"
#include "drivers/mss/mss_gpio/mss_gpio.h"

#include "NetworkInterface_LiteEth.h"
#include "net_demo.h"

#define STRAP_GPIO_MASK         (1UL << 0)      /* GPIO2_0 */
#define NET_SUBNET_A            (192U)
#define NET_SUBNET_B            (168U)
#define NET_SUBNET_C            (10U)
#define NET_HOST_BASE           (10U)
#define NET_HOST_PC             (1U)            /* renode/host_bridge.py */

#define PING_ATTEMPTS           (10U)
#define HOST_PING_ATTEMPTS      (4U)
#define PING_TIMEOUT_MS         (500U)
#define PING_BYTES              (32U)
#define TCP_TIMEOUT_MS          (2000U)
#define CONNECT_ATTEMPTS        (5U)

#define SERVER_STACK_WORDS      (configMINIMAL_STACK_SIZE * 4U)
#define CLIENT_STACK_WORDS      (configMINIMAL_STACK_SIZE * 4U)
#define DEMO_TASK_PRIORITY      (2U)

static net_print_t s_print;
static uint8_t s_id;
static uint8_t s_mac[6];
static NetworkInterface_t s_interface;
static NetworkEndPoint_t s_endpoint;
static SemaphoreHandle_t s_net_up = NULL;
static SemaphoreHandle_t s_ping_reply = NULL;
static volatile ePingReplyStatus_t s_ping_status;
static uint8_t s_tx[NET_ECHO_BYTES];
static uint8_t s_rx[NET_ECHO_BYTES];

static uint32_t peer_ip(void)
{
    return FreeRTOS_inet_addr_quick(NET_SUBNET_A, NET_SUBNET_B, NET_SUBNET_C,
                                    NET_HOST_BASE + (1U - s_id));
}

static uint8_t echo_byte(uint32_t i) { return (uint8_t)((i * 31U) ^ (i >> 8) ^ 0x5AU); }

/*------------------------------------------------------------------------------
 * FreeRTOS+TCP hooks
 */
void vApplicationIPNetworkEventHook_Multi(eIPCallbackEvent_t eNetworkEvent,
                                          struct xNetworkEndPoint *pxEndPoint)
{
    (void)pxEndPoint;

    if (eNetworkUp == eNetworkEvent)
    {
        (void)xSemaphoreGive(s_net_up);
    }
}

void vApplicationPingReplyHook(ePingReplyStatus_t eStatus, uint16_t usIdentifier)
{
    (void)usIdentifier;
    s_ping_status = eStatus;
    (void)xSemaphoreGive(s_ping_reply);
}

/* Not a cryptographic source: mtime mixed with an LCG. Good enough for TCP
 * sequence numbers and port numbers in a simulation. */
BaseType_t xApplicationGetRandomNumber(uint32_t *pulNumber)
{
    static uint32_t state = 0U;
    const uint64_t mtime = *(volatile uint64_t *)0x0200BFF8UL;     /* CLINT mtime */

    state = (state * 1664525U) + 1013904223U + (uint32_t)mtime + ((uint32_t)s_id << 24);
    *pulNumber = state;
    return pdTRUE;
}

uint32_t ulApplicationGetNextSequenceNumber(uint32_t ulSourceAddress, uint16_t usSourcePort,
                                            uint32_t ulDestinationAddress,
                                            uint16_t usDestinationPort)
{
    uint32_t r = 0U;

    (void)xApplicationGetRandomNumber(&r);
    return r ^ ulSourceAddress ^ ulDestinationAddress ^
           ((uint32_t)usSourcePort << 16) ^ usDestinationPort;
}

/*------------------------------------------------------------------------------
 * TCP echo server
 */
static void vEchoServerTask(void *pvParameters)
{
    static uint8_t buf[1024];
    struct freertos_sockaddr addr = { 0 };
    const TickType_t timeout = pdMS_TO_TICKS(TCP_TIMEOUT_MS);
    Socket_t listener;

    (void)pvParameters;

    listener = FreeRTOS_socket(FREERTOS_AF_INET, FREERTOS_SOCK_STREAM, FREERTOS_IPPROTO_TCP);
    configASSERT(FREERTOS_INVALID_SOCKET != listener);
    addr.sin_family = FREERTOS_AF_INET;
    addr.sin_port = FreeRTOS_htons(NET_ECHO_PORT);
    (void)FreeRTOS_bind(listener, &addr, sizeof(addr));
    (void)FreeRTOS_listen(listener, 2);

    for (;;)
    {
        struct freertos_sockaddr client;
        socklen_t len = sizeof(client);
        Socket_t s = FreeRTOS_accept(listener, &client, &len);
        uint32_t echoed = 0U;
        BaseType_t n;

        if ((NULL == s) || (FREERTOS_INVALID_SOCKET == s))
        {
            continue;   /* accept timed out */
        }
        (void)FreeRTOS_setsockopt(s, 0, FREERTOS_SO_RCVTIMEO, &timeout, sizeof(timeout));
        (void)FreeRTOS_setsockopt(s, 0, FREERTOS_SO_SNDTIMEO, &timeout, sizeof(timeout));

        while ((n = FreeRTOS_recv(s, buf, sizeof(buf), 0)) > 0)
        {
            if (FreeRTOS_send(s, buf, (size_t)n, 0) != n)
            {
                break;
            }
            echoed += (uint32_t)n;
        }
        s_print("[S] NET: echo server: client 192.168.10.%lu done, %lu bytes echoed\r\n",
                (unsigned long)(FreeRTOS_ntohl(client.sin_address.ulIP_IPv4) & 0xFFU),
                (unsigned long)echoed);
        (void)FreeRTOS_shutdown(s, FREERTOS_SHUT_RDWR);
        (void)FreeRTOS_closesocket(s);
    }
}

/*------------------------------------------------------------------------------
 * Client: ping, then TCP echo with the peer
 */
static BaseType_t ping(uint32_t ip, uint32_t attempts, uint32_t *ms)
{
    for (uint32_t i = 0U; i < attempts; i++)
    {
        const TickType_t t0 = xTaskGetTickCount();

        (void)xSemaphoreTake(s_ping_reply, 0);
        if (pdFAIL == FreeRTOS_SendPingRequest(ip, PING_BYTES, pdMS_TO_TICKS(100U)))
        {
            vTaskDelay(pdMS_TO_TICKS(PING_TIMEOUT_MS));
            continue;
        }
        if ((pdTRUE == xSemaphoreTake(s_ping_reply, pdMS_TO_TICKS(PING_TIMEOUT_MS))) &&
            (eSuccess == s_ping_status))
        {
            *ms = (uint32_t)((xTaskGetTickCount() - t0) * portTICK_PERIOD_MS);
            return pdPASS;
        }
    }
    return pdFAIL;
}

static const char *tcp_echo(uint32_t *ms)
{
    const TickType_t timeout = pdMS_TO_TICKS(TCP_TIMEOUT_MS);
    struct freertos_sockaddr addr = { 0 };
    Socket_t s = FREERTOS_INVALID_SOCKET;
    const char *err = "connect failed";
    TickType_t t0;
    uint32_t sent = 0U;
    uint32_t got = 0U;

    addr.sin_family = FREERTOS_AF_INET;
    addr.sin_port = FreeRTOS_htons(NET_ECHO_PORT);
    addr.sin_address.ulIP_IPv4 = peer_ip();

    for (uint32_t i = 0U; i < NET_ECHO_BYTES; i++)
    {
        s_tx[i] = echo_byte(i);
    }
    (void)memset(s_rx, 0, sizeof(s_rx));

    t0 = xTaskGetTickCount();
    for (uint32_t attempt = 0U; attempt < CONNECT_ATTEMPTS; attempt++)
    {
        s = FreeRTOS_socket(FREERTOS_AF_INET, FREERTOS_SOCK_STREAM, FREERTOS_IPPROTO_TCP);
        if (FREERTOS_INVALID_SOCKET == s)
        {
            return "no socket";
        }
        (void)FreeRTOS_setsockopt(s, 0, FREERTOS_SO_RCVTIMEO, &timeout, sizeof(timeout));
        (void)FreeRTOS_setsockopt(s, 0, FREERTOS_SO_SNDTIMEO, &timeout, sizeof(timeout));
        if (0 == FreeRTOS_connect(s, &addr, sizeof(addr)))
        {
            err = NULL;
            break;
        }
        (void)FreeRTOS_closesocket(s);
        s = FREERTOS_INVALID_SOCKET;
        vTaskDelay(pdMS_TO_TICKS(200U));
    }

    /* Send and receive interleaved, so neither side's windows fill up. */
    while ((NULL == err) && (got < NET_ECHO_BYTES))
    {
        if (sent < NET_ECHO_BYTES)
        {
            BaseType_t n = FreeRTOS_send(s, &s_tx[sent], NET_ECHO_BYTES - sent, 0);

            if (n <= 0)
            {
                err = "send failed";
                break;
            }
            sent += (uint32_t)n;
        }
        BaseType_t n = FreeRTOS_recv(s, &s_rx[got], NET_ECHO_BYTES - got, 0);
        if (n <= 0)
        {
            err = "no echo (receive timed out)";
            break;
        }
        got += (uint32_t)n;
    }
    if ((NULL == err) && (0 != memcmp(s_tx, s_rx, NET_ECHO_BYTES)))
    {
        err = "echo differs from what was sent";
    }
    *ms = (uint32_t)((xTaskGetTickCount() - t0) * portTICK_PERIOD_MS);

    if (FREERTOS_INVALID_SOCKET != s)
    {
        (void)FreeRTOS_shutdown(s, FREERTOS_SHUT_RDWR);
        (void)FreeRTOS_closesocket(s);
    }
    return err;
}

static void vClientTask(void *pvParameters)
{
    const unsigned peer = 1U - s_id;
    liteeth_netif_info_t info;
    uint32_t ms = 0U;
    const char *err;

    (void)pvParameters;

    (void)xSemaphoreTake(s_net_up, portMAX_DELAY);
    liteeth_netif_get_info(&info);
    s_print("[S] NET: up: 192.168.10.%u/24, MAC %02x:%02x:%02x:%02x:%02x:%02x, "
            "PHY id 0x%08lx, link %s\r\n",
            (unsigned)(NET_HOST_BASE + s_id), s_mac[0], s_mac[1], s_mac[2], s_mac[3],
            s_mac[4], s_mac[5], (unsigned long)info.phy_id,
            (pdFALSE != info.link_up) ? "up" : "down");

    if (pdPASS != ping(peer_ip(), PING_ATTEMPTS, &ms))
    {
        s_print("[S] NET: no reply from board %u at 192.168.10.%u (single board?)\r\n",
                peer, (unsigned)(NET_HOST_BASE + peer));
        vTaskDelete(NULL);
    }
    s_print("[S] NET: ping board %u: PASS (%lu ms)\r\n", peer, (unsigned long)ms);

    err = tcp_echo(&ms);
    liteeth_netif_get_info(&info);
    if (NULL == err)
    {
        s_print("[S] NET: TCP echo with board %u: PASS (%u bytes, %lu ms; "
                "MAC tx %lu, rx %lu, dropped %lu)\r\n",
                peer, (unsigned)NET_ECHO_BYTES, (unsigned long)ms,
                (unsigned long)info.mac.tx, (unsigned long)info.mac.rx,
                (unsigned long)(info.mac.rx_dropped + info.rx_no_buffer));
    }
    else
    {
        s_print("[S] NET: TCP echo with board %u: FAIL (%s)\r\n", peer, err);
    }

    /* The host, if renode/host_bridge.py is attached through the
     * SocketBridge model ('make sim-host'). Not a failure without it. */
    if (pdPASS == ping(FreeRTOS_inet_addr_quick(NET_SUBNET_A, NET_SUBNET_B, NET_SUBNET_C, NET_HOST_PC),
                       HOST_PING_ATTEMPTS, &ms))
    {
        s_print("[S] NET: ping host 192.168.10.%u: PASS (%lu ms)\r\n",
                (unsigned)NET_HOST_PC, (unsigned long)ms);
    }
    else
    {
        s_print("[S] NET: no reply from host 192.168.10.%u (no host bridge)\r\n",
                (unsigned)NET_HOST_PC);
    }
    vTaskDelete(NULL);
}

BaseType_t net_demo_start(net_print_t print)
{
    static const uint8_t mask[4] = { 255U, 255U, 255U, 0U };
    static const uint8_t none[4] = { 0U, 0U, 0U, 0U };
    uint8_t ip[4] = { NET_SUBNET_A, NET_SUBNET_B, NET_SUBNET_C, 0U };
    BaseType_t ok = pdPASS;

    s_print = print;
    s_id = (0U != (MSS_GPIO_get_inputs(GPIO2_LO) & STRAP_GPIO_MASK)) ? 1U : 0U;
    s_mac[0] = 0x02U; s_mac[1] = 0x4DU; s_mac[2] = 0x50U;
    s_mac[3] = 0x46U; s_mac[4] = 0x53U; s_mac[5] = s_id;
    ip[3] = (uint8_t)(NET_HOST_BASE + s_id);

    s_net_up = xSemaphoreCreateBinary();
    s_ping_reply = xSemaphoreCreateBinary();
    configASSERT((NULL != s_net_up) && (NULL != s_ping_reply));

    (void)pxLiteEth_FillInterfaceDescriptor(0, &s_interface);
    FreeRTOS_FillEndPoint(&s_interface, &s_endpoint, ip, mask, none, none, s_mac);
    ok &= FreeRTOS_IPInit_Multi();

    ok &= xTaskCreate(vEchoServerTask, "echo", SERVER_STACK_WORDS, NULL, DEMO_TASK_PRIORITY, NULL);
    ok &= xTaskCreate(vClientTask, "client", CLIENT_STACK_WORDS, NULL, DEMO_TASK_PRIORITY, NULL);

    print("[S] NET: board %u, FreeRTOS+TCP on LiteEth, 192.168.10.%u, peer 192.168.10.%u\r\n",
          (unsigned)s_id, (unsigned)ip[3], (unsigned)(NET_HOST_BASE + 1U - s_id));
    return ok;
}
