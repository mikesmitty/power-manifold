#ifndef _PWRMAN_BTSTACK_CONFIG_H
#define _PWRMAN_BTSTACK_CONFIG_H

// BTstack configuration for the Improv Wi-Fi BLE provisioning service
// (src/net/improv.c): a single LE peripheral connection, no pairing, no
// bonding, no classic BT. Trimmed from the pico-sdk kitchen_sink config;
// the cyw43 transport requirements (pre-buffer, chunk alignment, controller
// to host flow control) are kept as-is.

#define ENABLE_LE_PERIPHERAL
#define ENABLE_LOG_ERROR
#define ENABLE_PRINTF_HEXDUMP // pico_btstack_base compiles hci_dump_embedded_stdout.c, which insists

// cyw43 HCI transport: 4-byte packet header in front of every HCI packet,
// word-aligned fragments
#define HCI_OUTGOING_PRE_BUFFER_SIZE 4
#define HCI_ACL_PAYLOAD_SIZE (1691 + 4)
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT 4

// pools (BLE-only; the classic ones are absent because ENABLE_CLASSIC is)
#define MAX_NR_GATT_CLIENTS 1
#define MAX_NR_HCI_CONNECTIONS 1
#define MAX_NR_L2CAP_CHANNELS 2
#define MAX_NR_L2CAP_SERVICES 2
#define MAX_NR_SM_LOOKUP_ENTRIES 3
#define MAX_NR_WHITELIST_ENTRIES 1

// The LE device DB in pico_btstack_ble is the TLV-backed one (it insists on
// this being defined); btstack_glue.c gives it a discard-everything TLV, so
// nothing ever reaches flash. Nothing bonds, so one slot is plenty.
#define NVM_NUM_DEVICE_DB_ENTRIES 1

// limit controller buffer use to avoid overrunning the cyw43 shared bus
#define MAX_NR_CONTROLLER_ACL_BUFFERS 3
#define MAX_NR_CONTROLLER_SCO_PACKETS 3
#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
#define HCI_HOST_ACL_PACKET_LEN 1024
#define HCI_HOST_ACL_PACKET_NUM 3
#define HCI_HOST_SCO_PACKET_LEN 120
#define HCI_HOST_SCO_PACKET_NUM 3

#define MAX_ATT_DB_SIZE 512 // att_db_util scratch; our profile is compiled in

#define HAVE_EMBEDDED_TIME_MS
#define HAVE_ASSERT
#define HCI_RESET_RESEND_TIMEOUT_MS 1000
#define ENABLE_SOFTWARE_AES128

#endif
