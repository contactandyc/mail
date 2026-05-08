// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *SCHEMA_SQL =
    "PRAGMA journal_mode = WAL;"
    "PRAGMA synchronous = NORMAL;"
    "CREATE TABLE IF NOT EXISTS kv_store (key TEXT PRIMARY KEY, value TEXT);"
    "CREATE TABLE IF NOT EXISTS threads (id TEXT PRIMARY KEY);"
    "CREATE TABLE IF NOT EXISTS messages ("
    "    id TEXT PRIMARY KEY, thread_id TEXT, internal_date INTEGER,"
    "    subject TEXT, from_name TEXT, from_email TEXT, snippet TEXT, list_id TEXT, delivered_to TEXT, is_group INTEGER,"
    "    FOREIGN KEY(thread_id) REFERENCES threads(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS message_labels ("
    "    message_id TEXT, label_id TEXT, PRIMARY KEY(message_id, label_id),"
    "    FOREIGN KEY(message_id) REFERENCES messages(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS message_attachments ("
    "    message_id TEXT, attachment_id TEXT, filename TEXT, mime_type TEXT, file_size INTEGER, file_data BLOB,"
    "    PRIMARY KEY(message_id, attachment_id),"
    "    FOREIGN KEY(message_id) REFERENCES messages(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS remote_state (id TEXT PRIMARY KEY);"
    "CREATE TABLE IF NOT EXISTS pending_triage (id TEXT PRIMARY KEY);"
    "CREATE TABLE IF NOT EXISTS pending_hydration (id TEXT PRIMARY KEY);";

sqlite3 *db_init(const char *path) {
    sqlite3 *db = NULL;
    if (sqlite3_open(path, &db) != SQLITE_OK) return NULL;
    sqlite3_exec(db, SCHEMA_SQL, NULL, NULL, NULL);
    return db;
}

uint64_t db_get_history_id(sqlite3 *db) {
    sqlite3_stmt *stmt; uint64_t hid = 0;
    if (sqlite3_prepare_v2(db, "SELECT value FROM kv_store WHERE key='historyId'", -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) hid = strtoull((const char *)sqlite3_column_text(stmt, 0), NULL, 10);
        sqlite3_finalize(stmt);
    }
    return hid;
}

void db_set_history_id(sqlite3 *db, uint64_t hid) {
    char sql[128];
    snprintf(sql, sizeof(sql), "INSERT OR REPLACE INTO kv_store (key, value) VALUES ('historyId', '%llu')", hid);
    sqlite3_exec(db, sql, NULL, NULL, NULL);
}

void db_clear_remote_state(sqlite3 *db) {
    sqlite3_exec(db, "DELETE FROM remote_state;", NULL, NULL, NULL);
}

void db_insert_remote_state(sqlite3 *db, const char *id) {
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO remote_state (id) VALUES (?)", -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, id, -1, SQLITE_STATIC);
        sqlite3_step(stmt); sqlite3_finalize(stmt);
    }
}

void db_reconcile_remote_state(sqlite3 *db) {
    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    sqlite3_exec(db, "DELETE FROM messages WHERE id NOT IN (SELECT id FROM remote_state);", NULL, NULL, NULL);
    sqlite3_exec(db, "INSERT OR IGNORE INTO pending_triage (id) SELECT id FROM remote_state WHERE id NOT IN (SELECT id FROM messages);", NULL, NULL, NULL);
    sqlite3_exec(db, "DELETE FROM remote_state;", NULL, NULL, NULL);
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
}

static char *pop_from_queue(sqlite3 *db, const char *table) {
    char *id = NULL;
    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT id FROM %s LIMIT 1", table);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            id = strdup((const char *)sqlite3_column_text(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }

    // FIX: Immediately delete it so the next pop gets a fresh ID.
    if (id) {
        snprintf(sql, sizeof(sql), "DELETE FROM %s WHERE id = ?", table);
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, id, -1, SQLITE_STATIC);
            sqlite3_step(stmt); sqlite3_finalize(stmt);
        }
    }
    return id;
}

char *db_pop_pending_triage(sqlite3 *db) { return pop_from_queue(db, "pending_triage"); }
char *db_pop_pending_hydration(sqlite3 *db) { return pop_from_queue(db, "pending_hydration"); }

void db_move_triage_to_hydration(sqlite3 *db, const char *id) {
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO pending_hydration (id) VALUES (?)", -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, id, -1, SQLITE_STATIC); sqlite3_step(stmt); sqlite3_finalize(stmt);
    }
}

void db_requeue_triage(sqlite3 *db, const char *id) {
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO pending_triage (id) VALUES (?)", -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, id, -1, SQLITE_STATIC); sqlite3_step(stmt); sqlite3_finalize(stmt);
    }
}

void db_requeue_hydration(sqlite3 *db, const char *id) {
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO pending_hydration (id) VALUES (?)", -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, id, -1, SQLITE_STATIC); sqlite3_step(stmt); sqlite3_finalize(stmt);
    }
}

void db_save_message_full(sqlite3 *db, const gcloud_v1_gmail_message_t *msg) {
    if (!msg) return;
    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO threads (id) VALUES (?)", -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, msg->thread_id, -1, SQLITE_STATIC);
        sqlite3_step(stmt); sqlite3_finalize(stmt);
    }

    if (sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO messages (id, thread_id, internal_date, subject, from_name, from_email, snippet) VALUES (?, ?, ?, ?, ?, ?, ?)", -1, &stmt, NULL) == SQLITE_OK) {
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
        sqlite3_step(stmt); sqlite3_finalize(stmt);
    }
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
}

void db_delete_message(sqlite3 *db, const char *id) {
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, "DELETE FROM messages WHERE id = ?", -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, id, -1, SQLITE_STATIC); sqlite3_step(stmt); sqlite3_finalize(stmt);
    }
    // Also strip it from queues just in case
    if (sqlite3_prepare_v2(db, "DELETE FROM pending_triage WHERE id = ?", -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, id, -1, SQLITE_STATIC); sqlite3_step(stmt); sqlite3_finalize(stmt);
    }
    if (sqlite3_prepare_v2(db, "DELETE FROM pending_hydration WHERE id = ?", -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, id, -1, SQLITE_STATIC); sqlite3_step(stmt); sqlite3_finalize(stmt);
    }
}