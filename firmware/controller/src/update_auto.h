#pragma once

#include <stdbool.h>
#include <stdint.h>

// When a newer release installs by itself. The owner can turn automatic
// installs off (the update_auto setting), put them off for a week, or skip
// one version; Install from any surface still works in every case.
//
// A release waits a random 1 to 7 days after the controller first hears of
// it, drawn afresh for each release, so a fleet takes a new release over a
// week and one that is withdrawn soon after it comes out reaches few
// controllers. It then installs between 03:00 and 05:00 local time. A
// controller that does not know the time yet installs once the wait is over.
// An install that fails before the restart (download, signature) is tried
// again the next night. A version that restarted and was rolled back is skipped, as if the
// owner had skipped it (update_auto_settle in net/update_check.h).
//
// Pure code, host-tested.

#define UPDATE_AUTO_DAY_MS        (24u * 60u * 60u * 1000u)
#define UPDATE_AUTO_WAIT_DAYS_MIN 1u
#define UPDATE_AUTO_WAIT_DAYS_MAX 7u
#define UPDATE_AUTO_RETRY_MS      (20u * 60u * 60u * 1000u)
#define UPDATE_AUTO_WINDOW_START  (3u * 60u) // minutes after local midnight
#define UPDATE_AUTO_WINDOW_END    (5u * 60u)
#define UPDATE_AUTO_POSTPONE_S    (7u * 24u * 60u * 60u)

typedef enum {
    UPDATE_AUTO_OFF,       // the owner turned automatic installs off
    UPDATE_AUTO_NONE,      // no newer release is known
    UPDATE_AUTO_SKIPPED,   // the owner skipped this version, or it was rolled back
    UPDATE_AUTO_POSTPONED, // put off until postpone_until
    UPDATE_AUTO_HOLD,      // known for less than its wait
    UPDATE_AUTO_RETRY,     // the last attempt failed; waiting for the next night
    UPDATE_AUTO_WINDOW,    // waiting for 03:00 local time
    UPDATE_AUTO_INSTALL,   // install now
} update_auto_t;

typedef struct {
    bool        enabled;        // the update_auto setting
    const char *latest;         // the newer release known, "" for none
    const char *skip;           // the skipped version, "" for none
    uint32_t    now_ms;
    uint32_t    seen_ms;        // when latest first became known
    uint8_t     wait_days;      // drawn for latest (update_auto_wait_days)
    uint32_t    last_try_ms;    // the last automatic attempt, 0 for none
    uint32_t    epoch;          // unix time, 0 while the clock is not set
    int16_t     tz_offset_min;
    uint32_t    postpone_until; // unix time, 0 for none
} update_auto_in_t;

update_auto_t update_auto_decide(const update_auto_in_t *in);

// The wait for one release, from a random number.
uint8_t update_auto_wait_days(uint32_t random);

// "off", "none", "skipped", "postponed", "waiting", "retry", "scheduled" or
// "installing", for the status JSON.
const char *update_auto_name(update_auto_t s);
