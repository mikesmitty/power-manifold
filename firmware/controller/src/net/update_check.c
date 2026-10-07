#include "update_check.h"

#include <stdio.h>
#include <string.h>

#include "lwip/altcp.h"
#include "lwip/apps/http_client.h"
#include "pico/rand.h"

#include "flash_map.h"
#include "http_url.h"
#include "manifold.h"
#include "net.h"
#include "ota_pull.h"
#include "settings.h"
#include "update.h"
#include "update_auto.h"
#include "update_latest.h"

#define FIRST_CHECK_MS (30u * 1000u)            // after the network is first up
#define CHECK_EVERY_MS (24u * 60u * 60u * 1000u)
#define RETRY_MS       (60u * 60u * 1000u)      // after a check that failed
#define BUSY_RETRY_MS  (60u * 1000u)            // an update transfer is running

typedef enum { RES_NONE, RES_OK, RES_FAILED } result_t;

static bool           scheduled;
static uint32_t       next_at_ms;
static bool           busy;
static volatile bool  done;       // set by the result callback, handled in poll
static bool           transfer_ok;
static unsigned       http_status; // 0 when the source was not reached at all
static httpc_state_t *conn;
static char           body[256];
static size_t         body_len;
static bool           body_overflow;
static result_t       last;       // outcome of the last finished check
static uint32_t       last_ms;

static err_t recv_cb(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err) {
    (void)arg; (void)err;
    if (!p) return ERR_OK;
    for (struct pbuf *q = p; q; q = q->next) {
        size_t room = sizeof(body) - 1 - body_len;
        size_t n = q->len < room ? q->len : room;
        memcpy(body + body_len, q->payload, n);
        body_len += n;
        if (n < q->len) body_overflow = true;
    }
    altcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static void result_cb(void *arg, httpc_result_t res, u32_t rx_len, u32_t srv_res, err_t err) {
    (void)arg; (void)rx_len; (void)err;
    conn = NULL;
    transfer_ok = res == HTTPC_RESULT_OK && srv_res == 200 && !body_overflow;
    http_status = (unsigned)srv_res;
    done = true;
}

static const httpc_connection_t settings = {.result_fn = result_cb};

static bool start(char *err, size_t errlen) {
    char url[HTTP_URL_HOST_MAX + HTTP_URL_PATH_MAX];
    http_url_t parts;
    if (!update_source_pointer_url(g_settings.update_url, PICO_BOARD, url, sizeof(url)) ||
        !http_url_parse(url, &parts, err, errlen)) {
        if (err && errlen && !err[0]) snprintf(err, errlen, "update source url too long");
        return false;
    }
    body_len = 0;
    body_overflow = false;
    http_status = 0;
    done = false;
    net_lock();
    err_t rc = httpc_get_file_dns(parts.host, parts.port, parts.path, &settings, recv_cb, NULL, &conn);
    net_unlock();
    if (rc != ERR_OK) {
        if (err && errlen) snprintf(err, errlen, "http client start failed (%d)", (int)rc);
        return false;
    }
    busy = true;
    return true;
}

// The outcome is logged when it changes, not every time: a controller with
// no route to the update source would otherwise say so every hour.
static void finish(uint32_t now_ms, bool ok) {
    net_lock(); // the MQTT pointer is offered from lwIP callbacks
    body[body_len] = '\0';
    bool newer = ok && update_latest_offer_json(body);
    bool announce = newer && update_latest_newer_than(FW_VERSION);
    char version[UPDATE_LATEST_VERSION_MAX];
    snprintf(version, sizeof(version), "%s", update_latest_version());
    net_unlock();

    if (announce)
        printf("update: %s is available ('update latest' installs it)\n", version);
    else if (!ok && last != RES_FAILED && http_status)
        printf("update: %s names no release for this board (http %u)\n", g_settings.update_url, http_status);
    else if (!ok && last != RES_FAILED)
        printf("update: could not reach the update source %s\n", g_settings.update_url);
    else if (ok && last == RES_FAILED)
        printf("update: the update source answers again\n");
    last = ok ? RES_OK : RES_FAILED;
    last_ms = now_ms;
    next_at_ms = now_ms + (ok ? CHECK_EVERY_MS : RETRY_MS);
}

void update_check_poll(uint32_t now_ms) {
    if (busy) {
        if (!done) return;
        busy = false;
        finish(now_ms, transfer_ok);
        return;
    }
    if (!g_settings.update_url[0] || !net_available() || !net_up()) return;
    if (!scheduled) {
        scheduled = true;
        next_at_ms = now_ms + FIRST_CHECK_MS;
    }
    if ((int32_t)(now_ms - next_at_ms) < 0) return;
    if (update_active() || ota_pull_busy()) { // leave the link to the transfer
        next_at_ms = now_ms + BUSY_RETRY_MS;
        return;
    }
    if (!start(NULL, 0)) finish(now_ms, false);
}

bool update_check_now(char *err, size_t errlen) {
    if (err && errlen) err[0] = '\0';
    if (!g_settings.update_url[0]) {
        snprintf(err, errlen, "no update source set ('update source default')");
        return false;
    }
    if (busy) {
        snprintf(err, errlen, "a check is already running");
        return false;
    }
    if (!net_available() || !net_up()) {
        snprintf(err, errlen, "network is down");
        return false;
    }
    scheduled = true;
    return start(err, errlen);
}

const char *update_check_state(uint32_t now_ms, uint32_t *age_s) {
    *age_s = last == RES_NONE ? 0 : (now_ms - last_ms) / 1000u;
    if (!g_settings.update_url[0]) return "off";
    if (busy) return "checking";
    return last == RES_OK ? "ok" : last == RES_FAILED ? "failed" : "never";
}

void update_check_status(uint32_t now_ms, char *out, size_t cap) {
    if (!g_settings.update_url[0]) {
        snprintf(out, cap, "off");
    } else if (last == RES_NONE) {
        snprintf(out, cap, "%s, not asked yet", g_settings.update_url);
    } else {
        unsigned long min = (now_ms - last_ms) / 60000u;
        snprintf(out, cap, "%s, %s %lu h %lu min ago", g_settings.update_url,
                 last == RES_OK ? "answered" : "no answer", min / 60, min % 60);
    }
}

// ---- automatic installs -----------------------------------------------------

#define SEEN_MAX_S (30u * 24u * 60u * 60u) // keeps the uptime arithmetic in range

static uint32_t seen_ms;     // this boot's uptime when update_seen became known
static bool     seen_noted;  // seen_ms is set for update_seen
static uint32_t last_try_ms; // the last automatic attempt this boot
static bool     trying;      // an automatic pull is running
static bool     tried_noted; // update_tried holds the image the restart goes into

// The wait for a release is counted by the clock when the clock was set as
// it became known, so a restart does not start it again; by uptime when not.
static void note_seen(uint32_t now_ms, const char *latest) {
    uint32_t epoch = net_epoch();
    if (!latest[0]) return;
    if (strcmp(latest, g_settings.update_seen)) {
        snprintf(g_settings.update_seen, sizeof(g_settings.update_seen), "%s", latest);
        g_settings.update_seen_at = epoch;
        g_settings.update_wait_days = update_auto_wait_days(get_rand_32());
        seen_ms = now_ms;
        seen_noted = true;
        settings_save_later();
        if (g_settings.update_auto)
            printf("update: %s installs automatically after %u day%s, between 03:00 and 05:00\n", latest,
                   g_settings.update_wait_days, g_settings.update_wait_days == 1 ? "" : "s");
        return;
    }
    if (!seen_noted) {
        seen_ms = now_ms;
        seen_noted = true;
    }
    if (!g_settings.update_seen_at && epoch) { // the clock came after the release
        g_settings.update_seen_at = epoch - (now_ms - seen_ms) / 1000u;
        settings_save_later();
    }
}

static update_auto_t decide(uint32_t now_ms, const char *latest) {
    uint32_t epoch = net_epoch();
    uint32_t seen = seen_ms;
    if (g_settings.update_seen_at && epoch >= g_settings.update_seen_at && !strcmp(latest, g_settings.update_seen)) {
        uint32_t s = epoch - g_settings.update_seen_at;
        seen = now_ms - (s < SEEN_MAX_S ? s : SEEN_MAX_S) * 1000u;
    } else if (!seen_noted || strcmp(latest, g_settings.update_seen)) {
        seen = now_ms; // not noted yet: the wait has not started
    }
    update_auto_in_t in = {
        .enabled = g_settings.update_auto,
        .latest = latest,
        .skip = g_settings.update_skip,
        .now_ms = now_ms,
        .seen_ms = seen,
        .wait_days = g_settings.update_wait_days,
        .last_try_ms = last_try_ms,
        .epoch = epoch,
        .tz_offset_min = g_settings.tz_offset_min,
        .postpone_until = g_settings.update_postpone,
    };
    return update_auto_decide(&in);
}

void update_auto_poll(uint32_t now_ms) {
    if (update_reboot_pending()) {
        // Whatever asked for it, the restart goes into a new image: note
        // which, so the next boot can tell whether it stayed.
        if (!tried_noted) {
            tried_noted = true;
            snprintf(g_settings.update_tried, sizeof(g_settings.update_tried), "%s", update_version_str());
            if (!settings_save()) printf("update: could not note the image being installed\n");
        }
        return;
    }
    if (trying) {
        if (ota_pull_busy()) return;
        trying = false;
        printf("update: the automatic install did not finish; it is tried again the next night\n");
    }
    if (flash_map_update_pending() || update_active() || ota_pull_busy()) return;

    char latest[UPDATE_LATEST_VERSION_MAX], url[UPDATE_LATEST_URL_MAX];
    net_lock(); // the MQTT pointer is offered from lwIP callbacks
    bool newer = update_latest_newer_than(FW_VERSION);
    snprintf(latest, sizeof(latest), "%s", newer ? update_latest_version() : "");
    snprintf(url, sizeof(url), "%s", update_latest_url());
    net_unlock();

    note_seen(now_ms, latest);
    if (decide(now_ms, latest) != UPDATE_AUTO_INSTALL) return;
    last_try_ms = now_ms ? now_ms : 1;
    char e[96];
    if (ota_pull_start(url, 0, e, sizeof(e))) {
        trying = true;
        printf("update: installing %s automatically\n", latest);
    } else {
        printf("update: automatic install of %s: %s\n", latest, e);
    }
}

void update_auto_settle(void) {
    if (!g_settings.update_tried[0] || flash_map_update_pending()) return;
    if (strcmp(g_settings.update_tried, FW_VERSION)) {
        printf("update: %s did not start properly and was rolled back; it is not installed automatically\n",
               g_settings.update_tried);
        snprintf(g_settings.update_skip, sizeof(g_settings.update_skip), "%s", g_settings.update_tried);
    }
    g_settings.update_tried[0] = '\0';
    settings_save_later();
}

bool update_auto_postpone(char *err, size_t errlen) {
    uint32_t epoch = net_epoch();
    if (!epoch) {
        snprintf(err, errlen, "the clock is not set yet; try again once it is");
        return false;
    }
    g_settings.update_postpone = epoch + UPDATE_AUTO_POSTPONE_S;
    settings_save_later();
    printf("update: automatic installs put off for %u days\n", UPDATE_AUTO_POSTPONE_S / 86400u);
    return true;
}

bool update_auto_skip(char *err, size_t errlen) {
    net_lock();
    bool newer = update_latest_newer_than(FW_VERSION);
    if (newer) snprintf(g_settings.update_skip, sizeof(g_settings.update_skip), "%s", update_latest_version());
    net_unlock();
    if (!newer) {
        snprintf(err, errlen, "no newer release is known");
        return false;
    }
    settings_save_later();
    printf("update: %s is not installed automatically ('update latest' still installs it)\n", g_settings.update_skip);
    return true;
}

const char *update_auto_state(uint32_t now_ms, uint32_t *wait_s) {
    const char *latest = update_latest_newer_than(FW_VERSION) ? update_latest_version() : "";
    update_auto_t st = trying ? UPDATE_AUTO_INSTALL : decide(now_ms, latest);
    *wait_s = 0;
    if (st == UPDATE_AUTO_HOLD) {
        uint32_t epoch = net_epoch();
        uint32_t waited = g_settings.update_seen_at && epoch >= g_settings.update_seen_at
                              ? epoch - g_settings.update_seen_at
                              : (now_ms - seen_ms) / 1000u;
        uint32_t total = g_settings.update_wait_days * 86400u;
        *wait_s = waited < total ? total - waited : 0;
    }
    return update_auto_name(st);
}
