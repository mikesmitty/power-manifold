#include <string.h>

#include "cli_line.h"
#include "microtest.h"

static char out[200];

static const char *mask(const char *in) {
    cli_line_mask(in, out, sizeof(out));
    return out;
}

static void test_mask_secrets(void) {
    MT_ASSERT(!strcmp(mask("wifi home hunter2"), "wifi home " CLI_LINE_MASK));
    // the Wi-Fi password is the rest of the line, spaces and all
    MT_ASSERT(!strcmp(mask("wifi home a long pass phrase"), "wifi home " CLI_LINE_MASK));
    MT_ASSERT(!strcmp(mask("wifi home"), "wifi home"));
    MT_ASSERT(!strcmp(mask("mqtt broker.lan 8883 ha s3cret"), "mqtt broker.lan 8883 ha " CLI_LINE_MASK));
    MT_ASSERT(!strcmp(mask("mqtt broker.lan 1883 ha"), "mqtt broker.lan 1883 ha"));
    MT_ASSERT(!strcmp(mask("mqtt tls on"), "mqtt tls on"));
    MT_ASSERT(!strcmp(mask("token abcdef0123"), "token " CLI_LINE_MASK));
    MT_ASSERT(!strcmp(mask("token clear"), "token clear"));
    MT_ASSERT(!strcmp(mask("token"), "token"));
}

static void test_mask_spacing_and_width(void) {
    // tabs and runs of spaces collapse, as the console splits them
    MT_ASSERT(!strcmp(mask("  port\t1   on "), "port 1 on"));
    MT_ASSERT(!strcmp(mask(""), ""));
    MT_ASSERT(!strcmp(mask("   "), ""));
    // the mask is the same width whatever the secret's length
    MT_ASSERT(!strcmp(mask("token x"), mask("token a-much-longer-token-value")));
    // a line too long for the output is cut at a word, never mid-mask
    char small[16];
    cli_line_mask("wifi homenetwork hunter2", small, sizeof(small));
    MT_ASSERT(!strcmp(small, "wifi"));
    cli_line_mask("status", small, 1);
    MT_ASSERT(!strcmp(small, ""));
}

// The serial-only commands, one assertion each: a command added to the
// refusal list in cli_line.c is added here too.
static void test_web_refusal(void) {
    MT_ASSERT(!strcmp(cli_line_web_refusal("token abc"), "token"));
    MT_ASSERT(!strcmp(cli_line_web_refusal(" token clear"), "token"));
    MT_ASSERT(!strcmp(cli_line_web_refusal("bootsel"), "bootsel"));
    MT_ASSERT(!strcmp(cli_line_web_refusal("defaults"), "defaults"));
    MT_ASSERT(!strcmp(cli_line_web_refusal("stack"), "stack"));
    MT_ASSERT(!strcmp(cli_line_web_refusal("i2c scan 0"), "i2c"));
    MT_ASSERT(!strcmp(cli_line_web_refusal("sim fault 1 ocp"), "sim"));
    MT_ASSERT(!strcmp(cli_line_web_refusal("button long"), "button"));
    MT_ASSERT(!strcmp(cli_line_web_refusal("update --unsigned http://h/c.bin"), "update --unsigned"));
    MT_ASSERT(!strcmp(cli_line_web_refusal("update http://h/c.bin --downgrade"), "update --downgrade"));
    // a flag far down a long line is still found
    MT_ASSERT(!strcmp(cli_line_web_refusal("update a b c d e f g h i j k l m n o p --unsigned"),
                      "update --unsigned"));
    MT_ASSERT(cli_line_web_refusal("update latest") == NULL);
    MT_ASSERT(cli_line_web_refusal("update check") == NULL);
    MT_ASSERT(cli_line_web_refusal("status") == NULL);
    MT_ASSERT(cli_line_web_refusal("tokens") == NULL);
    MT_ASSERT(cli_line_web_refusal("save") == NULL);
    MT_ASSERT(cli_line_web_refusal("ups") == NULL);
    MT_ASSERT(!strcmp(cli_line_web_refusal("vin cal 24.13"), "vin cal"));
    MT_ASSERT(!strcmp(cli_line_web_refusal("vin  cal reset"), "vin cal"));
    MT_ASSERT(cli_line_web_refusal("vin") == NULL);
    MT_ASSERT(cli_line_web_refusal("simulate") == NULL);
    MT_ASSERT(cli_line_web_refusal("") == NULL);
}

void run_cli_line_tests(void) {
    mt_run("cli: secrets are masked in the logged line", test_mask_secrets);
    mt_run("cli: logged lines are tidy and cut at a word", test_mask_spacing_and_width);
    mt_run("cli: the web console refuses the serial-only commands", test_web_refusal);
}
