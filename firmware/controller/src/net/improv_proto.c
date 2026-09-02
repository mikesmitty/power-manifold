#include "improv_proto.h"

#include <string.h>

uint8_t improv_checksum(const uint8_t *p, size_t n) {
    uint32_t sum = 0;
    for (size_t i = 0; i < n; i++) sum += p[i];
    return (uint8_t)sum;
}

int improv_rpc_frame_len(const uint8_t *buf, size_t len) {
    if (len < 2) return 0;
    size_t total = (size_t)buf[1] + 3; // cmd, len, data, checksum
    if (total > IMPROV_RPC_MAX) return -1;
    if (len < total) return 0;
    if (len > total) return -1;
    return (int)total;
}

uint8_t improv_rpc_parse(const uint8_t *pkt, size_t len, improv_rpc_t *out) {
    memset(out, 0, sizeof(*out));
    if (len < 3 || len != (size_t)pkt[1] + 3) return IMPROV_ERR_INVALID_RPC;
    if (improv_checksum(pkt, len - 1) != pkt[len - 1]) return IMPROV_ERR_INVALID_RPC;

    out->cmd = pkt[0];
    const uint8_t *data = pkt + 2;
    size_t dlen = pkt[1];

    switch (out->cmd) {
    case IMPROV_CMD_WIFI_SETTINGS: {
        if (dlen < 2) return IMPROV_ERR_INVALID_RPC;
        size_t ssid_len = data[0];
        if (1 + ssid_len + 1 > dlen) return IMPROV_ERR_INVALID_RPC;
        size_t pass_len = data[1 + ssid_len];
        if (2 + ssid_len + pass_len != dlen) return IMPROV_ERR_INVALID_RPC;
        if (ssid_len == 0 || ssid_len > IMPROV_SSID_MAX || pass_len > IMPROV_PASS_MAX)
            return IMPROV_ERR_INVALID_RPC;
        memcpy(out->ssid, data + 1, ssid_len);
        memcpy(out->pass, data + 2 + ssid_len, pass_len);
        return IMPROV_ERR_NONE;
    }
    case IMPROV_CMD_IDENTIFY:
        return IMPROV_ERR_NONE;
    default:
        return IMPROV_ERR_UNKNOWN_RPC;
    }
}

size_t improv_result_build(uint8_t cmd, const char *url, uint8_t *out, size_t cap) {
    size_t ulen = strlen(url);
    size_t total = 3 + ulen + 1; // cmd, len, str_len, str, checksum
    if (ulen + 1 > 255 || total > cap) return 0;
    out[0] = cmd;
    out[1] = (uint8_t)(ulen + 1);
    out[2] = (uint8_t)ulen;
    memcpy(out + 3, url, ulen);
    out[total - 1] = improv_checksum(out, total - 1);
    return total;
}
