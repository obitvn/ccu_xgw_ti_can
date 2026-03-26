/*
 * Minimal test.c for xGW Multicore Project
 * Only contains essential lwIP initialization and xGW UDP start
 */

#include <stdio.h>
#include <kernel/dpl/DebugP.h>
#include <string.h>

/* lwIP core includes */
#include "lwip/opt.h"
#include "lwip/sys.h"
#include "lwip/timeouts.h"
#include "lwip/init.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"

/* lwIP netif includes */
#include "lwip/etharp.h"
#include "netif/ethernet.h"

/* SDK includes - same as ccu_ti */
#include "ti_enet_config.h"

/* xGW UDP interface */
#include "enet/xgw_udp_interface.h"

#ifndef LWIP_EXAMPLE_APP_ABORT
#define LWIP_EXAMPLE_APP_ABORT() 0
#endif

/* Forward declarations */
static void test_init(void *arg);
void main_loop(void *a0);

/* Network interface structure */
static struct netif g_netif;

/* test_netif_init - Initialize network interface with static IP */
static void test_netif_init(void)
{
#if LWIP_IPV4 && USE_ETHERNET
    ip4_addr_t ipaddr, netmask, gw;

    /* Use static IP configuration */
    IP4_ADDR(&ipaddr,  192, 168, 1, 100);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gw,      192, 168, 1, 1);

    /* Add network interface */
    netif_add(&g_netif, &ipaddr, &netmask, &gw,
              NULL, ethernetif_init, ethernet_input);

    /* Set the network interface as default */
    netif_set_default(&g_netif);

    /* Bring the network interface up */
    netif_set_up(&g_netif);
    netif_set_link_up(&g_netif);
#endif /* LWIP_IPV4 && USE_ETHERNET */
}

/* apps_init - Minimal apps initialization (empty for xGW) */
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

    /* init randomizer */
    srand((unsigned int)sys_now() / 1000);

    /* init network interfaces */
    test_netif_init();

    /* init apps (empty for xGW) */
    apps_init();

    /* Start xGW UDP interface */
    DebugP_log("[Core0] Starting xGW UDP interface...\r\n");
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

    /* initialize lwIP stack, network interfaces and applications */
#if NO_SYS
    lwip_init();
    test_init(NULL);
#else /* NO_SYS */
    err = sys_sem_new(&init_sem, 0);
    LWIP_ASSERT("failed to create init_sem", err == ERR_OK);
    LWIP_UNUSED_ARG(err);
    tcpip_init(test_init, &init_sem);
    /* we have to wait for initialization to finish before
     * calling update_adapter()! */
    sys_sem_wait(&init_sem);
    sys_sem_free(&init_sem);
#endif /* NO_SYS */

#if (LWIP_SOCKET || LWIP_NETCONN) && LWIP_NETCONN_SEM_PER_THREAD
    netconn_thread_init();
#endif

    /* MAIN LOOP for driver update */
    while (!LWIP_EXAMPLE_APP_ABORT()) {
#if NO_SYS
        /* handle timers (already done in tcpip.c when NO_SYS=0) */
        sys_check_timeouts();
#endif /* NO_SYS */

#if USE_ETHERNET
        sys_msleep(1);
#endif /* USE_ETHERNET */

#if ENABLE_LOOPBACK && !LWIP_NETIF_LOOPBACK_MULTITHREADING
        /* check for loopback packets on all netifs */
        netif_poll_all();
#endif /* ENABLE_LOOPBACK && !LWIP_NETIF_LOOPBACK_MULTITHREADING */
    }
}
