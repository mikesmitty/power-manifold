#include "regmap.h"

#include <string.h>

#define CMD_QUEUE 4

static uint8_t img[BLADE_REG_END];   // the registers as the firmware last left them
static uint8_t snap[BLADE_REG_END];  // what the transfer in progress reads
static uint8_t stage[BLADE_REG_END]; // configuration bytes written in this transfer
static bool    staged[BLADE_REG_END];
static bool    any_staged;

static uint8_t ptr;
static bool    want_ptr;
static bool    configured;
static bool    config_changed;

static uint8_t cmd_q[CMD_QUEUE];
static uint8_t cmd_head, cmd_n;
static volatile uint32_t transactions;

static uint16_t get16(const uint8_t *b, uint8_t reg) {
    return (uint16_t)(b[reg] | (b[reg + 1] << 8));
}

static void put16(uint8_t reg, uint16_t v) {
    img[reg] = (uint8_t)(v & 0xFF);
    img[reg + 1] = (uint8_t)(v >> 8);
}

static void refresh_status(void) {
    uint8_t st = img[BLADE_REG_STATUS] & (uint8_t)~(BLADE_ST_CONFIGURED | BLADE_ST_FAULT);
    if (configured) st |= BLADE_ST_CONFIGURED;
    if (get16(img, BLADE_REG_FAULT)) st |= BLADE_ST_FAULT;
    img[BLADE_REG_STATUS] = st;
}

static bool writable(uint8_t reg) {
    return reg == BLADE_REG_CONTROL || (reg >= BLADE_REG_MAX_MA && reg < BLADE_REG_END);
}

static void push_command(uint8_t cmd) {
    if (cmd == BLADE_CMD_CLEAR_FAULTS) {
        put16(BLADE_REG_FAULT, 0);
        refresh_status();
    }
    if (!cmd || cmd_n == CMD_QUEUE) return; // full: dropped
    cmd_q[(uint8_t)(cmd_head + cmd_n) % CMD_QUEUE] = cmd;
    cmd_n++;
}

static void commit(void) {
    if (!any_staged) return;
    for (uint8_t r = 0; r < BLADE_REG_END; r++) {
        if (!staged[r]) continue;
        img[r] = stage[r];
        staged[r] = false;
    }
    any_staged = false;

    img[BLADE_REG_CONTROL] &= BLADE_CTL_PORT_EN;
    if (get16(img, BLADE_REG_MAX_MA) > BLADE_MAX_MA_LIMIT)
        put16(BLADE_REG_MAX_MA, BLADE_MAX_MA_LIMIT);
    configured = true;
    config_changed = true;
    refresh_status();
}

void regmap_init(uint8_t reset_cause) {
    memset(img, 0, sizeof img);
    memset(staged, 0, sizeof staged);
    any_staged = false;
    ptr = 0;
    want_ptr = false;
    configured = false;
    config_changed = false;
    cmd_head = cmd_n = 0;
    transactions = 0;

    img[BLADE_REG_WHO_AM_I] = BLADE_WHO_AM_I;
    img[BLADE_REG_PROTO] = BLADE_PROTO_VERSION;
    img[BLADE_REG_FW_MAJOR] = FW_MAJOR;
    img[BLADE_REG_FW_MINOR] = FW_MINOR;
    img[BLADE_REG_FW_PATCH] = FW_PATCH;
    img[BLADE_REG_RESET_CAUSE] = reset_cause;
    img[BLADE_REG_CAPS] = BLADE_CAP_PPS;
    put16(BLADE_REG_FAULT, BLADE_FAULT_RESET);
    refresh_status();
    memcpy(snap, img, sizeof snap);
}

void regmap_set_boot(uint8_t flags) {
    img[BLADE_REG_BOOT] = flags;
    snap[BLADE_REG_BOOT] = flags;
}

void regmap_addressed(bool read) {
    transactions++;
    commit(); // a repeated start ends the write before it
    if (read) memcpy(snap, img, sizeof snap);
    else want_ptr = true;
}

void regmap_rx(uint8_t byte) {
    if (want_ptr) {
        ptr = byte;
        want_ptr = false;
        return;
    }
    if (ptr == BLADE_REG_COMMAND) {
        push_command(byte);
    } else if (ptr < BLADE_REG_END && writable(ptr)) {
        stage[ptr] = byte;
        staged[ptr] = true;
        any_staged = true;
    }
    ptr++;
}

uint8_t regmap_tx(void) {
    uint8_t v = ptr < BLADE_REG_END ? snap[ptr] : 0;
    ptr++;
    return v;
}

void regmap_stop(void) {
    commit();
    want_ptr = false;
}

void regmap_publish(const regmap_live_t *live) {
    img[BLADE_REG_STATUS] = live->status;
    img[BLADE_REG_PDO] = live->pdo;
    put16(BLADE_REG_CONTRACT_MV, live->contract_mv);
    put16(BLADE_REG_CONTRACT_MA, live->contract_ma);
    put16(BLADE_REG_VBUS_MV, live->vbus_mv);
    put16(BLADE_REG_IOUT_MA, live->iout_ma);
    put16(BLADE_REG_VOUT_MV, live->vout_mv);
    put16(BLADE_REG_TEMP_CONV, (uint16_t)live->temp_conv_dc);
    put16(BLADE_REG_TEMP_PLUG, (uint16_t)live->temp_plug_dc);
    put16(BLADE_REG_TEMP_MCU, (uint16_t)live->temp_mcu_dc);
    refresh_status();
}

void regmap_raise(uint16_t faults) {
    put16(BLADE_REG_FAULT, get16(img, BLADE_REG_FAULT) | faults);
    refresh_status();
}

uint16_t regmap_faults(void) {
    return get16(img, BLADE_REG_FAULT);
}

bool regmap_config(regmap_config_t *out) {
    out->port_en = (img[BLADE_REG_CONTROL] & BLADE_CTL_PORT_EN) != 0;
    out->max_ma = get16(img, BLADE_REG_MAX_MA);
    out->max_mv = get16(img, BLADE_REG_MAX_MV);
    out->watch_s = img[BLADE_REG_WATCH_S];
    bool changed = config_changed;
    config_changed = false;
    return changed;
}

uint32_t regmap_transactions(void) {
    return transactions;
}

uint8_t regmap_command(void) {
    if (!cmd_n) return 0;
    uint8_t cmd = cmd_q[cmd_head];
    cmd_head = (uint8_t)(cmd_head + 1) % CMD_QUEUE;
    cmd_n--;
    return cmd;
}
