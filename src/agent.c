// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "agent.h"
#include <string.h>
#include <time.h>
#include <stdio.h>

static bool has_label(const gcloud_v1_gmail_message_t *msg, const char *label) {
    if (!msg || !msg->label_ids) return false;
    for (size_t i = 0; i < msg->num_labels; i++) {
        if (strcmp(msg->label_ids[i], label) == 0) return true;
    }
    return false;
}

static bool is_standalone_sa(const char *subject) {
    if (!subject) return false;
    // Exact match
    if (strcmp(subject, "SA") == 0) return true;
    // Starts with "SA "
    if (strncmp(subject, "SA ", 3) == 0) return true;
    // Contains " SA "
    if (strstr(subject, " SA ") != NULL) return true;

    // Ends with " SA"
    size_t len = strlen(subject);
    if (len > 3 && strcmp(subject + len - 3, " SA") == 0) return true;

    return false;
}

bool agent_should_trash(const gcloud_v1_gmail_message_t *msg) {
    if (!msg) return false;
    return false; // for now!

    // Rule 1: Kill the noise
    if (msg->subject) {
        if (strstr(msg->subject, "AMZN:") != NULL || is_standalone_sa(msg->subject)) {
            printf("[Agent] Matched spam filter: %.30s...\n", msg->subject);
            return true;
        }
    }

    // Rule 2: Unread and older than 1 year
    if (has_label(msg, "UNREAD")) {
        uint64_t now_ms = (uint64_t)time(NULL) * 1000;
        uint64_t one_year_ms = 365ULL * 24 * 60 * 60 * 1000;
        
        if (now_ms > msg->internal_date && (now_ms - msg->internal_date) > one_year_ms) {
            printf("[Agent] Rule Match: Unread > 1 year old (Subject: %.30s...)\n", msg->subject ? msg->subject : "");
            return true;
        }
    }

    return false;
}