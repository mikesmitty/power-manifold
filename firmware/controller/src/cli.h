#pragma once

#include <stdbool.h>
#include <stddef.h>

// Maintenance console on USB CDC (stdio). Provisioning (WiFi/MQTT
// credentials), status, and manual control — the recovery path when the
// network is unreachable or not yet configured.

void cli_init(void);
void cli_poll(void);

// The web console (/api/v1/console): the HTTP handler hands over a batch of
// command lines, the main loop runs them in cli_poll as if typed at the
// console, and the HTTP handler reads back what they printed. One batch at
// a time. The commands cli_line_web_refusal names are refused.
#define CLI_WEB_IN_MAX  2048 // a pasted batch, newline-separated
#define CLI_WEB_OUT_MAX 8192 // what a batch prints: room for `export` in full

typedef enum {
    CLI_WEB_IDLE,    // nothing since boot
    CLI_WEB_QUEUED,  // handed over, waiting for the main loop
    CLI_WEB_RUNNING,
    CLI_WEB_DONE,    // cli_web_output holds the transcript
} cli_web_state_t;

// From the HTTP handler. False while a batch is queued or running, or when
// the text is CLI_WEB_IN_MAX bytes or longer.
bool cli_web_submit(const char *text, size_t len);
cli_web_state_t cli_web_state(void);
// The last batch's transcript: each line as "web> <line>" with its secrets
// masked, then what it printed. Only stable while the state is CLI_WEB_DONE.
const char *cli_web_output(size_t *len);
