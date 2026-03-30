/*
 * test.c for xGW Multicore Project
 * Based on ccu_ti reference - uses TI-specific LwipifEnetApp interface
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* lwIP core includes */
#include "lwip/opt.h"
#include "lwip/sys.h"
#include "lwip/timeouts.h"
#include "lwip/init.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/udp.h"

/* lwIP netif includes */
#include "lwip/etharp.h"
#include "netif/ethernet.h"

/* TI ENET lwIP interface - REQUIRED for CPSW driver */
#include "ti_enet_config.h"
#include "ti_enet_lwipif.h"

/* xGW UDP interface */
#include "enet/xgw_udp_interface.h"

#ifndef LWIP_EXAMPLE_APP_ABORT
#define LWIP_EXAMPLE_APP_ABORT() 0
#endif

/* Define to enable ethernet */
#ifndef USE_ETHERNET
#define USE_ETHERNET  1
#endif

/* Static IP configuration - Board IP: 192.168.1.9, PC: 192.168.1.3 */
#define USE_DHCP    0  /* Static IP */
#define USE_AUTOIP  0

#if (!(USE_DHCP || USE_AUTOIP)) /* Static IP */
#define IP_ADDR_POOL_COUNT  (1U)

const ip_addr_t gStaticIP[IP_ADDR_POOL_COUNT] = {
    IPADDR4_INIT_BYTES(192, 168, 1, 9),   /* Board IP */
};

const ip_addr_t gStaticIPGateway[IP_ADDR_POOL_COUNT] = {
    IPADDR4_INIT_BYTES(192, 168, 1, 1),   /* Gateway */
};

const ip_addr_t gStaticIPNetmask[IP_ADDR_POOL_COUNT] = {
    IPADDR4_INIT_BYTES(255, 255, 255, 0), /* Netmask */
};
#endif

/* Handle to the Application interface for the LwipIf Layer */
LwipifEnetApp_Handle hlwipIfApp = NULL;

/* Global netif pointers - must use array for TI interface */
#if LWIP_IPV4
struct netif *g_netif[ENET_SYSCFG_NETIF_COUNT] = {NULL};
#endif

/* Forward declarations */
static void test_netif_init(void);
static void apps_init(void);
static void test_init(void *arg);
void main_loop(void *a0);

/* test_netif_init - Initialize network interface with TI-specific API */
static void test_netif_init(void)
{
#if LWIP_IPV4 && USE_ETHERNET
    ip4_addr_t ipaddr, netmask, gw;
    uint32_t i;

    ip4_addr_set_zero(&gw);
    ip4_addr_set_zero(&ipaddr);
    ip4_addr_set_zero(&netmask);

    /* Get TI ENET lwIP interface handle */
    hlwipIfApp = LwipifEnetApp_getHandle();
    if (hlwipIfApp == NULL) {
        DebugP_log("[ERROR] LwipifEnetApp_getHandle() failed!\r\n");
        return;
    }

    /* Open netif for each interface */
    for (i = 0U; i < ENET_SYSCFG_NETIF_COUNT; i++) {
#if !(USE_DHCP || USE_AUTOIP) /* Static IP */
        /* Set static IP address */
        LWIP_ASSERT("More IP allocated than reserved", i < IP_ADDR_POOL_COUNT);
        ip4_addr_set(&ipaddr, &gStaticIP[i]);
        ip4_addr_set(&gw, &gStaticIPGateway[i]);
        ip4_addr_set(&netmask, &gStaticIPNetmask[i]);

        DebugP_log("[%d] Starting lwIP, IP: %s\r\n", i, ip4addr_ntoa(&ipaddr));
#endif

        /* Open netif using TI-specific API - THIS IS CRITICAL! */
        g_netif[i] = LwipifEnetApp_netifOpen(hlwipIfApp, NETIF_INST_ID0 + i,
                                              &ipaddr, &netmask, &gw);
        if (g_netif[i] == NULL) {
            DebugP_log("[ERROR] LwipifEnetApp_netifOpen(%d) failed!\r\n", i);
            continue;
        }

        DebugP_log("[%d] Netif opened: %c%c%d\r\n", i,
                   g_netif[i]->name[0], g_netif[i]->name[1], g_netif[i]->num);
    }

    /* Start packet processing scheduler - CRITICAL for RX to work! */
    if (g_netif[NETIF_INST_ID0] != NULL) {
        LwipifEnetApp_startSchedule(hlwipIfApp, g_netif[NETIF_INST_ID0]);
        DebugP_log("[NETIF] Packet processing scheduler started\r\n");
    }

    /* Bring netif up */
    for (i = 0U; i < ENET_SYSCFG_NETIF_COUNT; i++) {
        if (g_netif[i] != NULL) {
            netif_set_up(g_netif[i]);
            netif_set_link_up(g_netif[i]);

            DebugP_log("[%d] netif up: IP=%s, up=%d, link=%d\r\n", i,
                       ip4addr_ntoa(netif_ip4_addr(g_netif[i])),
                       netif_is_up(g_netif[i]),
                       netif_is_link_up(g_netif[i]));
        }
    }
#endif /* LWIP_IPV4 && USE_ETHERNET */
}

/* apps_init - Minimal apps initialization */
static void apps_init(void)
{
    /* No additional apps needed for xGW */
}

/* test_init - Called by tcpip_init() when tcpip thread is ready */
static void test_init(void *arg)
{
#if !NO_SYS
    sys_sem_t *init_sem = (sys_sem_t*)arg;
    LWIP_ASSERT("arg != NULL", arg != NULL);
#endif

    DebugP_log("[Core0] lwIP tcpip thread ready, initializing network...\r\n");

    /* init randomizer */
    srand((unsigned int)sys_now() / 1000);

    /* init network interfaces */
    test_netif_init();

    /* init apps (empty for xGW) */
    apps_init();

    /* Start xGW UDP interface */
    DebugP_log("[Core0] Starting xGW UDP interface...\r\n");

    /* Set PC IP address for UDP TX */
    uint8_t pc_ip[4] = {192, 168, 1, 3};  /* 192.168.1.3 */
    xgw_udp_set_pc_ip(pc_ip);

    int32_t status = xgw_udp_start();
    if (status != 0) {
        DebugP_log("[Core0] ERROR: xGW UDP interface start failed!\r\n");
    } else {
        DebugP_log("[Core0] xGW UDP interface started successfully\r\n");
        DebugP_log("[Core0] UDP RX Port: %d (PC -> xGW)\r\n", XGW_UDP_RX_PORT);
        DebugP_log("[Core0] UDP TX Port: %d (xGW -> PC)\r\n", XGW_UDP_TX_PORT);
    }

#if !NO_SYS
    sys_sem_signal(init_sem);
#endif
}

/* main_loop - Main loop for lwIP processing */
void main_loop(void *a0)
{
#if !NO_SYS
    err_t err;
    sys_sem_t init_sem;
#endif

    DebugP_log("[Core0] main_loop entry, initializing lwIP...\r\n");

    /* initialize lwIP stack, network interfaces and applications */
#if NO_SYS
    lwip_init();
    test_init(NULL);
#else /* NO_SYS */
    err = sys_sem_new(&init_sem, 0);
    LWIP_ASSERT("failed to create init_sem", err == ERR_OK);
    LWIP_UNUSED_ARG(err);

    /* tcpip_init() creates tcpip thread and calls test_init() callback */
    tcpip_init(test_init, &init_sem);

    /* wait for initialization to finish */
    sys_sem_wait(&init_sem);
    sys_sem_free(&init_sem);
#endif /* NO_SYS */

#if (LWIP_SOCKET || LWIP_NETCONN) && LWIP_NETCONN_SEM_PER_THREAD
    netconn_thread_init();
#endif

    DebugP_log("[Core0] lwIP init complete, entering main loop...\r\n");

    /* MAIN LOOP for driver update */
    uint32_t loop_count = 0;
    while (!LWIP_EXAMPLE_APP_ABORT()) {
#if NO_SYS
        /* handle timers (already done in tcpip.c when NO_SYS=0) */
        sys_check_timeouts();
#endif /* NO_SYS */

#if USE_ETHERNET
        sys_msleep(1);

        /* Print status every 5 seconds */
        if (++loop_count >= 5000) {
            loop_count = 0;
#if LWIP_IPV4
            if (g_netif[NETIF_INST_ID0] != NULL) {
                struct netif *pNetif = g_netif[NETIF_INST_ID0];
                DebugP_log("[NETIF-STAT] IP=%s, up=%d, link=%d\r\n",
                           ip4addr_ntoa(netif_ip4_addr(pNetif)),
                           netif_is_up(pNetif),
                           netif_is_link_up(pNetif));
            }
#endif
        }
#endif /* USE_ETHERNET */

#if ENABLE_LOOPBACK && !LWIP_NETIF_LOOPBACK_MULTITHREADING
        /* check for loopback packets on all netifs */
        netif_poll_all();
#endif /* ENABLE_LOOPBACK && !LWIP_NETIF_LOOPBACK_MULTITHREADING */
    }

    DebugP_log("[Core0] main_loop exit!\r\n");
}
