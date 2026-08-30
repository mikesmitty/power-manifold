#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

// lwIP options for pico_cyw43_arch_lwip_threadsafe_background (NO_SYS=1).
// Based on the pico-examples common options, plus mDNS/MQTT/SNTP.

#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0

#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    16384
#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_ARP_QUEUE          10
#define MEMP_NUM_TCP_PCB            12
#define MEMP_NUM_UDP_PCB            8
#define PBUF_POOL_SIZE              24

#define LWIP_IPV4                   1
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_DNS                    1
#define LWIP_DHCP                   1
#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0

#define TCP_MSS                     1460
#define TCP_WND                     (8 * TCP_MSS)
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define LWIP_TCP_KEEPALIVE          1

#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_TX_SINGLE_PBUF   1
#define LWIP_CHKSUM_ALGORITHM       3

#define MEM_STATS                   0
#define SYS_STATS                   0
#define MEMP_STATS                  0
#define LINK_STATS                  0

// mDNS responder (advertises <hostname>.local and the HTTP service)
#define LWIP_MDNS_RESPONDER         1
#define LWIP_IGMP                   1
#define LWIP_NUM_NETIF_CLIENT_DATA  (1 + LWIP_MDNS_RESPONDER)
#define LWIP_NETIF_EXT_STATUS_CALLBACK 1
#define MEMP_NUM_SYS_TIMEOUT        (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 8)

// MQTT: ring buffer must absorb bursts of HA discovery config publishes
#define MQTT_OUTPUT_RINGBUF_SIZE    4096
#define MQTT_VAR_HEADER_BUFFER_LEN  256
#define MQTT_REQ_MAX_IN_FLIGHT      5

// SNTP: wall-clock time for log/event timestamps
#define SNTP_SERVER_DNS             1
#define SNTP_SET_SYSTEM_TIME(sec)   sntp_report_time(sec)
#include <stdint.h>
#ifdef __cplusplus
extern "C"
#endif
void sntp_report_time(uint32_t sec);

#endif // _LWIPOPTS_H
