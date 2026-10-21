#include "i2c_diag.h"

#include <string.h>

#include "hardware/i2c.h"
#include "hardware/sync.h"

#include "i2c_txn.h"
#include "pins.h"
#include "tca9539.h"
#include "tca9548a.h"

i2c_diag_result_t g_i2c_diag;
volatile uint32_t g_i2c_diag_seq;

#define DIAG_TIMEOUT_US (5 * 1000)

void i2c_diag_run(uint32_t arg) {
    uint8_t op = (arg >> 28) & 0xF, ch = (arg >> 24) & 0xF, addr = (arg >> 16) & 0xFF,
            reg = (arg >> 8) & 0xFF, n = arg & 0xFF;
    i2c_diag_result_t r;
    memset(&r, 0, sizeof(r));
    r.op = op;
    if (op == I2C_DIAG_EN) {
        r.ok = tca9539_set_en(ch, n != 0);
    } else {
        r.selected = ch == 0xF ? tca9548a_deselect_all() : tca9548a_select(ch);
        switch (op) {
        case I2C_DIAG_SCAN:
            for (uint8_t a = 0x08; a < 0x78; a++) {
                uint8_t b;
                bool t = i2c_txn_begin(); // per address: 112 timeouts would outlast a flash write's wait
                if (i2c_read_timeout_us(I2C_BUS, a, &b, 1, false, DIAG_TIMEOUT_US) == 1)
                    r.found[a >> 5] |= 1u << (a & 31);
                i2c_txn_end(t);
            }
            r.ok = 1;
            break;
        case I2C_DIAG_READ: {
            if (n < 1) n = 1;
            if (n > sizeof(r.data)) n = sizeof(r.data);
            r.count = n;
            bool t = i2c_txn_begin();
            r.ok = i2c_write_timeout_us(I2C_BUS, addr, &reg, 1, true, DIAG_TIMEOUT_US) == 1 &&
                   i2c_read_timeout_us(I2C_BUS, addr, r.data, n, false, DIAG_TIMEOUT_US) == n;
            i2c_txn_end(t);
            break;
        }
        case I2C_DIAG_WRITE: {
            uint8_t buf[2] = {reg, n};
            bool t = i2c_txn_begin();
            r.ok = i2c_write_timeout_us(I2C_BUS, addr, buf, 2, false, DIAG_TIMEOUT_US) == 2;
            i2c_txn_end(t);
            break;
        }
        default:
            break;
        }
    }
    g_i2c_diag = r;
    __dmb();
    g_i2c_diag_seq++;
}
