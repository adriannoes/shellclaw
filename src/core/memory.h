/**
 * @file memory.h
 * @brief SQLite-backed memory store: long-term memories (FTS5) and session history.
 */

#ifndef SHELLCLAW_MEMORY_H
#define SHELLCLAW_MEMORY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize memory store at the given DB path. Creates file and schema if missing.
 *
 * @param path Path to SQLite database (e.g. ~/.shellclaw/memory.db); caller must expand ~.
 * @return 0 on success, non-zero on error.
 */
int memory_init(const char *path);

/**
 * Save or update a long-term memory by key.
 *
 * @param key     Unique key for the memory.
 * @param content Text content (stored and indexed for FTS5).
 * @param metadata Optional JSON metadata (may be NULL).
 * @return 0 on success, non-zero on error.
 */
int memory_save(const char *key, const char *content, const char *metadata);

/**
 * Recall memories matching the query using FTS5 (BM25). Writes up to max_len bytes
 * into results; multiple memories are formatted as newline-separated or JSON array.
 *
 * @param query   FTS5 search query.
 * @param results Output buffer.
 * @param max_len Size of results buffer.
 * @param limit   Maximum number of memories to return.
 * @return 0 on success, non-zero on error.
 */
int memory_recall(const char *query, char *results, size_t max_len, int limit);

/**
 * Load session messages by session ID (e.g. "cli:default" or "telegram:123456789").
 *
 * @param session_id   Session identifier.
 * @param messages_out Output buffer for JSON array of messages; caller must free if allocated.
 * @param max_len      Size of messages_out buffer (or 0 if messages_out is to be allocated by implementation).
 * @return 0 on success, non-zero if not found or error.
 */
int session_load(const char *session_id, char *messages_out, size_t max_len);

/**
 * Save (upsert) session messages by session ID.
 *
 * @param session_id Session identifier.
 * @param messages   JSON array of messages.
 * @return 0 on success, non-zero on error.
 */
int session_save(const char *session_id, const char *messages);

/**
 * Delete a session by ID.
 *
 * @param session_id Session identifier.
 * @return 0 on success, non-zero on error.
 */
int session_delete(const char *session_id);

/**
 * List session IDs from the database.
 *
 * @param session_ids_out Array of pointers to receive session IDs; caller must free each.
 * @param max_count       Maximum number of session IDs to return.
 * @return Number of sessions returned, or -1 on error.
 */
int session_list(char **session_ids_out, int max_count);

/**
 * Release resources and close the database. Safe to call multiple times.
 */
void memory_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_MEMORY_H */
