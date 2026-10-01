#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// OTA transport core: streams a firmware image (a UF2, or the raw .bin with
// or without its signature trailer) into the inactive A/B slot, forces the
// try-before-you-buy flag on the written IMAGE_DEF, and reboots into it via a
// bootrom flash-update boot. The trial image then commits itself only after
// main.c's health window passes; anything less reboots straight back into the
// current image.
//
// What gets in (checked at finish, before the slot is made bootable): an image
// signed by one of the built-in keys (update_sig.h) for this board, no older
// than the running one. The caller of update_begin can waive either rule with
// UPDATE_ALLOW_*; only the console does, since reaching it takes the USB
// header or a debug probe. A build without keys accepts unsigned images from
// anyone, and still refuses older ones.
//
// One transfer at a time, core 0 only (the HTTP endpoint is the caller).
// Flash writes go through flash_safe_execute, so the engine must be running.
// On any error the transfer aborts; the half-written slot is harmless because
// its first sector — the only place the bootrom looks for an IMAGE_DEF — is
// erased up front and written last.

#define UPDATE_ALLOW_UNSIGNED  0x01u
#define UPDATE_ALLOW_DOWNGRADE 0x02u

bool update_begin(uint32_t stream_len, unsigned allow, char *err, size_t errlen);
bool update_write(const uint8_t *data, size_t len, char *err, size_t errlen);
bool update_finish(char *err, size_t errlen);
void update_abort(void);

bool        update_active(void);
uint32_t    update_age_ms(void);      // ms since the stream last made progress
uint32_t    update_bytes(void);       // logical image bytes placed so far
const char *update_slot_name(void);   // target slot, "A"/"B"
const char *update_version_str(void); // incoming image version, after finish
unsigned    update_key_count(void);   // signing keys built in; 0 = signing not enforced

// Post-finish reboot handshake: the HTTP layer schedules, the main loop polls
// update_reboot_due() so the 200 response can flush first, then reboots into
// the trial image (a plain reboot would ignore the TBYB-flagged slot).
void update_schedule_reboot(uint32_t delay_ms);
bool update_reboot_due(void);
void update_reboot_now(void) __attribute__((noreturn));
