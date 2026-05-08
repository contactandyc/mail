That makes complete sense. Stripping out the marketing fluff and framing it accurately as an educational proof-of-concept gives it a much better, more authentic tone.

Here is a revised `README.md` that frames the project as a sample application demonstrating asynchronous queues and API triage patterns.

***

# Gmail Sync & Active Agent Demo (`mail`)

This is a sample asynchronous C daemon designed to demonstrate how to monitor, triage, and locally mirror a Google Workspace / Gmail inbox.

It serves as a proof-of-concept for building stateful, queue-backed background workers. Rather than downloading full email payloads immediately, this demo explores a **3-Phase Sync Architecture**—using SQLite to stage downloads, filter emails via lightweight metadata, and execute simple active agent rules (like auto-trashing spam) before committing to heavy HTTP downloads.

*Note: This is an experimental sample application, not a production-ready email client. It is intended for educational purposes and testing asynchronous C patterns.*

## Concepts Demonstrated

* **Queue-Backed State:** Demonstrates decoupling the event loop's state from memory. Tasks are popped from and re-queued into SQLite staging tables (`pending_triage`, `pending_hydration`). If the process is killed, it resumes from the database queues on the next boot.
* **3-Phase Download Engine:** 1. **Discovery:** Fetches pages of IDs via `messages.list` to map the remote state and catch deletions.
    2. **Triage:** Fetches lightweight `format=metadata` payloads to feed the rules engine, discarding matched junk early.
    3. **Hydration:** Fetches `format=full` payloads only for emails that survive triage.
* **Incremental Delta Sync:** Leverages the Gmail `history.list` API to fetch new arrivals and remote deletions without performing full-syncs.
* **Asynchronous Rate Limiting:** Shows how to balance bulk background operations (fetching hundreds of emails) with immediate agent actions (sending/trashing) using priority queues and a token-bucket rate manager.
* **Zero-Friction OAuth:** Uses `an-oauth-library` to handle localhost browser authentication, token refreshes, and scope validation from the terminal.

## Project Structure

* **`src/main.c`**: The entry point. Bootstraps OAuth, initializes the rate limiter, sends a sample startup confirmation email, and kicks off the sync engine.
* **`src/sync.c`**: The orchestrator. Contains the `sync_tick` loop, draining the SQLite queues and handling the asynchronous Curl callbacks for the 3-phase and delta syncs.
* **`src/db.c`**: The storage layer. Wraps SQLite to manage the final storage tables (`messages`, `threads`, `message_attachments`) and the temporary staging queues (`remote_state`, `pending_triage`, `pending_hydration`).
* **`src/agent.c`**: The rules engine sandbox. A simple module containing business logic to decide if an email should be trashed based on its metadata.
* **`src/test_send.c`**: A standalone utility for validating your OAuth token and Base64 MIME encoding by sending a test email outside of the main sync loop.

## Setup & Execution

### Prerequisites
1. Ensure your Google Cloud Project has the Gmail API enabled.
2. Download your OAuth 2.0 Desktop Client credentials and save them in the project root as `credentials.json`.
3. The demo requires the `https://mail.google.com/` scope for full read/write/trash access.

### Running the Demo
Run the compiled executable. If a valid `config.json` is not found, the program will open your default web browser to capture an OAuth consent grant via a temporary local web server (port `8080`).

```bash
./build/mail
```

Once authenticated, the daemon will:
1. Send a "System Online" email to your authenticated address.
2. Begin **Phase 1: Discovery**, gathering the IDs of emails in your inbox (excluding `IN:TRASH` and `IN:SPAM`).
3. Transition into **Phase 2: Triage** to analyze metadata, eventually leading to **Phase 3: Hydration**.

## Playing with the Active Agent

The Active Agent is a playground for adding automated email filters. To add new behaviors, modify the `agent_should_trash()` function in `src/agent.c`.

**Current Sample Rules:**
* **Keyword Matching:** Trashes incoming emails with `AMZN:` in the subject or `SA` as a standalone word.
* **Inbox Expiration:** Trashes any email holding the `UNREAD` label that is older than 1 year.

### Example: Trashing a specific sender
You can easily expand the agent to check headers like the sender's address:
```c
bool agent_should_trash(const gcloud_v1_gmail_message_t *msg) {
    if (!msg) return false;

    // ... existing rules ...

    // Rule 3: Trash emails from a specific domain
    if (msg->from && msg->from->email) {
        if (strstr(msg->from->email, "@example-marketing.com") != NULL) {
            printf("[Agent] Trashing email from: %s\n", msg->from->email);
            return true;
        }
    }

    return false; // Keep email, move to Hydration
}
```
*Note: Because the program uses database queues, you can safely `Ctrl+C` the process, recompile your new agent rules, and restart. It will pick up exactly where it left off in the triage queue.*

## Database Schema Overview

The resulting `mail.db` SQLite database is normalized to separate the permanent data from the transient sync queues.

* **Final Storage:**
    * `messages`: Contains `internal_date`, `subject`, `snippet`, and parsed `from` data.
    * `threads`: Tracks conversation ID clusters.
    * `message_labels`: A mapping table for Gmail's categorical labels.
    * `message_attachments`: Stores attachment metadata and the raw `file_data` (BLOB).
* **Sync State:**
    * `remote_state`: Temporary holding table used during Phase 1 to reconcile deletions.
    * `pending_triage` & `pending_hydration`: The worker queues that drive Phases 2 and 3.

## License

Apache-2.0 © 2026 Andy Curtis
