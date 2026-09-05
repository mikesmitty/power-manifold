#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Dotted-quad text <-> IPv4 in network byte order (the first octet in the
// lowest address, as lwIP's ip4_addr_t.addr holds it). Hardware-free.
bool   ip4_parse(const char *s, uint32_t *addr_nbo); // strict: four decimal octets
size_t ip4_format(char *out, size_t cap, uint32_t addr_nbo); // "" for 0
bool   ip4_mask_valid(uint32_t mask_nbo); // a contiguous run of ones, not empty
