#include "eth.h"

#include <stdio.h>
#include <string.h>

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/async_context.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"

#include "lwip/dhcp.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"

#include "net.h"
#include "pins.h"
#include "settings.h"
#include "w6100.h"

#define LINK_POLL_MS 500

static struct netif eth_netif;
static uint8_t mac[6];
static async_when_pending_worker_t rx_worker;
static async_at_time_worker_t link_worker;
static bool present;
static bool link;
static uint8_t phy;
static uint32_t rx_frames, tx_frames, tx_fails;
static char status[48];

static void int_enable(bool en) {
    gpio_set_irq_enabled(PIN_ETH_INT_N, GPIO_IRQ_LEVEL_LOW, en);
}

// INTn is level-triggered: it stays low until the socket's RECV interrupt is
// cleared, so the IRQ is masked here and re-enabled once the worker has
// drained the chip (same pattern the cyw43 driver uses for its wake pin).
static void gpio_irq(void) {
    if (gpio_get_irq_event_mask(PIN_ETH_INT_N) & GPIO_IRQ_LEVEL_LOW) {
        int_enable(false);
        async_context_set_work_pending(net_async_context(), &rx_worker);
    }
}

// async-context worker: lwIP lock held
static void rx_work(async_context_t *ctx, async_when_pending_worker_t *w) {
    (void)ctx; (void)w;
    for (;;) {
        w6100_irq_ack();
        uint16_t len;
        while ((len = w6100_rx_begin()) != 0) {
            struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
            if (p) {
                for (struct pbuf *q = p; q; q = q->next)
                    w6100_rx_read((uint8_t *)q->payload, (uint16_t)q->len);
            }
            w6100_rx_end(); // out of buffers: the frame is dropped, not stuck
            if (!p) continue;
            rx_frames++;
            if (eth_netif.input(p, &eth_netif) != ERR_OK) pbuf_free(p);
        }
        // anything that landed after the ack would not raise a new edge on a
        // level line we have masked: look once more before re-arming
        if (!w6100_rx_pending()) break;
    }
    int_enable(true);
}

static const char *speed_str(void) {
    // PHYSR: SPD bit set = 10 Mbps, DPX bit set = half duplex (verified on
    // a W6100-EVB-Pico2 against a 100M/full switch port)
    return (phy & 0x02) ? ((phy & 0x04) ? "10M half" : "10M full")
                        : ((phy & 0x04) ? "100M half" : "100M full");
}

static void link_work(async_context_t *ctx, async_at_time_worker_t *w) {
    bool up = w6100_link_up();
    if (up != link) {
        link = up;
        if (up) {
            phy = w6100_phy_status();
            netif_set_link_up(&eth_netif); // DHCP restarts from lwIP's link hook
            printf("eth: link up, %s\n", speed_str());
        } else {
            netif_set_link_down(&eth_netif);
            printf("eth: link down\n");
        }
    }
    async_context_add_at_time_worker_in_ms(ctx, w, LINK_POLL_MS);
}

static err_t linkoutput(struct netif *n, struct pbuf *p) {
    (void)n;
    if (!link || !w6100_tx_begin(p->tot_len)) {
        tx_fails++;
        return ERR_IF;
    }
    for (struct pbuf *q = p; q; q = q->next)
        w6100_tx_write((const uint8_t *)q->payload, (uint16_t)q->len);
    if (!w6100_tx_end()) {
        tx_fails++;
        return ERR_IF;
    }
    tx_frames++;
    return ERR_OK;
}

static err_t netif_init_cb(struct netif *n) {
    n->name[0] = 'e';
    n->name[1] = 't';
    n->output = etharp_output;
    n->linkoutput = linkoutput;
    n->mtu = 1500;
    n->hwaddr_len = ETH_HWADDR_LEN;
    memcpy(n->hwaddr, mac, ETH_HWADDR_LEN);
    n->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_IGMP;
    return ERR_OK;
}

bool eth_init(void) {
    // The W6100 ships without a MAC: derive a locally-administered unicast
    // one from the RP2350's unique ID so it is stable per board.
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    mac[0] = 0x02;
    memcpy(mac + 1, id.id + 3, 5);

    if (!w6100_init(mac)) {
        const w6100_fail_t *f = w6100_last_failure();
        printf("eth: no W6100 answering on SPI0 (GP16-21): %s failed, CIDR %04x VER %04x"
               " SYSR %02x; wired path off\n", f->step, f->cidr, f->ver, f->sysr);
        return false;
    }
    present = true;

    net_lock();
    if (g_settings.ip_static) {
        // the wired link owns the static address (net.h); no DHCP client, so
        // nothing restarts from the link hook
        ip4_addr_t ip = {.addr = g_settings.ip_addr}, mask = {.addr = g_settings.ip_mask},
                   gw = {.addr = g_settings.ip_gw};
        netif_add(&eth_netif, &ip, &mask, &gw, NULL, netif_init_cb, netif_input);
    } else {
        netif_add(&eth_netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4, NULL, netif_init_cb,
                  netif_input);
    }
    netif_set_hostname(&eth_netif, g_settings.device_name);
    netif_set_up(&eth_netif);
    if (!g_settings.ip_static) dhcp_start(&eth_netif);
    rx_worker.do_work = rx_work;
    async_context_add_when_pending_worker(net_async_context(), &rx_worker);
    link_worker.do_work = link_work;
    async_context_add_at_time_worker_in_ms(net_async_context(), &link_worker, LINK_POLL_MS);
    net_unlock();

    gpio_init(PIN_ETH_INT_N);
    gpio_set_dir(PIN_ETH_INT_N, GPIO_IN);
    gpio_pull_up(PIN_ETH_INT_N);
    gpio_add_raw_irq_handler(PIN_ETH_INT_N, gpio_irq);
    int_enable(true);
    irq_set_enabled(IO_IRQ_BANK0, true);

    printf("eth: W6100 v%04x, mac %02x:%02x:%02x:%02x:%02x:%02x\n", w6100_version(), mac[0],
           mac[1], mac[2], mac[3], mac[4], mac[5]);
    return true;
}

bool eth_present(void) {
    return present;
}

bool eth_link(void) {
    return present && link;
}

bool eth_up(void) {
    return present && link && !ip4_addr_isany_val(*netif_ip4_addr(&eth_netif));
}

struct netif *eth_netif_ptr(void) {
    return present ? &eth_netif : NULL;
}

const char *eth_status_str(void) {
    if (!present) return "absent";
    if (!link) return "no link";
    snprintf(status, sizeof(status), "%s, %s", speed_str(),
             eth_up() ? ip4addr_ntoa(netif_ip4_addr(&eth_netif)) : "no address");
    return status;
}
