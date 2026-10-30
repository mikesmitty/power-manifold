#pragma once

// Room check for lwIP's MQTT output ring. lwIP tells an empty ring from a full
// one only by put == get, yet it accepts a packet that fits exactly. That
// packet leaves put == get, so the ring reads as empty and everything queued
// in it is lost. The broker then reads payload bytes as packet headers and
// drops the connection. The check lives inside lwIP, so a publish is refused
// here before it gets that far.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// What every publish leaves free. lwIP adds SUBSCRIBE (64 bytes at most here),
// PINGREQ (2) and PUBACK (4) to the ring by itself, so stopping one byte short
// of the end would still let those fill it exactly.
#define MQTT_RING_RESERVE 128

// Bytes a PUBLISH takes in the ring: type, remaining length, topic length,
// topic, packet ID above QoS 0, payload.
static inline size_t mqtt_publish_wire_len(size_t topic_len, size_t payload_len, uint8_t qos) {
    size_t remaining = 2 + topic_len + (qos ? 2 : 0) + payload_len;
    size_t len = 1 + remaining;
    do {
        len++;
        remaining >>= 7;
    } while (remaining);
    return len;
}

// Whether a packet may join a ring with these indices and still leave the
// reserve free
static inline bool mqtt_ring_takes(uint16_t put, uint16_t get, size_t ring_size, size_t wire_len) {
    size_t used = put >= get ? (size_t)(put - get) : ring_size - (size_t)(get - put);
    return wire_len + MQTT_RING_RESERVE <= ring_size - used;
}
