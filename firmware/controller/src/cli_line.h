#pragma once

#include <stddef.h>

// Console command lines as text, hardware-free so the host tests cover them.

// What a secret becomes in the log: always the same width, so the log
// never tells how long a password or token is.
#define CLI_LINE_MASK "********"

// The line with its secrets replaced by CLI_LINE_MASK: the password of
// `wifi` and `mqtt`, and the value of `token` (`token clear` stays as it
// is). Words come out separated by single spaces; a line that does not fit
// in cap is cut short.
void cli_line_mask(const char *in, char *out, size_t cap);

// Why the web console refuses this line, or NULL if it may run there. The
// refused commands are the ones that must stay behind the serial console:
// `token`, `bootsel`, `defaults`, `update` with --unsigned or --downgrade,
// `vin cal`, and the bench and test tools `stack`, `i2c`, `sim` and
// `button`. The returned text names the command, for "'%s' works only on
// the serial console".
const char *cli_line_web_refusal(const char *line);
