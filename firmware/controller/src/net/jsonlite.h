#pragma once

#include <stdbool.h>
#include <stddef.h>

// Just enough JSON for the management API's flat request objects: pull one
// top-level key's string or integer out of a body, and escape a string for
// output. No allocation, no tree; keys are matched textually ("key" followed
// by a colon), so nesting is not understood and is not needed.

// Copy the string value of "key" into out, unescaped (\" \\ \/ \b \f \n \r
// \t and \uXXXX below the surrogate range). 1 = found and fits; 0 = key
// absent or its value is not a string (out untouched); -1 = value longer than
// cap-1 (out holds the truncated prefix).
int json_get_str(const char *json, const char *key, char *out, size_t cap);

// Integer value of "key" (optional sign, decimal). False when absent or not a
// bare number.
bool json_get_int(const char *json, const char *key, long *out);

// Element idx (0-based) of the string array under "key", with json_get_str's
// result codes; 0 when the key is absent, not an array, too short, or the
// element (or an earlier one) is not a string.
int json_get_str_at(const char *json, const char *key, unsigned idx, char *out, size_t cap);

// Element idx of the array under "key" as an integer; false when absent, not
// an array, too short, or the element is not a bare number (strings and
// numbers before it are skipped, anything else stops the scan).
bool json_get_int_at(const char *json, const char *key, unsigned idx, long *out);

// Write in as a JSON string body (no surrounding quotes) into out, escaping
// the quote, backslash and control characters. Always NUL-terminates and
// never splits an escape sequence; returns the bytes written (excluding the
// NUL), which is less than strlen(in) would need only when truncated.
size_t json_escape(char *out, size_t cap, const char *in);
