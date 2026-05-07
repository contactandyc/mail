// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <time.h>

// Your Ecosystem Headers
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/rate_manager.h"
#include "a-curl-gcloud-plugin/plugins/token.h"
#include "a-curl-gcloud-plugin/plugins/v1/gmail.h"
#include "a-curl-gcloud-plugin/sinks/v1/gmail.h"
#include "a-memory-library/aml_alloc.h"

#define GMAIL_TOKEN_RES_ID 100

// ============================================================================
// PART 1: THE LOCAL SQLITE DATABASE
// ============================================================================

static const char *SCHEMA_SQL =
    "PRAGMA journal_mode = WAL;"
    "PRAGMA synchronous = NORMAL;"
    "PRAGMA foreign_keys = ON;"

    "CREATE TABLE IF NOT EXISTS kv_store (key TEXT PRIMARY KEY, value TEXT);"
    "CREATE TABLE IF NOT EXISTS threads (id TEXT PRIMARY KEY);"

    "CREATE TABLE IF NOT EXISTS messages ("
    "    id TEXT PRIMARY KEY, thread_id TEXT, internal_date INTEGER,"
    "    subject TEXT, from_name TEXT, from_email TEXT, snippet TEXT, list_id TEXT, delivered_to TEXT, is_group INTEGER,"
    "    FOREIGN KEY(thread_id) REFERENCES threads(id) ON DELETE CASCADE"
    ");"

    "CREATE TABLE IF NOT EXISTS message_bodies ("
    "    message_id TEXT PRIMARY KEY, body_data TEXT,"
    "    FOREIGN KEY(message_id) REFERENCES messages(id) ON DELETE CASCADE"
    ");"

    "CREATE TABLE IF NOT EXISTS message_labels ("
    "    message_id TEXT, label_id TEXT, PRIMARY KEY(message_id, label_id),"
    "    FOREIGN KEY(message_id) REFERENCES messages(id) ON DELETE CASCADE"
    ");"

    // NEW: Attachments table with a BLOB for file_data
    "CREATE TABLE IF NOT EXISTS message_attachments ("
    "    message_id TEXT, attachment_id TEXT, filename TEXT, mime_type TEXT, file_size INTEGER, file_data BLOB,"
    "    PRIMARY KEY(message_id, attachment_id),"
    "    FOREIGN KEY(message_id) REFERENCES messages(id) ON DELETE CASCADE"
    ");"

    "CREATE INDEX IF NOT EXISTS idx_messages_thread ON messages(thread_id);"
    "CREATE INDEX IF NOT EXISTS idx_messages_date ON messages(internal_date DESC);"
    "CREATE INDEX IF NOT EXISTS idx_labels_label ON message_labels(label_id);";

sqlite3 *init_db(const char *path) {
    sqlite3 *db = NULL;
    if (sqlite3_open(path, &db) != SQLITE_OK) return NULL;
    sqlite3_exec(db, SCHEMA_SQL, NULL, NULL, NULL);
    return db;
}

uint64_t db_get_history_id(sqlite3 *db) {
    sqlite3_stmt *stmt;
    uint64_t hid = 0;
    if (sqlite3_prepare_v2(db, "SELECT value FROM kv_store WHERE key='historyId'", -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            hid = strtoull((const char *)sqlite3_column_text(stmt, 0), NULL, 10);
        }
        sqlite3_finalize(stmt);
    }
    return hid;
}

void db_set_history_id(sqlite3 *db, uint64_t hid) {
    char sql[128];
    snprintf(sql, sizeof(sql), "INSERT OR REPLACE INTO kv_store (key, value) VALUES ('historyId', '%llu')", hid);
    sqlite3_exec(db, sql, NULL, NULL, NULL);
}

// ============================================================================
// PART 2: THE SYNC ENGINE (Using your Plugins & Sinks!)
// ============================================================================

typedef struct {
    sqlite3 *db;
    char *next_page_token;
    int pending_ops; // Renamed to track BOTH messages and attachments
} sync_ctx_t;

typedef struct {
    sync_ctx_t *sync_ctx;
    char *message_id;
    char *attachment_id;
} attachment_ctx_t;

// Forward declaration for the pagination loop
static void queue_next_page(curl_event_loop_t *loop, sync_ctx_t *ctx);

// Callback when an ATTACHMENT body finishes downloading
static void on_attachment_downloaded(void *arg, curl_event_request_t *req, bool success, const uint8_t *file_data, size_t file_len) {
    attachment_ctx_t *actx = (attachment_ctx_t *)arg;
    sync_ctx_t *ctx = actx->sync_ctx;
    sqlite3 *db = ctx->db;

    if (success && file_data && file_len > 0) {
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db, "UPDATE message_attachments SET file_data = ? WHERE message_id = ? AND attachment_id = ?", -1, &stmt, NULL);
        sqlite3_bind_blob(stmt, 1, file_data, file_len, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, actx->message_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, actx->attachment_id, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        printf("[DB] Saved File | Msg: %s | Att: %s (%zu bytes)\n", actx->message_id, actx->attachment_id, file_len);
    } else {
        fprintf(stderr, "[Sink] Failed to download attachment %s\n", actx->attachment_id);
    }

    // NO MORE MANUAL FREEING NEEDED!
    // The event loop will destroy req->pool and wipe out actx instantly.

    // Drain Logic
    ctx->pending_ops--;
    if (ctx->pending_ops <= 0) {
        queue_next_page(req->loop, ctx);
    }
}

// Callback when a single FULL message body completes
static void on_message_downloaded(void *arg, curl_event_request_t *req, bool success, const gcloud_v1_gmail_message_t *msg) {
    sync_ctx_t *ctx = (sync_ctx_t *)arg;
    sqlite3 *db = ctx->db;

    if (success && msg) {
        sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

        char sql[2048];
        snprintf(sql, sizeof(sql), "INSERT OR IGNORE INTO threads (id) VALUES ('%s');", msg->thread_id);
        sqlite3_exec(db, sql, NULL, NULL, NULL);

        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO messages (id, thread_id, internal_date, subject, from_name, from_email, snippet, list_id, delivered_to, is_group) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, msg->id, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, msg->thread_id, -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 3, msg->internal_date);
        sqlite3_bind_text(stmt, 4, msg->subject, -1, SQLITE_STATIC);

        if (msg->from && msg->from->email) {
            sqlite3_bind_text(stmt, 5, msg->from->name, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 6, msg->from->email, -1, SQLITE_STATIC);
        } else {
            sqlite3_bind_null(stmt, 5); sqlite3_bind_null(stmt, 6);
        }

        sqlite3_bind_text(stmt, 7, msg->snippet, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 8, msg->list_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 9, msg->delivered_to, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 10, msg->is_group ? 1 : 0);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (msg->body_data) {
            sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO message_bodies (message_id, body_data) VALUES (?, ?)", -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, msg->id, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, (const char *)msg->body_data, msg->body_len, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }

        // --- NEW: QUEUE ATTACHMENTS ---
        if (msg->num_attachments > 0) {
            sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO message_attachments (message_id, attachment_id, filename, mime_type, file_size) VALUES (?, ?, ?, ?, ?)", -1, &stmt, NULL);

            for (size_t k = 0; k < msg->num_attachments; k++) {
                sqlite3_reset(stmt);
                sqlite3_bind_text(stmt, 1, msg->id, -1, SQLITE_STATIC);
                sqlite3_bind_text(stmt, 2, msg->attachments[k].attachment_id, -1, SQLITE_STATIC);
                sqlite3_bind_text(stmt, 3, msg->attachments[k].filename, -1, SQLITE_STATIC);
                sqlite3_bind_text(stmt, 4, msg->attachments[k].mime_type, -1, SQLITE_STATIC);
                sqlite3_bind_int64(stmt, 5, msg->attachments[k].size);
                sqlite3_step(stmt);

                // 1. Initialize the request FIRST
                curl_event_request_t *att_req = gcloud_v1_gmail_attachments_get_init(
                    req->loop, GMAIL_TOKEN_RES_ID, gcloud_v1_gmail_endpoint(), "me",
                    msg->id, msg->attachments[k].attachment_id
                );

                // 2. Allocate the context directly inside the request's arena!
                attachment_ctx_t *actx = aml_pool_zalloc(att_req->pool, sizeof(*actx));
                actx->sync_ctx = ctx;

                // msg->id belongs to the parent request, so we must copy it to the child
                actx->message_id = aml_pool_strdup(att_req->pool, msg->id);
                actx->attachment_id = aml_pool_strdup(att_req->pool, msg->attachments[k].attachment_id);

                gcloud_v1_gmail_attachment_get_sink(att_req, on_attachment_downloaded, actx);
                gcloud_v1_gmail_submit(req->loop, att_req, 0);

                ctx->pending_ops++;
            }
            sqlite3_finalize(stmt);
        }

        sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);

        // Formatted Logging
        time_t raw_time = msg->internal_date / 1000;
        struct tm *time_info = localtime(&raw_time);
        char time_str[32] = "Unknown";
        if (time_info) strftime(time_str, sizeof(time_str), "%b %d %y %H:%M", time_info);

        char short_subj[41] = "(No Subject)";
        if (msg->subject) snprintf(short_subj, sizeof(short_subj), "%.37s...", msg->subject);

        char short_from[31] = "Unknown";
        if (msg->from && msg->from->name) snprintf(short_from, sizeof(short_from), "%.27s...", msg->from->name);
        else if (msg->from && msg->from->email) snprintf(short_from, sizeof(short_from), "%.27s...", msg->from->email);

        printf("[DB] Saved %s | %s | From: %-30s | Subj: %s\n", msg->id, time_str, short_from, short_subj);
    }

    // Drain Logic
    ctx->pending_ops--;
    if (ctx->pending_ops <= 0) {
        queue_next_page(req->loop, ctx);
    }
}

// Callback when a LIST page completes
static void on_messages_listed(void *arg, curl_event_request_t *req, bool success,
                               const gcloud_v1_gmail_message_ref_t *msgs, size_t num_msgs,
                               const char *next_page_token, uint64_t result_size_estimate)
{
    sync_ctx_t *ctx = (sync_ctx_t *)arg;
    if (!success) return;

    if (ctx->next_page_token) aml_free(ctx->next_page_token);
    ctx->next_page_token = next_page_token ? aml_strdup(next_page_token) : NULL;

    // We increment pending_ops here, instead of replacing it, just in case
    // there was a weird race condition (there shouldn't be).
    ctx->pending_ops += num_msgs;

    if (num_msgs == 0) {
        if (ctx->pending_ops <= 0) queue_next_page(req->loop, ctx);
        return;
    }

    for (size_t i = 0; i < num_msgs; i++) {
        curl_event_request_t *get_req = gcloud_v1_gmail_messages_get_init(
            req->loop, GMAIL_TOKEN_RES_ID, gcloud_v1_gmail_endpoint(), "me", msgs[i].id
        );
        gcloud_v1_gmail_messages_get_set_format(get_req, "full");
        gcloud_v1_gmail_message_get_sink(get_req, true, on_message_downloaded, ctx);
        gcloud_v1_gmail_submit(req->loop, get_req, 0);
    }
}

static void queue_next_page(curl_event_loop_t *loop, sync_ctx_t *ctx) {
    if (!ctx->next_page_token) {
        printf("[Sync] Reached the end of the sync chain.\n");
        return;
    }

    printf("[Sync] Batch complete. Queuing next page...\n");

    curl_event_request_t *list_req = gcloud_v1_gmail_messages_list_init(
        loop, GMAIL_TOKEN_RES_ID, gcloud_v1_gmail_endpoint(), "me"
    );

    gcloud_v1_gmail_messages_list_set_max_results(list_req, 500);
    gcloud_v1_gmail_messages_list_set_page_token(list_req, ctx->next_page_token);

    gcloud_v1_gmail_messages_list_sink(list_req, on_messages_listed, ctx);
    gcloud_v1_gmail_submit(loop, list_req, 10);
}

static void start_sync_cycle(curl_event_loop_t *loop, sqlite3 *db) {
    sync_ctx_t *ctx = aml_zalloc(sizeof(sync_ctx_t));
    ctx->db = db;
    ctx->pending_ops = 0;

    printf("[Engine] Performing FULL initial sync (Max 500 per page)...\n");

    curl_event_request_t *list_req = gcloud_v1_gmail_messages_list_init(
        loop, GMAIL_TOKEN_RES_ID, gcloud_v1_gmail_endpoint(), "me"
    );

    gcloud_v1_gmail_messages_list_set_max_results(list_req, 500);
    gcloud_v1_gmail_messages_list_sink(list_req, on_messages_listed, ctx);
    gcloud_v1_gmail_submit(loop, list_req, 10);
}

// ============================================================================
// PART 3: THE DAEMON ENTRY POINT
// ============================================================================

int main(int argc, char **argv) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    rate_manager_init();

    // NEW: With plugins enforcing Weights (5.0 per message),
    // a pool of 200 tokens/sec = ~40 messages/attachments downloaded per second.
    // This safely keeps you under Google's 250 units/sec hard cap.
    rate_manager_set_limit("gmail_api", 20, 200.0);

    sqlite3 *db = init_db("mail.db");
    if (!db) return 1;

    curl_event_loop_t *loop = curl_event_loop_init(NULL, NULL);
    curl_event_loop_enable_http3(loop, true);

    if (!curl_event_plugin_gcloud_token_init(loop, "config.json", GMAIL_TOKEN_RES_ID, true)) {
        fprintf(stderr, "Failed to start token refresher. Did you run the setup utility?\n");
        return 1;
    }

    start_sync_cycle(loop, db);

    printf("[Engine] Daemon is running. Waiting for Sync to complete...\n");
    curl_event_loop_run(loop);

    curl_event_loop_destroy(loop);
    rate_manager_destroy();
    sqlite3_close(db);
    curl_global_cleanup();

    return 0;
}
