// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/rate_manager.h"
#include "a-curl-gcloud-plugin/plugins/token.h"
#include "a-curl-gcloud-plugin/plugins/v1/gmail.h"
#include "a-curl-gcloud-plugin/sinks/v1/gmail.h"
#include "a-memory-library/aml_pool.h"
#include "a-memory-library/aml_alloc.h"

#include "an-oauth-library/google.h"
#include "an-oauth-library/core.h"

#include "db.h"
#include "sync.h"

#define GMAIL_TOKEN_RES_ID 100

// ============================================================================
// REPL MODE
// ============================================================================

static int repl_row_callback(void *NotUsed, int argc, char **argv, char **azColName) {
    for (int i = 0; i < argc; i++) {
        printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
    }
    printf("\n");
    return 0;
}

static void run_sql_repl(sqlite3 *db) {
    char line[4096];
    char query[16384] = "";
    char *errmsg = NULL;

    printf("=============================================\n");
    printf(" Interactive SQLite REPL Mode\n");
    printf(" Connected to mail.db\n");
    printf(" Type '.quit' or '.exit' to leave.\n");
    printf("=============================================\n\n");

    while (1) {
        if (strlen(query) == 0) printf("sqlite> ");
        else printf("   ...> ");

        if (!fgets(line, sizeof(line), stdin)) break;

        if (strncmp(line, ".quit", 5) == 0 || strncmp(line, ".exit", 5) == 0) break;

        if (strlen(query) == 0 && (strcmp(line, "\n") == 0 || strcmp(line, "\r\n") == 0)) continue;

        strncat(query, line, sizeof(query) - strlen(query) - 1);

        if (strchr(query, ';')) {
            int rc = sqlite3_exec(db, query, repl_row_callback, 0, &errmsg);
            if (rc != SQLITE_OK) {
                fprintf(stderr, "SQL Error: %s\n\n", errmsg);
                sqlite3_free(errmsg);
            }
            query[0] = '\0';
        }
    }
    printf("Exiting REPL...\n");
}

// ============================================================================
// DAEMON UTILS
// ============================================================================

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
    if (success) printf("[Agent] Sent Startup Email Success! ID: %s\n", id);
    else fprintf(stderr, "[Agent] Failed to send startup email.\n");
}

static void send_startup_email(curl_event_loop_t *loop) {
    const char *raw_email =
        "From: contactandyc@gmail.com\r\n"
        "To: contactandyc@gmail.com\r\n"
        "Subject: [Daemon] System Online\r\n"
        "Content-Type: text/plain; charset=\"UTF-8\"\r\n\r\n"
        "The Gmail Sync & Agent daemon has successfully authenticated and started.\n";

    curl_event_request_t *req = gcloud_v1_gmail_messages_send_init(
        loop, GMAIL_TOKEN_RES_ID, gcloud_v1_gmail_endpoint(), "me"
    );

    char *b64_email = b64enc_websafe(req->pool, raw_email, strlen(raw_email));
    gcloud_v1_gmail_messages_send_set_raw(req, b64_email);
    gcloud_v1_gmail_message_id_sink(req, on_email_sent_callback, NULL);

    gcloud_v1_gmail_submit(loop, req, 0);
    printf("[Agent] Startup confirmation email queued for delivery.\n");
}

// ============================================================================
// MAIN ENTRY
// ============================================================================

int main(int argc, char **argv) {
    bool repl_mode = false;
    bool daemon_mode = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--sql") == 0) repl_mode = true;
        if (strcmp(argv[i], "--daemon") == 0) daemon_mode = true;
    }

    sqlite3 *db = db_init("mail.db");
    if (!db) {
        fprintf(stderr, "FATAL: Could not open mail.db.\n");
        return 1;
    }

    if (repl_mode) {
        run_sql_repl(db);
        sqlite3_close(db);
        return 0;
    }

    const char *config_file = "config.json";
    const char *creds_file = "credentials.json";
    const char *required_scopes = "https://mail.google.com/";
    const int auth_port = 8080;

    aml_pool_t *boot_pool = aml_pool_init(1024);
    an_oauth_app_t *app_creds = an_oauth_load_app_credentials(boot_pool, creds_file);

    if (!app_creds) {
        fprintf(stderr, "FATAL: Could not read %s.\n", creds_file);
        return 1;
    }

    if (an_oauth_needs_reauth(config_file, required_scopes, app_creds)) {
        printf("[Boot] Authorizing scopes: %s\n", required_scopes);
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_event_loop_t *temp_loop = curl_event_loop_init(NULL, NULL);

        an_oauth_credentials_t *creds = an_oauth_google_localhost_flow(temp_loop, app_creds, required_scopes, auth_port);
        curl_event_loop_destroy(temp_loop);

        if (!creds) return 1;
        an_oauth_save_credentials(config_file, creds);
        an_oauth_credentials_free(creds);
    }
    aml_pool_destroy(boot_pool);

    curl_global_init(CURL_GLOBAL_DEFAULT);
    rate_manager_init();
    rate_manager_set_limit("gmail_api", 50, 1000.0);

    curl_event_loop_t *loop = curl_event_loop_init(NULL, NULL);

    if (!curl_event_plugin_gcloud_token_init(loop, config_file, GMAIL_TOKEN_RES_ID, true)) {
        fprintf(stderr, "FATAL: Failed to initialize token refresher.\n");
        return 1;
    }

    send_startup_email(loop);
    sync_start(loop, db, daemon_mode);

    if (daemon_mode) {
        printf("[System] Running in DAEMON mode. Press Ctrl+C to stop.\n");
    } else {
        printf("[System] Running in ONE-SHOT mode. Will exit when queues are clear.\n");
    }

    curl_event_loop_run(loop);

    curl_event_loop_destroy(loop);
    rate_manager_destroy();
    sqlite3_close(db);
    curl_global_cleanup();

    return 0;
}