#include "ota_pull.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "lwip/altcp.h"
#include "lwip/apps/http_client.h"

#include "http_url.h"
#include "net.h"
#include "update.h"

static bool           busy;
static bool           feed_failed; // update_write refused; drain the rest
static httpc_state_t *conn;
static http_url_t     target;
static unsigned       allow; // UPDATE_ALLOW_*, from whoever asked for the pull

static err_t recv_cb(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err) {
    (void)arg; (void)err;
    if (!p) return ERR_OK; // closed; result_cb reports
    if (!feed_failed) {
        char e[96];
        for (struct pbuf *q = p; q; q = q->next) {
            if (!update_write((const uint8_t *)q->payload, q->len, e, sizeof(e))) {
                // no clean way to abort an httpc transfer mid-body; report
                // once and discard the remainder of the download
                printf("update: pull aborted: %s\n", e);
                feed_failed = true;
                break;
            }
        }
    }
    altcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static err_t headers_cb(httpc_state_t *c, void *arg, struct pbuf *hdr,
                        u16_t hdr_len, u32_t content_len) {
    (void)c; (void)arg; (void)hdr; (void)hdr_len;
    char e[96];
    if (content_len == 0xFFFFFFFFu) {
        printf("update: pull aborted: server sent no Content-Length\n");
        return ERR_VAL;
    }
    if (!update_begin(content_len, allow, e, sizeof(e))) {
        printf("update: pull refused: %s\n", e);
        return ERR_VAL; // aborts the transfer; result_cb sees LOCAL_ABORT
    }
    printf("update: pulling %lu bytes into slot %s\n",
           (unsigned long)content_len, update_slot_name());
    return ERR_OK;
}

static void result_cb(void *arg, httpc_result_t res, u32_t rx_len,
                      u32_t srv_res, err_t err) {
    (void)arg; (void)rx_len;
    busy = false;
    conn = NULL;
    if (feed_failed) return; // already reported and aborted
    if (res != HTTPC_RESULT_OK || srv_res != 200) {
        update_abort();
        printf("update: pull failed (result %d, http %lu, err %d)\n",
               (int)res, (unsigned long)srv_res, (int)err);
        return;
    }
    char e[96];
    if (!update_finish(e, sizeof(e))) {
        printf("update: %s\n", e);
        return;
    }
    update_schedule_reboot(1000);
    printf("update: %lu bytes -> slot %s (v%s); trial reboot in 1s\n",
           (unsigned long)update_bytes(), update_slot_name(),
           update_version_str());
}

static const httpc_connection_t settings = {
    .result_fn = result_cb,
    .headers_done_fn = headers_cb,
};

static bool eout(char *err, size_t errlen, const char *msg) {
    if (err && errlen) snprintf(err, errlen, "%s", msg);
    return false;
}

bool ota_pull_start(const char *url, unsigned allow_flags, char *err, size_t errlen) {
    if (busy) return eout(err, errlen, "a pull is already in progress");
    if (!http_url_parse(url, &target, err, errlen)) return false;
    allow = allow_flags;

    feed_failed = false;
    net_lock();
    err_t rc = httpc_get_file_dns(target.host, target.port, target.path, &settings, recv_cb,
                                  NULL, &conn);
    net_unlock();
    if (rc != ERR_OK) {
        snprintf(err, errlen, "http client start failed (%d)", (int)rc);
        return false;
    }
    busy = true;
    return true;
}

bool ota_pull_busy(void) {
    return busy;
}
