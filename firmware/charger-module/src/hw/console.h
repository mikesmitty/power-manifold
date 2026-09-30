#pragma once

#include <stdint.h>

// Console on USART2 (test pads J1 TX / J6 RX), 115200 8N1, transmit only.
// Never blocks: output goes through a buffer, and what does not fit is lost.

void console_init(void);
void console_str(const char *s);
void console_dec(int32_t v);
void console_tenths(int32_t v); // 253 -> "25.3"
void console_hex(uint32_t v, unsigned digits);
