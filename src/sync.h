// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#ifndef MAIL_SYNC_H
#define MAIL_SYNC_H

#include "a-curl-library/curl_event_loop.h"
#include <sqlite3.h>

void sync_start(curl_event_loop_t *loop, sqlite3 *db);

#endif