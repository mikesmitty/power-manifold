#include "cli_line.h"

#include <stdbool.h>
#include <string.h>

// The next word at *p, split on spaces and tabs as the console's strtok_r
// does. Returns its length (0 at the end of the line) and leaves *p just past it.
static size_t next_word(const char **p, const char **word) {
    const char *s = *p;
    while (*s == ' ' || *s == '\t') s++;
    *word = s;
    while (*s && *s != ' ' && *s != '\t') s++;
    *p = s;
    return (size_t)(s - *word);
}

static bool word_is(const char *word, size_t len, const char *what) {
    return len == strlen(what) && !strncmp(word, what, len);
}

void cli_line_mask(const char *in, char *out, size_t cap) {
    if (!cap) return;
    out[0] = '\0';

    // The first secret word: it and everything after it become one mask. A
    // Wi-Fi password is the rest of the line and may hold spaces; an MQTT
    // password is the fifth word.
    const char *p = in, *word;
    size_t wl = next_word(&p, &word);
    int secret = -1;
    if (word_is(word, wl, "wifi")) {
        secret = 2;
    } else if (word_is(word, wl, "mqtt")) {
        secret = 4;
    } else if (word_is(word, wl, "token")) {
        const char *q = p, *arg;
        size_t al = next_word(&q, &arg);
        if (!word_is(arg, al, "clear")) secret = 1;
    }

    size_t len = 0;
    for (int i = 0; wl; i++, wl = next_word(&p, &word)) {
        if (i == secret) {
            word = CLI_LINE_MASK;
            wl = strlen(CLI_LINE_MASK);
        }
        if (len + (i ? 1 : 0) + wl >= cap) break;
        if (i) out[len++] = ' ';
        memcpy(out + len, word, wl);
        len += wl;
        out[len] = '\0';
        if (i == secret) break;
    }
}

// The commands only the serial console may run. When a console command is
// added or changed, decide whether it belongs here: anything that sets the
// API token, lets an unsigned or older image in, resets the settings, stops
// the firmware so only someone at the box can bring it back, or is a bench
// or test tool. Add each one to test_web_refusal as well.
static const char *const serial_only[] = {
    "token", "bootsel", "defaults",        // protected
    "stack", "i2c", "sim", "button",       // bench and test tools
};

const char *cli_line_web_refusal(const char *line) {
    const char *p = line, *word;
    size_t wl = next_word(&p, &word);
    for (size_t i = 0; i < sizeof(serial_only) / sizeof(serial_only[0]); i++)
        if (word_is(word, wl, serial_only[i])) return serial_only[i];
    if (word_is(word, wl, "update")) {
        while ((wl = next_word(&p, &word)) != 0) {
            if (word_is(word, wl, "--unsigned")) return "update --unsigned";
            if (word_is(word, wl, "--downgrade")) return "update --downgrade";
        }
    }
    return NULL;
}
