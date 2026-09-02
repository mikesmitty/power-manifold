#include "improv.h"

#include <stdio.h>
#include <string.h>

#include "btstack.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "improv_profile.h" // generated from improv_profile.gatt
#include "improv_proto.h"
#include "ipc.h"
#include "manifold.h"
#include "net.h"
#include "settings.h"

// Threading: BTstack runs in the cyw43 async context (the same worker lwIP
// uses), so the ATT/HCI callbacks below execute under that lock and
// improv_poll() takes it before touching shared state. Callbacks only record
// requests; the main loop performs the side effects (WiFi join, engine
// commands, settings save) — ipc_cmd_push has a single producer by design.

#define ADV_INTERVAL_UNITS 0x00A0 // 100 ms in 0.625 ms units
#define CONNECT_TIMEOUT_MS (30 * 1000)
#define CLOSE_AFTER_MS     1000  // let the result notification land first
#define IDENTIFY_MS        10000
#define RESULT_MAX         64    // [cmd len slen "http://255.255.255.255/" crc] is 27

// generated attribute handles; compile_gatt suffixes each UUID's first
// instance with _01
#define H_NAME_VALUE   ATT_CHARACTERISTIC_GAP_DEVICE_NAME_01_VALUE_HANDLE
#define H_STATE_VALUE  ATT_CHARACTERISTIC_00467768_6228_2272_4663_277478268001_01_VALUE_HANDLE
#define H_STATE_CCC    ATT_CHARACTERISTIC_00467768_6228_2272_4663_277478268001_01_CLIENT_CONFIGURATION_HANDLE
#define H_ERROR_VALUE  ATT_CHARACTERISTIC_00467768_6228_2272_4663_277478268002_01_VALUE_HANDLE
#define H_ERROR_CCC    ATT_CHARACTERISTIC_00467768_6228_2272_4663_277478268002_01_CLIENT_CONFIGURATION_HANDLE
#define H_RPC_VALUE    ATT_CHARACTERISTIC_00467768_6228_2272_4663_277478268003_01_VALUE_HANDLE
#define H_RESULT_VALUE ATT_CHARACTERISTIC_00467768_6228_2272_4663_277478268004_01_VALUE_HANDLE
#define H_RESULT_CCC   ATT_CHARACTERISTIC_00467768_6228_2272_4663_277478268004_01_CLIENT_CONFIGURATION_HANDLE
#define H_CAPS_VALUE   ATT_CHARACTERISTIC_00467768_6228_2272_4663_277478268005_01_VALUE_HANDLE

// notification bits (subscriptions and the send queue)
#define NOTIFY_STATE  1u
#define NOTIFY_ERROR  2u
#define NOTIFY_RESULT 4u

typedef enum { OPEN_NONE, OPEN_UNPROVISIONED, OPEN_DOWN, OPEN_MANUAL } open_reason_t;

static bool available;   // BTstack initialised
static bool powered;     // HCI power requested on
static bool hci_working; // controller up: advertising possible
static bool auto_inhibit; // `improv off`: no automatic windows until `improv on`
static bool window_open;
static open_reason_t open_reason;
static uint32_t window_until_ms; // 0 = open-ended
static uint32_t down_since_ms;   // 0 = network up

static hci_con_handle_t con = HCI_CON_HANDLE_INVALID;
static uint8_t notify_en; // NOTIFY_* the client subscribed to
static uint8_t pending;   // NOTIFY_* waiting for ATT_EVENT_CAN_SEND_NOW

static uint8_t improv_state = IMPROV_STATE_AUTHORIZED;
static uint8_t improv_error = IMPROV_ERR_NONE;
static uint8_t result[RESULT_MAX];
static uint8_t result_len;
static uint8_t rpc_buf[IMPROV_RPC_MAX];
static uint16_t rpc_len;

// raised in BTstack callbacks, acted on from improv_poll
static bool req_identify;
static bool req_creds;
static char req_ssid[IMPROV_SSID_MAX + 1];
static char req_pass[IMPROV_PASS_MAX + 1];

// connection attempt in flight; previous credentials come back on failure
static bool attempt;
static bool attempt_left_old_net; // link seen down since the join was issued
static uint32_t attempt_start_ms;
static char prev_ssid[sizeof(g_settings.wifi_ssid)];
static char prev_pass[sizeof(g_settings.wifi_pass)];
static uint32_t close_at_ms;

static btstack_packet_callback_registration_t hci_cb;

// Advertisement (exactly 31 bytes): flags, the 128-bit Improv service UUID
// clients filter on, and the Improv service data under 16-bit UUID 0x4677:
// [state, capabilities, 4 reserved]. The device name rides in the scan
// response.
#define ADV_STATE_IDX 25
static uint8_t adv_data[31] = {
    0x02, BLUETOOTH_DATA_TYPE_FLAGS, 0x06, // LE general discoverable, no BR/EDR
    0x11, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS,
    // 00467768-6228-2272-4663-277478268000, little-endian
    0x00, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46,
    0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00,
    0x09, BLUETOOTH_DATA_TYPE_SERVICE_DATA, 0x77, 0x46,
    IMPROV_STATE_AUTHORIZED, IMPROV_CAP_IDENTIFY, 0x00, 0x00, 0x00, 0x00,
};
static uint8_t scan_rsp[31];
static uint8_t scan_rsp_len;

// ---- GATT side (BTstack context) -------------------------------------------

static void queue_notify(uint8_t bits) {
    pending |= bits;
    if (con != HCI_CON_HANDLE_INVALID) att_server_request_can_send_now_event(con);
}

static void set_state(uint8_t s) {
    if (improv_state == s) return;
    improv_state = s;
    adv_data[ADV_STATE_IDX] = s;
    if (hci_working) gap_advertisements_set_data(sizeof(adv_data), adv_data);
    queue_notify(NOTIFY_STATE);
}

static void set_error(uint8_t e) {
    if (improv_error == e) return;
    improv_error = e;
    queue_notify(NOTIFY_ERROR);
}

static void send_pending(void) {
    if (con == HCI_CON_HANDLE_INVALID) { pending = 0; return; }
    pending &= notify_en; // unsubscribed characteristics just keep their value
    if (pending & NOTIFY_STATE) {
        pending &= (uint8_t)~NOTIFY_STATE;
        att_server_notify(con, H_STATE_VALUE, &improv_state, 1);
    } else if (pending & NOTIFY_ERROR) {
        pending &= (uint8_t)~NOTIFY_ERROR;
        att_server_notify(con, H_ERROR_VALUE, &improv_error, 1);
    } else if (pending & NOTIFY_RESULT) {
        pending &= (uint8_t)~NOTIFY_RESULT;
        att_server_notify(con, H_RESULT_VALUE, result, result_len);
    }
    if (pending) att_server_request_can_send_now_event(con);
}

static void start_advertising(void) {
    bd_addr_t null_addr;
    memset(null_addr, 0, sizeof(null_addr));
    gap_advertisements_set_params(ADV_INTERVAL_UNITS, ADV_INTERVAL_UNITS, 0 /* ADV_IND */,
                                  0 /* public address */, null_addr, 0x07, 0x00);
    size_t n = strlen(g_settings.device_name);
    if (n > sizeof(scan_rsp) - 2) n = sizeof(scan_rsp) - 2;
    scan_rsp[0] = (uint8_t)(n + 1);
    scan_rsp[1] = BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME;
    memcpy(scan_rsp + 2, g_settings.device_name, n);
    scan_rsp_len = (uint8_t)(n + 2);
    gap_scan_response_set_data(scan_rsp_len, scan_rsp);
    gap_advertisements_set_data(sizeof(adv_data), adv_data);
    gap_advertisements_enable(1);
}

static void process_rpc(void) {
    improv_rpc_t rpc;
    uint8_t err = improv_rpc_parse(rpc_buf, rpc_len, &rpc);
    rpc_len = 0;
    if (err != IMPROV_ERR_NONE) {
        set_error(err);
        return;
    }
    switch (rpc.cmd) {
    case IMPROV_CMD_WIFI_SETTINGS:
        if (attempt || close_at_ms) return; // one attempt at a time
        strcpy(req_ssid, rpc.ssid);
        strcpy(req_pass, rpc.pass);
        req_creds = true;
        set_error(IMPROV_ERR_NONE);
        break;
    case IMPROV_CMD_IDENTIFY:
        req_identify = true;
        set_error(IMPROV_ERR_NONE);
        break;
    }
}

static uint8_t ccc_bit(uint16_t handle) {
    switch (handle) {
    case H_STATE_CCC:  return NOTIFY_STATE;
    case H_ERROR_CCC:  return NOTIFY_ERROR;
    case H_RESULT_CCC: return NOTIFY_RESULT;
    default:           return 0;
    }
}

static uint16_t att_read_cb(hci_con_handle_t ch, uint16_t handle, uint16_t offset,
                            uint8_t *buf, uint16_t cap) {
    (void)ch;
    switch (handle) {
    case H_NAME_VALUE:
        return att_read_callback_handle_blob((const uint8_t *)g_settings.device_name,
                                             (uint16_t)strlen(g_settings.device_name),
                                             offset, buf, cap);
    case H_STATE_VALUE:
        return att_read_callback_handle_blob(&improv_state, 1, offset, buf, cap);
    case H_ERROR_VALUE:
        return att_read_callback_handle_blob(&improv_error, 1, offset, buf, cap);
    case H_RESULT_VALUE:
        return att_read_callback_handle_blob(result, result_len, offset, buf, cap);
    case H_CAPS_VALUE: {
        uint8_t caps = IMPROV_CAP_IDENTIFY;
        return att_read_callback_handle_blob(&caps, 1, offset, buf, cap);
    }
    case H_STATE_CCC:
    case H_ERROR_CCC:
    case H_RESULT_CCC: {
        uint8_t ccc[2];
        little_endian_store_16(ccc, 0, (notify_en & ccc_bit(handle))
                                           ? GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION
                                           : 0);
        return att_read_callback_handle_blob(ccc, 2, offset, buf, cap);
    }
    default:
        return 0;
    }
}

static int att_write_cb(hci_con_handle_t ch, uint16_t handle, uint16_t mode,
                        uint16_t offset, uint8_t *buf, uint16_t len) {
    (void)ch;
    // prepared (long) write bookkeeping: pieces land by offset, then EXECUTE
    if (mode == ATT_TRANSACTION_MODE_VALIDATE) return 0;
    if (mode == ATT_TRANSACTION_MODE_CANCEL) { rpc_len = 0; return 0; }
    if (mode == ATT_TRANSACTION_MODE_EXECUTE) {
        if (rpc_len) process_rpc();
        return 0;
    }

    switch (handle) {
    case H_STATE_CCC:
    case H_ERROR_CCC:
    case H_RESULT_CCC: {
        uint8_t bit = ccc_bit(handle);
        if (len >= 2 && little_endian_read_16(buf, 0) ==
                            GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION)
            notify_en |= bit;
        else
            notify_en &= (uint8_t)~bit;
        if (pending & notify_en) att_server_request_can_send_now_event(con);
        return 0;
    }
    case H_RPC_VALUE:
        if (mode == ATT_TRANSACTION_MODE_ACTIVE) {
            if (offset == 0) rpc_len = 0;
            if ((size_t)offset + len > sizeof(rpc_buf)) {
                rpc_len = 0;
                return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH;
            }
            memcpy(rpc_buf + offset, buf, len);
            if (offset + len > rpc_len) rpc_len = (uint16_t)(offset + len);
            return 0;
        }
        // plain writes: one packet may arrive as several MTU-sized writes
        if ((size_t)rpc_len + len > sizeof(rpc_buf)) {
            rpc_len = 0;
            set_error(IMPROV_ERR_INVALID_RPC);
            return 0;
        }
        memcpy(rpc_buf + rpc_len, buf, len);
        rpc_len = (uint16_t)(rpc_len + len);
        int frame = improv_rpc_frame_len(rpc_buf, rpc_len);
        if (frame > 0) {
            process_rpc();
        } else if (frame < 0) {
            rpc_len = 0;
            set_error(IMPROV_ERR_INVALID_RPC);
        }
        return 0;
    default:
        return 0;
    }
}

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet,
                           uint16_t size) {
    (void)channel; (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;
    switch (hci_event_packet_get_type(packet)) {
    case BTSTACK_EVENT_STATE:
        hci_working = btstack_event_state_get_state(packet) == HCI_STATE_WORKING;
        if (hci_working && window_open) start_advertising();
        break;
    case ATT_EVENT_CONNECTED:
        con = att_event_connected_get_handle(packet);
        notify_en = 0;
        pending = 0;
        rpc_len = 0;
        printf("improv: client connected\n");
        break;
    case ATT_EVENT_MTU_EXCHANGE_COMPLETE:
        printf("improv: att mtu %u\n", att_event_mtu_exchange_complete_get_MTU(packet));
        break;
    case HCI_EVENT_DISCONNECTION_COMPLETE:
        if (con != HCI_CON_HANDLE_INVALID) printf("improv: client disconnected\n");
        con = HCI_CON_HANDLE_INVALID;
        notify_en = 0;
        pending = 0;
        rpc_len = 0;
        break;
    case ATT_EVENT_CAN_SEND_NOW:
        send_pending();
        break;
    default:
        break;
    }
}

// ---- window control (lock held) --------------------------------------------

static void open_locked(uint32_t window_ms, open_reason_t why, const char *label,
                        uint32_t now_ms) {
    window_open = true;
    open_reason = why;
    window_until_ms = 0;
    if (window_ms) {
        window_until_ms = now_ms + window_ms;
        if (!window_until_ms) window_until_ms = 1;
    }
    improv_state = IMPROV_STATE_AUTHORIZED;
    improv_error = IMPROV_ERR_NONE;
    adv_data[ADV_STATE_IDX] = improv_state;
    result_len = 0;
    rpc_len = 0;
    pending = 0;
    close_at_ms = 0;
    if (!powered) {
        powered = true;
        hci_power_control(HCI_POWER_ON); // advertising starts at HCI_STATE_WORKING
    } else if (hci_working) {
        start_advertising();
    }
    printf("improv: BLE provisioning open (%s)", label);
    if (window_ms) printf(", closes in %lu min", (unsigned long)(window_ms / 60000));
    printf("\n");
}

static void close_locked(const char *why) {
    window_open = false;
    open_reason = OPEN_NONE;
    window_until_ms = 0;
    close_at_ms = 0;
    if (hci_working) gap_advertisements_enable(0);
    if (powered) {
        // power-off drops any connection first, then the controller sleeps
        powered = false;
        hci_working = false;
        hci_power_control(HCI_POWER_OFF);
    }
    printf("improv: BLE provisioning closed (%s)\n", why);
}

// ---- public API ------------------------------------------------------------

void improv_init(void) {
    net_lock();
    // Load the BT firmware now rather than lazily on the first window: the
    // download holds the shared bus for about a second, long enough to time
    // out WiFi ioctls in flight (seen as [CYW43] do_ioctl timeouts).
    if (cyw43_bluetooth_hci_init() != 0) {
        net_unlock();
        printf("improv: BT controller init failed; BLE provisioning unavailable\n");
        return;
    }
    l2cap_init();
    sm_init();
    att_server_init(profile_data, att_read_cb, att_write_cb);
    hci_cb.callback = packet_handler;
    hci_add_event_handler(&hci_cb);
    att_server_register_packet_handler(packet_handler);
    net_unlock();
    available = true;
}

bool improv_open(uint32_t window_ms, const char *why) {
    if (!available) return false;
    net_lock();
    auto_inhibit = false;
    if (window_open)
        printf("improv: already open (%s)\n", improv_state_str());
    else
        open_locked(window_ms, OPEN_MANUAL, why, to_ms_since_boot(get_absolute_time()));
    net_unlock();
    return true;
}

void improv_close(void) {
    if (!available) return;
    net_lock();
    auto_inhibit = true;
    if (window_open) close_locked("by request");
    net_unlock();
}

void improv_poll(uint32_t now_ms) {
    if (!available) return;
    bool do_save = false, do_identify = false;

    net_lock();

    bool up = net_up();
    bool unprovisioned = !g_settings.wifi_ssid[0];
    if (up) down_since_ms = 0;
    else if (!down_since_ms) down_since_ms = now_ms ? now_ms : 1;

    // automatic windows (a wired link counts as being on the network: no
    // point advertising WiFi provisioning to a box already reachable)
    if (!window_open && !auto_inhibit) {
        if (unprovisioned && !up)
            open_locked(0, OPEN_UNPROVISIONED, "no WiFi credentials", now_ms);
        else if (!up && now_ms - down_since_ms >= IMPROV_DOWN_OPEN_MS)
            open_locked(0, OPEN_DOWN, "network down", now_ms);
    } else if (window_open && !attempt && !close_at_ms) {
        if (open_reason == OPEN_UNPROVISIONED && (!unprovisioned || up))
            close_locked(unprovisioned ? "network up" : "credentials set");
        else if (open_reason == OPEN_DOWN && up)
            close_locked("network is back");
        else if (window_until_ms && (int32_t)(now_ms - window_until_ms) >= 0)
            close_locked("window timed out");
    }

    if (req_identify) {
        req_identify = false;
        do_identify = true;
    }

    if (req_creds) {
        req_creds = false;
        memcpy(prev_ssid, g_settings.wifi_ssid, sizeof(prev_ssid));
        memcpy(prev_pass, g_settings.wifi_pass, sizeof(prev_pass));
        snprintf(g_settings.wifi_ssid, sizeof(g_settings.wifi_ssid), "%s", req_ssid);
        snprintf(g_settings.wifi_pass, sizeof(g_settings.wifi_pass), "%s", req_pass);
        attempt = true;
        attempt_left_old_net = false;
        attempt_start_ms = now_ms;
        set_state(IMPROV_STATE_PROVISIONING);
        printf("improv: joining '%s'\n", g_settings.wifi_ssid);
        net_reconnect();
    }

    if (attempt) {
        // Fresh driver status, not net_up()'s cached flag: the previous
        // network's link may still read UP in the same pass the join was
        // issued. Success means the link went away and came back with an
        // address; a join the driver never actually started (still on the
        // old network) runs into the timeout instead.
        int status = net_link_status();
        if (status != CYW43_LINK_UP) attempt_left_old_net = true;
        if (status == CYW43_LINK_UP && attempt_left_old_net) {
            attempt = false;
            char url[40];
            snprintf(url, sizeof(url), "http://%s/", net_ip_str());
            result_len = (uint8_t)improv_result_build(IMPROV_CMD_WIFI_SETTINGS, url, result,
                                                      sizeof(result));
            set_error(IMPROV_ERR_NONE);
            set_state(IMPROV_STATE_PROVISIONED);
            queue_notify(NOTIFY_RESULT);
            do_save = true;
            close_at_ms = now_ms + CLOSE_AFTER_MS;
            if (!close_at_ms) close_at_ms = 1;
            printf("improv: provisioned, %s\n", url);
        } else if (status < 0 || now_ms - attempt_start_ms >= CONNECT_TIMEOUT_MS) {
            attempt = false;
            printf("improv: join failed (%s); keeping previous credentials\n",
                   status < 0 ? "rejected" : "timeout");
            memcpy(g_settings.wifi_ssid, prev_ssid, sizeof(prev_ssid));
            memcpy(g_settings.wifi_pass, prev_pass, sizeof(prev_pass));
            if (g_settings.wifi_ssid[0]) net_reconnect();
            set_error(IMPROV_ERR_UNABLE_TO_CONNECT);
            set_state(IMPROV_STATE_AUTHORIZED);
        }
    }

    if (close_at_ms && (int32_t)(now_ms - close_at_ms) >= 0) close_locked("provisioned");

    net_unlock();

    // side effects outside the lock: flash write parks core 1, and the
    // command queue has one producer (this loop)
    if (do_save)
        printf(settings_save() ? "improv: credentials saved\n"
                               : "improv: settings save FAILED\n");
    if (do_identify) {
        engine_cmd_t c = {.op = CMD_LED_IDENTIFY, .arg = IDENTIFY_MS};
        ipc_cmd_push(&c);
        printf("improv: identify\n");
    }
}

bool improv_available(void) {
    return available;
}

bool improv_active(void) {
    return window_open;
}

const char *improv_state_str(void) {
    if (!window_open) return "off";
    if (attempt) return "provisioning";
    if (improv_state == IMPROV_STATE_PROVISIONED) return "provisioned";
    if (con != HCI_CON_HANDLE_INVALID) return "connected";
    return "advertising";
}

uint32_t improv_window_left_s(uint32_t now_ms) {
    if (!window_open || !window_until_ms) return 0;
    int32_t left = (int32_t)(window_until_ms - now_ms);
    return left > 0 ? (uint32_t)left / 1000 : 0;
}
