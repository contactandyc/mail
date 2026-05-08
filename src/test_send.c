// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "a-curl-library/curl_event_loop.h"
#include "a-curl-gcloud-plugin/plugins/token.h"
#include "a-curl-gcloud-plugin/plugins/v1/gmail.h"
#include "a-curl-gcloud-plugin/sinks/v1/gmail.h"
#include "a-memory-library/aml_pool.h"

#define GMAIL_TOKEN_RES_ID 100

// Web-safe Base64 encoder (Required for sending emails!)
static char *b64enc_websafe(aml_pool_t *pool, const void *data, size_t len) {
    static const char *enc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t out_len = ((len + 2)/3)*4;
    char *out = aml_pool_alloc(pool, out_len + 1);
    const unsigned char *in = (const unsigned char *)data;
    char *p = out;
    for (size_t i=0; i<len; i+=3) {
        unsigned v = in[i] << 16;
        if (i+1 < len) v |= in[i+1] << 8;
        if (i+2 < len) v |= in[i+2];
        *p++ = enc[(v >> 18) & 63];
        *p++ = enc[(v >> 12) & 63];
        if (i+1 < len) *p++ = enc[(v >> 6) & 63];
        if (i+2 < len) *p++ = enc[v & 63];
    }
    *p = 0;
    return out;
}

static void on_email_sent_callback(void *arg, curl_event_request_t *req, bool success, const char *id, const char *thread_id) {
    if (success) {
        printf("\n[Test] SUCCESS! Email sent. Message ID: %s\n", id);
    } else {
        fprintf(stderr, "\n[Test] FAILED to send email. Check your OAuth scopes or payload.\n");
    }

    // Stop the loop immediately so the test program can exit cleanly
    curl_event_loop_stop(req->loop);
}

int main(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl_event_loop_t *loop = curl_event_loop_init(NULL, NULL);

    // Reuse the config.json we just successfully generated!
    if (!curl_event_plugin_gcloud_token_init(loop, "config.json", GMAIL_TOKEN_RES_ID, true)) {
        fprintf(stderr, "FATAL: Failed to init token refresher. Check config.json.\n");
        return 1;
    }

    // Added 'From:' header just in case.
    const char *raw_email =
        "From: contactandyc@gmail.com\r\n"
        "To: contactandyc@gmail.com\r\n"
        "Subject: [Test] Isolated Send Test\r\n"
        "Content-Type: text/plain; charset=\"UTF-8\"\r\n\r\n"
        "This is a test from the standalone sender.\n";

    curl_event_request_t *req = gcloud_v1_gmail_messages_send_init(
        loop, GMAIL_TOKEN_RES_ID, gcloud_v1_gmail_endpoint(), "me"
    );

    char *b64_email = b64enc_websafe(req->pool, raw_email, strlen(raw_email));

    gcloud_v1_gmail_messages_send_set_raw(req, b64_email);
    gcloud_v1_gmail_message_id_sink(req, on_email_sent_callback, NULL);
    gcloud_v1_gmail_submit(loop, req, 0);

    printf("[Test] Dispatching email to loop...\n");
    curl_event_loop_run(loop);

    curl_event_loop_destroy(loop);
    curl_global_cleanup();

    return 0;
}
