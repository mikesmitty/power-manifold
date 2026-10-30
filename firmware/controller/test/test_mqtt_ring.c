#include "microtest.h"
#include "mqtt_ring.h"

#define RING 4096

// The card's messages on the wire at this writing, by topic and payload size
#define STATUS mqtt_publish_wire_len(20, 379, 0)
#define PORT   mqtt_publish_wire_len(30, 367, 0)
#define PORT_1 mqtt_publish_wire_len(30, 359, 0)
#define PORT_6 mqtt_publish_wire_len(30, 358, 0)

static void test_wire_len(void) {
    MT_ASSERT_EQ(STATUS, 404);
    MT_ASSERT_EQ(PORT, 402);
    MT_ASSERT_EQ(PORT_1, 394);
    MT_ASSERT_EQ(PORT_6, 393);
    MT_ASSERT_EQ(mqtt_publish_wire_len(10, 10, 1), 26); // QoS 1 adds the packet ID
    // the remaining length takes a second byte from 128
    MT_ASSERT_EQ(mqtt_publish_wire_len(25, 100, 0), 129);
    MT_ASSERT_EQ(mqtt_publish_wire_len(25, 101, 0), 131);
}

static void test_room(void) {
    size_t most = RING - MQTT_RING_RESERVE;
    MT_ASSERT(mqtt_ring_takes(0, 0, RING, most));
    MT_ASSERT(!mqtt_ring_takes(0, 0, RING, most + 1));
    // the same 1000 bytes queued, wrapped and not
    MT_ASSERT(mqtt_ring_takes(1000, 0, RING, most - 1000));
    MT_ASSERT(!mqtt_ring_takes(1000, 0, RING, most - 999));
    MT_ASSERT(mqtt_ring_takes(500, 3596, RING, most - 1000));
    MT_ASSERT(!mqtt_ring_takes(500, 3596, RING, most - 999));
}

// lwIP would take a packet of exactly the free space and lose the ring
static void test_never_fills_exactly(void) {
    for (unsigned used = 0; used < RING; used += 7) {
        uint16_t put = (uint16_t)((used + 2100) % RING);
        MT_ASSERT(!mqtt_ring_takes(put, 2100, RING, RING - used));
        MT_ASSERT(!mqtt_ring_takes(put, 2100, RING, RING - used - 1));
    }
}

// The message HA kept from 2026-10-30: 91 bytes of port 2 still queued, then
// ports 3 to 6, the status and ports 1 to 5 filled the ring to the last byte
static void test_field_case(void) {
    const size_t cycle[] = {PORT, PORT, PORT, PORT_6, STATUS, PORT_1, PORT, PORT, PORT, PORT};
    size_t put = 91;
    for (unsigned i = 0; i < 9; i++) {
        MT_ASSERT(mqtt_ring_takes((uint16_t)put, 0, RING, cycle[i]));
        put += cycle[i];
    }
    MT_ASSERT_EQ(put + cycle[9], RING);
    MT_ASSERT(!mqtt_ring_takes((uint16_t)put, 0, RING, cycle[9]));
}

void run_mqtt_ring_tests(void) {
    mt_run("mqtt ring: a publish's size on the wire", test_wire_len);
    mt_run("mqtt ring: room counts a wrapped ring", test_room);
    mt_run("mqtt ring: a publish never fills it exactly", test_never_fills_exactly);
    mt_run("mqtt ring: the case seen in the field is refused", test_field_case);
}
