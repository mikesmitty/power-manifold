// BTstack bring-up on the CYW43, standing in for the pico-sdk's
// pico_btstack_cyw43 library (btstack_cyw43.c), which cyw43_arch_init()
// calls into when Bluetooth is enabled. Same HCI transport and run-loop
// setup, minus the flash-backed TLV store the SDK version installs:
//
//  - that store formats two flash sectors at a fixed top-of-flash offset
//    the first time it runs (from inside cyw43_arch_init). On RP2350 the
//    default is sectors N-3/N-2, which overlaps the legacy settings pair on
//    boards without a partition table;
//  - it reads them back through the raw XIP window, which lands in the
//    wrong place once the bootrom has translated that window to slot B.
//
// Nothing here needs persisting: Improv is plaintext GATT by design, no
// pairing, no bonding. The LE device DB (which pico_btstack_ble compiles in
// its TLV-backed flavour) gets a TLV that finds nothing and keeps nothing,
// and with no global TLV instance the security manager falls back to its
// built-in ER/IR keys.

#include "ble/le_device_db_tlv.h"
#include "btstack_memory.h"
#include "btstack_run_loop.h"
#include "btstack_tlv.h"
#include "hci.h"
#include "pico/btstack_cyw43.h"
#include "pico/btstack_hci_transport_cyw43.h"
#include "pico/btstack_run_loop_async_context.h"

static int tlv_none_get(void *ctx, uint32_t tag, uint8_t *buf, uint32_t size) {
    (void)ctx; (void)tag; (void)buf; (void)size;
    return 0; // nothing stored
}

static int tlv_none_store(void *ctx, uint32_t tag, const uint8_t *data, uint32_t size) {
    (void)ctx; (void)tag; (void)data; (void)size;
    return 0; // accepted, discarded
}

static void tlv_none_delete(void *ctx, uint32_t tag) {
    (void)ctx; (void)tag;
}

static const btstack_tlv_t tlv_none = {
    &tlv_none_get,
    &tlv_none_store,
    &tlv_none_delete,
};

bool btstack_cyw43_init(async_context_t *context) {
    btstack_memory_init();
    btstack_run_loop_init(btstack_run_loop_async_context_get_instance(context));
    hci_init(hci_transport_cyw43_instance(), NULL);
    le_device_db_tlv_configure(&tlv_none, NULL);
    return true;
}

void btstack_cyw43_deinit(async_context_t *context) {
    (void)context;
    hci_power_control(HCI_POWER_OFF);
    hci_close();
    btstack_run_loop_async_context_deinit();
    btstack_run_loop_deinit();
    btstack_memory_deinit();
}
