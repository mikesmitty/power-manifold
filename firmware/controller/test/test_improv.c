#include <string.h>

#include "improv_proto.h"
#include "microtest.h"

// Improv Wi-Fi BLE framing: [cmd, len, data..., checksum], checksum = low
// byte of the sum of everything before it.

static size_t wifi_packet(uint8_t *out, const char *ssid, const char *pass) {
    size_t s = strlen(ssid), p = strlen(pass);
    out[0] = IMPROV_CMD_WIFI_SETTINGS;
    out[1] = (uint8_t)(2 + s + p);
    out[2] = (uint8_t)s;
    memcpy(out + 3, ssid, s);
    out[3 + s] = (uint8_t)p;
    memcpy(out + 4 + s, pass, p);
    size_t n = 4 + s + p;
    out[n] = improv_checksum(out, n);
    return n + 1;
}

static void test_checksum_is_low_byte_of_sum(void) {
    uint8_t b[] = {0xFF, 0xFF, 0x03};
    MT_ASSERT_EQ(improv_checksum(b, 3), (0xFF + 0xFF + 0x03) & 0xFF);
    MT_ASSERT_EQ(improv_checksum(b, 0), 0);
}

static void test_wifi_settings_parse(void) {
    uint8_t pkt[IMPROV_RPC_MAX];
    size_t n = wifi_packet(pkt, "MyNet", "hunter22");
    improv_rpc_t rpc;
    MT_ASSERT_EQ(improv_rpc_parse(pkt, n, &rpc), IMPROV_ERR_NONE);
    MT_ASSERT_EQ(rpc.cmd, IMPROV_CMD_WIFI_SETTINGS);
    MT_ASSERT(strcmp(rpc.ssid, "MyNet") == 0);
    MT_ASSERT(strcmp(rpc.pass, "hunter22") == 0);
}

static void test_open_network_has_empty_password(void) {
    uint8_t pkt[IMPROV_RPC_MAX];
    size_t n = wifi_packet(pkt, "Cafe", "");
    improv_rpc_t rpc;
    MT_ASSERT_EQ(improv_rpc_parse(pkt, n, &rpc), IMPROV_ERR_NONE);
    MT_ASSERT(strcmp(rpc.ssid, "Cafe") == 0);
    MT_ASSERT_EQ(rpc.pass[0], 0);
}

static void test_bad_checksum_is_invalid_rpc(void) {
    uint8_t pkt[IMPROV_RPC_MAX];
    size_t n = wifi_packet(pkt, "MyNet", "pw");
    pkt[n - 1] ^= 0x01;
    improv_rpc_t rpc;
    MT_ASSERT_EQ(improv_rpc_parse(pkt, n, &rpc), IMPROV_ERR_INVALID_RPC);
}

static void test_unknown_command(void) {
    uint8_t pkt[4] = {0x7E, 0x00, 0x00, 0x00};
    pkt[2] = improv_checksum(pkt, 2);
    improv_rpc_t rpc;
    MT_ASSERT_EQ(improv_rpc_parse(pkt, 3, &rpc), IMPROV_ERR_UNKNOWN_RPC);
}

static void test_identify(void) {
    uint8_t pkt[3] = {IMPROV_CMD_IDENTIFY, 0x00, 0x00};
    pkt[2] = improv_checksum(pkt, 2);
    improv_rpc_t rpc;
    MT_ASSERT_EQ(improv_rpc_parse(pkt, 3, &rpc), IMPROV_ERR_NONE);
    MT_ASSERT_EQ(rpc.cmd, IMPROV_CMD_IDENTIFY);
}

static void test_inner_lengths_must_add_up(void) {
    uint8_t pkt[IMPROV_RPC_MAX];
    size_t n = wifi_packet(pkt, "MyNet", "pw");
    improv_rpc_t rpc;
    // ssid length claims more than the payload holds
    pkt[2] = 40;
    pkt[n - 1] = improv_checksum(pkt, n - 1);
    MT_ASSERT_EQ(improv_rpc_parse(pkt, n, &rpc), IMPROV_ERR_INVALID_RPC);
    // password length short of the payload
    n = wifi_packet(pkt, "MyNet", "pw");
    pkt[3 + 5] = 1;
    pkt[n - 1] = improv_checksum(pkt, n - 1);
    MT_ASSERT_EQ(improv_rpc_parse(pkt, n, &rpc), IMPROV_ERR_INVALID_RPC);
}

static void test_oversized_fields_rejected(void) {
    uint8_t pkt[IMPROV_RPC_MAX + 64];
    char ssid[34];
    memset(ssid, 'a', 33);
    ssid[33] = 0;
    size_t n = wifi_packet(pkt, ssid, "pw");
    improv_rpc_t rpc;
    MT_ASSERT_EQ(improv_rpc_parse(pkt, n, &rpc), IMPROV_ERR_INVALID_RPC);
    n = wifi_packet(pkt, "", "pw"); // empty ssid
    MT_ASSERT_EQ(improv_rpc_parse(pkt, n, &rpc), IMPROV_ERR_INVALID_RPC);
}

static void test_max_size_packet_fits(void) {
    uint8_t pkt[IMPROV_RPC_MAX];
    char ssid[33], pass[65];
    memset(ssid, 's', 32); ssid[32] = 0;
    memset(pass, 'p', 64); pass[64] = 0;
    size_t n = wifi_packet(pkt, ssid, pass);
    MT_ASSERT_EQ(n, IMPROV_RPC_MAX);
    MT_ASSERT_EQ(improv_rpc_frame_len(pkt, n), (int)n);
    improv_rpc_t rpc;
    MT_ASSERT_EQ(improv_rpc_parse(pkt, n, &rpc), IMPROV_ERR_NONE);
    MT_ASSERT_EQ(strlen(rpc.ssid), 32);
    MT_ASSERT_EQ(strlen(rpc.pass), 64);
}

static void test_frame_accumulates_mtu_pieces(void) {
    uint8_t pkt[IMPROV_RPC_MAX];
    size_t n = wifi_packet(pkt, "A-longer-network-name", "correct horse battery");
    MT_ASSERT(n > 20);
    MT_ASSERT_EQ(improv_rpc_frame_len(pkt, 1), 0);   // no length byte yet
    MT_ASSERT_EQ(improv_rpc_frame_len(pkt, 20), 0);  // first 20-byte write
    MT_ASSERT_EQ(improv_rpc_frame_len(pkt, n - 1), 0);
    MT_ASSERT_EQ(improv_rpc_frame_len(pkt, n), (int)n);
}

static void test_frame_rejects_garbage(void) {
    uint8_t pkt[IMPROV_RPC_MAX + 8];
    size_t n = wifi_packet(pkt, "MyNet", "pw");
    MT_ASSERT(improv_rpc_frame_len(pkt, n + 1) < 0); // trailing byte
    uint8_t huge[2] = {IMPROV_CMD_WIFI_SETTINGS, 0xFF};
    MT_ASSERT(improv_rpc_frame_len(huge, 2) < 0);    // longer than any valid packet
}

static void test_result_round_trips(void) {
    uint8_t out[64];
    size_t n = improv_result_build(IMPROV_CMD_WIFI_SETTINGS, "http://10.0.0.7/", out, sizeof(out));
    MT_ASSERT_EQ(n, 3 + 16 + 1);
    MT_ASSERT_EQ(out[0], IMPROV_CMD_WIFI_SETTINGS);
    MT_ASSERT_EQ(out[1], 17); // one length-prefixed string
    MT_ASSERT_EQ(out[2], 16);
    MT_ASSERT(memcmp(out + 3, "http://10.0.0.7/", 16) == 0);
    MT_ASSERT_EQ(out[n - 1], improv_checksum(out, n - 1));
    MT_ASSERT_EQ(improv_rpc_frame_len(out, n), (int)n); // same framing both ways
    MT_ASSERT_EQ(improv_result_build(IMPROV_CMD_WIFI_SETTINGS, "http://10.0.0.7/", out, 19), 0);
}

void run_improv_tests(void) {
    printf("improv protocol\n");
    mt_run("checksum is the low byte of the sum", test_checksum_is_low_byte_of_sum);
    mt_run("wifi settings parse", test_wifi_settings_parse);
    mt_run("open network has empty password", test_open_network_has_empty_password);
    mt_run("bad checksum is INVALID_RPC", test_bad_checksum_is_invalid_rpc);
    mt_run("unknown command is UNKNOWN_RPC", test_unknown_command);
    mt_run("identify", test_identify);
    mt_run("inner lengths must add up", test_inner_lengths_must_add_up);
    mt_run("oversized or empty fields rejected", test_oversized_fields_rejected);
    mt_run("max size packet fits the buffer", test_max_size_packet_fits);
    mt_run("frame accumulates MTU-sized pieces", test_frame_accumulates_mtu_pieces);
    mt_run("frame rejects garbage", test_frame_rejects_garbage);
    mt_run("result packet round-trips", test_result_round_trips);
}
