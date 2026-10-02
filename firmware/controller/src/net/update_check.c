#include "update_check.h"

#include <stdio.h>
#include <string.h>

#include "lwip/altcp.h"
#include "lwip/apps/http_client.h"

#include "http_url.h"
#include "manifold.h"
#include "net.h"
#include "ota_pull.h"
#include "settings.h"
#include "update.h"
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
