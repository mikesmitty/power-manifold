#pragma once

// Maintenance console on USB CDC (stdio). Provisioning (WiFi/MQTT
// credentials), status, and manual control — the recovery path when the
// network is unreachable or not yet configured.

void cli_init(void);
void cli_poll(void);
