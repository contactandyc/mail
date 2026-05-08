// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#ifndef MAIL_AGENT_H
#define MAIL_AGENT_H

#include <stdbool.h>
#include "a-curl-gcloud-plugin/sinks/v1/gmail.h"

// Returns true if the message should be trashed
bool agent_should_trash(const gcloud_v1_gmail_message_t *msg);

#endif