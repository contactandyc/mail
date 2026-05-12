// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#ifndef MAIL_DB_H
#define MAIL_DB_H

#include <sqlite3.h>
#include <stdbool.h>
#include <stdint.h>
#include "a-curl-gcloud-plugin/sinks/v1/gmail.h"

sqlite3 *db_init(const char *path);
uint64_t db_get_history_id(sqlite3 *db);
void db_set_history_id(sqlite3 *db, uint64_t hid);

// Phase 1: Discovery
void db_clear_remote_state(sqlite3 *db);
void db_insert_remote_state(sqlite3 *db, const char *id);
void db_reconcile_remote_state(sqlite3 *db);

// Queues & Transitions
char *db_pop_pending_triage(sqlite3 *db);
void db_move_triage_to_hydration(sqlite3 *db, const char *id);
void db_requeue_triage(sqlite3 *db, const char *id);

char *db_pop_pending_hydration(sqlite3 *db);
void db_requeue_hydration(sqlite3 *db, const char *id);

// Final Storage
void db_save_message_full(sqlite3 *db, const gcloud_v1_gmail_message_t *msg);
void db_delete_message(sqlite3 *db, const char *id);

// Count
uint64_t db_get_table_count(sqlite3 *db, const char *table_name);

#endif