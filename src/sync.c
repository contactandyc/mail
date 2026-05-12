// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "sync.h"
#include "db.h"
#include "agent.h"

// GCloud & Curl
#include "a-curl-gcloud-plugin/plugins/v1/gmail.h"
#include "a-curl-gcloud-plugin/sinks/v1/gmail.h"
#include "a-curl-gcloud-plugin/plugins/token.h"
#include "a-curl-library/sinks/memory.h"

// Utils
#include "a-memory-library/aml_alloc.h"
#include "a-json-library/ajson.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GMAIL_TOKEN_RES_ID 100
#define MAX_CONCURRENT_OPS 10

typedef struct {
    curl_event_loop_t *loop;
    sqlite3 *db;
    int active_ops;
    uint64_t history_id;
    bool daemon_mode;
} sync_ctx_t;

typedef struct {
    sync_ctx_t *ctx;
    char *id;
} fetch_ctx_t;

static sync_ctx_t g_sync_ctx = {0};

static void sync_tick(sync_ctx_t *ctx);
static void start_discovery_page(sync_ctx_t *ctx, const char *page_token);
static void start_delta_sync(sync_ctx_t *ctx, const char *page_token, int priority);
static void start_fetch_profile(sync_ctx_t *ctx);

// ============================================================================
// SHARED AUTH
// ============================================================================
static bool inject_auth(curl_event_request_t *req) {
    const gcloud_token_payload_t *tok = curl_event_res_peek(req->loop, GMAIL_TOKEN_RES_ID);
    if (!tok || !tok->access_token) return false;
    char auth[512]; snprintf(auth, sizeof(auth), "Bearer %s", tok->access_token);
    curl_event_request_set_header(req, "Authorization", auth);
    return true;
}

// ============================================================================
// PHASE 3: HYDRATION (Download Body)
// ============================================================================
static void on_hydration_complete(void *arg, curl_event_request_t *req, bool success, const gcloud_v1_gmail_message_t *msg) {
    fetch_ctx_t *fctx = (fetch_ctx_t *)arg;
    sync_ctx_t *ctx = fctx->ctx;
    char *id = fctx->id;
    ctx->active_ops--;

    if (success && msg) {
        db_save_message_full(ctx->db, msg);
        printf("[Hydration] Saved complete email: %.40s\n", msg->subject ? msg->subject : id);
    } else if (!success) {
        printf("[Hydration] Failed to fetch %s. Dropping from queue.\n", id);
    }

    free(id);
    free(fctx);
    sync_tick(ctx);
}

// ============================================================================
// PHASE 2: TRIAGE (Metadata + Agent Rules)
// ============================================================================
static void on_message_trashed(void *arg, curl_event_request_t *req, bool success, const char *id, const char *thread) {}

static void on_triage_complete(void *arg, curl_event_request_t *req, bool success, const gcloud_v1_gmail_message_t *msg) {
    fetch_ctx_t *fctx = (fetch_ctx_t *)arg;
    sync_ctx_t *ctx = fctx->ctx;
    char *id = fctx->id;
    ctx->active_ops--;

    if (success && msg) {
        if (agent_should_trash(msg)) {
            curl_event_request_t *trash_req = gcloud_v1_gmail_messages_trash_init(
                ctx->loop, GMAIL_TOKEN_RES_ID, gcloud_v1_gmail_endpoint(), "me", id);
            curl_event_request_body(trash_req, "");
            gcloud_v1_gmail_message_id_sink(trash_req, on_message_trashed, NULL);
            gcloud_v1_gmail_submit(ctx->loop, trash_req, 0);

            printf("[Agent] TRASHED -> %s\n", id);
        } else {
            db_move_triage_to_hydration(ctx->db, id);
            printf("[Triage] Passed checks. Queued for hydration -> %s\n", id);
        }
    } else if (!success) {
        printf("[Triage] Failed to fetch metadata for %s. Dropping from queue.\n", id);
    }

    free(id);
    free(fctx);
    sync_tick(ctx);
}

// ============================================================================
// QUEUE PUMP
// ============================================================================
static void sync_tick(sync_ctx_t *ctx) {
    while (ctx->active_ops < MAX_CONCURRENT_OPS) {
        char *triage_id = db_pop_pending_triage(ctx->db);
        if (triage_id) {
            fetch_ctx_t *fctx = malloc(sizeof(fetch_ctx_t));
            fctx->ctx = ctx; fctx->id = triage_id;
            curl_event_request_t *req = gcloud_v1_gmail_messages_get_init(
                ctx->loop, GMAIL_TOKEN_RES_ID, gcloud_v1_gmail_endpoint(), "me", triage_id);
            gcloud_v1_gmail_messages_get_set_format(req, "metadata");
            gcloud_v1_gmail_message_get_sink(req, false, on_triage_complete, fctx);
            gcloud_v1_gmail_submit(ctx->loop, req, 20);
            ctx->active_ops++;
            continue;
        }

        char *hydrate_id = db_pop_pending_hydration(ctx->db);
        if (hydrate_id) {
            fetch_ctx_t *fctx = malloc(sizeof(fetch_ctx_t));
            fctx->ctx = ctx; fctx->id = hydrate_id;
            curl_event_request_t *req = gcloud_v1_gmail_messages_get_init(
                ctx->loop, GMAIL_TOKEN_RES_ID, gcloud_v1_gmail_endpoint(), "me", hydrate_id);
            gcloud_v1_gmail_messages_get_set_format(req, "full");
            gcloud_v1_gmail_message_get_sink(req, true, on_hydration_complete, fctx);
            gcloud_v1_gmail_submit(ctx->loop, req, 20);
            ctx->active_ops++;
            continue;
        }
        break;
    }

    if (ctx->active_ops == 0) {
        if (ctx->daemon_mode) {
            printf("[Engine] Queues clear. Polling Gmail in 15 seconds...\n");
            start_delta_sync(ctx, NULL, -15);
        } else {
            printf("[Engine] Sync complete. Queues clear.\n");
        }
    }
}

// ============================================================================
// PHASE 1.5: FETCH PROFILE (Get Real History ID)
// ============================================================================
static void on_profile_complete(char *data, size_t length, bool success, CURLcode result, long http_code, const char *error_msg, void *arg, curl_event_request_t *req) {
    sync_ctx_t *ctx = (sync_ctx_t *)arg;
    ctx->active_ops--;

    if (success && http_code == 200) {
        ajson_t *json = ajson_parse_string(req->pool, data);
        if (!ajson_is_error(json)) {
            const char *hid_str = ajsono_scan_str(json, "historyId", NULL);
            if (hid_str) {
                ctx->history_id = strtoull(hid_str, NULL, 10);
                db_set_history_id(ctx->db, ctx->history_id);
                printf("[Discovery] Captured current historyId: %llu\n", ctx->history_id);
            }
        }
    } else {
        printf("[Discovery] Failed to capture profile. Re-trying Discovery on next boot.\n");
    }
    sync_tick(ctx);
}

static void start_fetch_profile(sync_ctx_t *ctx) {
    char url[1024];
    snprintf(url, sizeof(url), "%s/gmail/v1/users/me/profile", gcloud_v1_gmail_endpoint());

    curl_event_request_t *req = curl_event_request_build_get(url, NULL, NULL);
    curl_event_request_rate_limit(req, "gmail_api", true);
    curl_event_request_depend(req, GMAIL_TOKEN_RES_ID);
    curl_event_request_on_prepare(req, inject_auth);
    memory_sink(req, on_profile_complete, ctx);

    curl_event_loop_submit(ctx->loop, req, 20);
    ctx->active_ops++;
}

// ============================================================================
// PHASE 1: DISCOVERY (Full list)
// ============================================================================
static void on_discovery_complete(void *arg, curl_event_request_t *req, bool success, const gcloud_v1_gmail_message_ref_t *msgs, size_t num_msgs, const char *next_page, uint64_t estimate) {
    sync_ctx_t *ctx = (sync_ctx_t *)arg;

    if (success && num_msgs > 0) {
        sqlite3_exec(ctx->db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
        for (size_t i = 0; i < num_msgs; i++) {
            db_insert_remote_state(ctx->db, msgs[i].id);
        }
        sqlite3_exec(ctx->db, "COMMIT;", NULL, NULL, NULL);
    }

    if (success && next_page && *next_page) {
        printf("[Discovery] Fetching next 500 IDs...\n");
        start_discovery_page(ctx, next_page);
    } else if (success) {
        printf("[Discovery] Complete! Reconciling local state...\n");
        db_reconcile_remote_state(ctx->db);

        // FIX: Fetch the REAL history ID instead of hardcoding 1
        start_fetch_profile(ctx);
    } else {
        printf("[Discovery] Network failed. Sleeping...\n");
    }
}

static void start_discovery_page(sync_ctx_t *ctx, const char *page_token) {
    curl_event_request_t *req = gcloud_v1_gmail_messages_list_init(ctx->loop, GMAIL_TOKEN_RES_ID, gcloud_v1_gmail_endpoint(), "me");
    gcloud_v1_gmail_messages_list_set_max_results(req, 500);
    gcloud_v1_gmail_messages_list_set_query(req, "-in:trash%20-in:spam");

    if (page_token) gcloud_v1_gmail_messages_list_set_page_token(req, page_token);
    gcloud_v1_gmail_messages_list_sink(req, on_discovery_complete, ctx);
    gcloud_v1_gmail_submit(ctx->loop, req, 20);
}

// ============================================================================
// DELTA SYNC (history.list)
// ============================================================================
static void on_delta_complete(char *data, size_t length, bool success, CURLcode result, long http_code, const char *error_msg, void *arg, curl_event_request_t *req) {
    sync_ctx_t *ctx = (sync_ctx_t *)arg;
    ctx->active_ops--;

    if (!success || http_code != 200) {
        printf("[Delta] History expired or network drop. Falling back to Full Discovery.\n");
        db_clear_remote_state(ctx->db);
        start_discovery_page(ctx, NULL);
        return;
    }

    ajson_t *json = ajson_parse_string(req->pool, data);
    if (ajson_is_error(json)) return;

    ajson_t *history = ajsono_scan(json, "history");
    if (history && ajson_is_array(history)) {
        for (ajsona_t *h = ajsona_first(history); h; h = ajsona_next(h)) {
            ajson_t *added = ajsono_scan(h->value, "messagesAdded");
            if (added && ajson_is_array(added)) {
                for (ajsona_t *a = ajsona_first(added); a; a = ajsona_next(a)) {
                    ajson_t *msg = ajsono_scan(a->value, "message");
                    const char *id = msg ? ajsono_scan_str(msg, "id", NULL) : NULL;

                    if (id) {
                        sqlite3_stmt *stmt;
                        if (sqlite3_prepare_v2(ctx->db, "INSERT OR IGNORE INTO pending_triage (id) VALUES (?)", -1, &stmt, NULL) == SQLITE_OK) {
                            sqlite3_bind_text(stmt, 1, id, -1, SQLITE_STATIC); sqlite3_step(stmt); sqlite3_finalize(stmt);
                        }
                    }
                }
            }
            ajson_t *deleted = ajsono_scan(h->value, "messagesDeleted");
            if (deleted && ajson_is_array(deleted)) {
                for (ajsona_t *d = ajsona_first(deleted); d; d = ajsona_next(d)) {
                    ajson_t *msg = ajsono_scan(d->value, "message");
                    const char *id = msg ? ajsono_scan_str(msg, "id", NULL) : NULL;
                    if (id) {
                        db_delete_message(ctx->db, id);
                        printf("[Delta] Remotely Deleted -> %s\n", id);
                    }
                }
            }
        }
    }

    const char *new_hid = ajsono_scan_str(json, "historyId", NULL);
    if (new_hid) {
        ctx->history_id = strtoull(new_hid, NULL, 10);
        db_set_history_id(ctx->db, ctx->history_id);
    }

    const char *npt = ajsono_scan_str(json, "nextPageToken", NULL);
    if (npt) {
        start_delta_sync(ctx, npt, 10);
    } else {
        sync_tick(ctx);
    }
}

static void start_delta_sync(sync_ctx_t *ctx, const char *page_token, int priority) {
    char url[1024];
    snprintf(url, sizeof(url), "%s/gmail/v1/users/me/history?startHistoryId=%llu", gcloud_v1_gmail_endpoint(), ctx->history_id);
    if (page_token) snprintf(url + strlen(url), sizeof(url) - strlen(url), "&pageToken=%s", page_token);

    curl_event_request_t *req = curl_event_request_build_get(url, NULL, NULL);
    curl_event_request_rate_limit(req, "gmail_api", true);
    curl_event_request_depend(req, GMAIL_TOKEN_RES_ID);
    curl_event_request_on_prepare(req, inject_auth);
    memory_sink(req, on_delta_complete, ctx);

    curl_event_loop_submit(ctx->loop, req, priority);
    ctx->active_ops++;
}

void sync_start(curl_event_loop_t *loop, sqlite3 *db, bool daemon_mode) {
    sync_ctx_t *ctx = &g_sync_ctx;
    ctx->loop = loop;
    ctx->db = db;
    ctx->active_ops = 0;
    ctx->history_id = db_get_history_id(db);
    ctx->daemon_mode = daemon_mode;

    uint64_t saved_count   = db_get_table_count(db, "messages");
    uint64_t triage_count  = db_get_table_count(db, "pending_triage");
    uint64_t hydrate_count = db_get_table_count(db, "pending_hydration");

    printf("\n=============================================\n");
    printf("[System] Booting Gmail Sync Engine\n");
    printf("         Fully Saved Emails : %llu\n", saved_count);
    printf("         Pending Triage     : %llu\n", triage_count);
    printf("         Pending Hydration  : %llu\n", hydrate_count);
    printf("=============================================\n\n");

    char *resume_check = db_pop_pending_triage(db);
    if (!resume_check) resume_check = db_pop_pending_hydration(db);

    if (resume_check) {
        printf("[Engine] Resuming interrupted queues...\n");
        free(resume_check);
        sync_tick(ctx);
    } else if (ctx->history_id == 0) {
        printf("[Engine] Booting Phase 1: Discovery (Fetching IDs)\n");
        db_clear_remote_state(ctx->db);
        start_discovery_page(ctx, NULL);
    } else {
        printf("[Engine] Booting Delta Sync...\n");
        start_delta_sync(ctx, NULL, 10);
    }
}