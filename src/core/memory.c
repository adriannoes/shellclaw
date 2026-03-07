/**
 * @file memory.c
 * @brief SQLite memory store: schema (memories + FTS5, sessions), save/recall, session CRUD.
 */

#include "core/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlite3.h"

#define WARN_RECREATED "Warning: memory DB invalid or corrupted, recreated at %s\n"

static sqlite3 *g_db;

static const char *SCHEMA_MEMORIES =
	"CREATE TABLE IF NOT EXISTS memories ("
	"  rowid INTEGER PRIMARY KEY,"
	"  key TEXT UNIQUE NOT NULL,"
	"  content TEXT NOT NULL,"
	"  metadata TEXT,"
	"  created_at TEXT,"
	"  updated_at TEXT"
	");";

static const char *SCHEMA_MEMORIES_FTS =
	"CREATE VIRTUAL TABLE IF NOT EXISTS memories_fts USING fts5("
	"  content,"
	"  key,"
	"  content='memories',"
	"  content_rowid='rowid'"
	");";

static const char *TRIGGER_MEMORIES_AI =
	"CREATE TRIGGER IF NOT EXISTS memories_ai AFTER INSERT ON memories BEGIN "
	"  INSERT INTO memories_fts(rowid, content, key) VALUES (new.rowid, new.content, new.key); "
	"END;";

static const char *TRIGGER_MEMORIES_AD =
	"CREATE TRIGGER IF NOT EXISTS memories_ad AFTER DELETE ON memories BEGIN "
	"  INSERT INTO memories_fts(memories_fts, rowid, content, key) VALUES ('delete', old.rowid, old.content, old.key); "
	"END;";

static const char *TRIGGER_MEMORIES_AU =
	"CREATE TRIGGER IF NOT EXISTS memories_au AFTER UPDATE ON memories BEGIN "
	"  INSERT INTO memories_fts(memories_fts, rowid, content, key) VALUES ('delete', old.rowid, old.content, old.key); "
	"  INSERT INTO memories_fts(rowid, content, key) VALUES (new.rowid, new.content, new.key); "
	"END;";

static const char *SCHEMA_SESSIONS =
	"CREATE TABLE IF NOT EXISTS sessions ("
	"  id TEXT PRIMARY KEY,"
	"  messages TEXT NOT NULL DEFAULT '[]',"
	"  created_at TEXT,"
	"  updated_at TEXT"
	");";

static int run_schema(void)
{
	char *err = NULL;
	if (sqlite3_exec(g_db, SCHEMA_MEMORIES, NULL, NULL, &err) != SQLITE_OK) goto fail;
	if (sqlite3_exec(g_db, SCHEMA_MEMORIES_FTS, NULL, NULL, &err) != SQLITE_OK) goto fail;
	if (sqlite3_exec(g_db, TRIGGER_MEMORIES_AI, NULL, NULL, &err) != SQLITE_OK) goto fail;
	if (sqlite3_exec(g_db, TRIGGER_MEMORIES_AD, NULL, NULL, &err) != SQLITE_OK) goto fail;
	if (sqlite3_exec(g_db, TRIGGER_MEMORIES_AU, NULL, NULL, &err) != SQLITE_OK) goto fail;
	if (sqlite3_exec(g_db, SCHEMA_SESSIONS, NULL, NULL, &err) != SQLITE_OK) goto fail;
	return 0;
fail:
	if (err) sqlite3_free(err);
	return -1;
}

int memory_init(const char *path)
{
	if (!path || g_db) return -1;
	int recreated = 0;
	if (sqlite3_open(path, &g_db) != SQLITE_OK) {
		if (g_db) { sqlite3_close(g_db); g_db = NULL; }
		remove(path);
		if (sqlite3_open(path, &g_db) != SQLITE_OK) {
			if (g_db) sqlite3_close(g_db);
			g_db = NULL;
			return -1;
		}
		recreated = 1;
	}
	if (run_schema() != 0) {
		sqlite3_close(g_db);
		g_db = NULL;
		remove(path);
		if (sqlite3_open(path, &g_db) != SQLITE_OK) {
			if (g_db) sqlite3_close(g_db);
			g_db = NULL;
			return -1;
		}
		if (run_schema() != 0) {
			sqlite3_close(g_db);
			g_db = NULL;
			return -1;
		}
		recreated = 1;
	}
	if (recreated) fprintf(stderr, WARN_RECREATED, path);
	return 0;
}

int memory_save(const char *key, const char *content, const char *metadata)
{
	if (!g_db || !key || !content) return -1;
	const char *meta = metadata ? metadata : "";
	const char *sql = "INSERT INTO memories(key, content, metadata, created_at, updated_at) "
		"VALUES(?1, ?2, ?3, datetime('now'), datetime('now')) "
		"ON CONFLICT(key) DO UPDATE SET content=excluded.content, metadata=excluded.metadata, updated_at=datetime('now')";
	sqlite3_stmt *stmt = NULL;
	if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
	sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 2, content, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 3, meta, -1, SQLITE_TRANSIENT);
	int ret = sqlite3_step(stmt) == SQLITE_DONE ? 0 : -1;
	sqlite3_finalize(stmt);
	return ret;
}

int memory_recall(const char *query, char *results, size_t max_len, int limit)
{
	if (!g_db || !results || max_len == 0 || !query) return -1;
	results[0] = '\0';
	if (limit <= 0) limit = 10;
	const char *sql = "SELECT content FROM memories_fts WHERE memories_fts MATCH ?1 ORDER BY rank LIMIT ?2";
	sqlite3_stmt *stmt = NULL;
	if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
	sqlite3_bind_text(stmt, 1, query, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 2, limit);
	size_t len = 0;
	while (sqlite3_step(stmt) == SQLITE_ROW && len < max_len - 1) {
		const char *content = (const char *)sqlite3_column_text(stmt, 0);
		if (content) {
			size_t n = strlen(content);
			if (len + n + 2 > max_len) n = max_len - len - 2;
			if (len > 0) { results[len++] = '\n'; results[len++] = '\n'; }
			memcpy(results + len, content, n + 1);
			len += n;
		}
	}
	sqlite3_finalize(stmt);
	return 0;
}

int session_load(const char *session_id, char *messages_out, size_t max_len)
{
	if (!g_db || !session_id || !messages_out || max_len == 0) return -1;
	messages_out[0] = '\0';
	const char *sql = "SELECT messages FROM sessions WHERE id = ?1";
	sqlite3_stmt *stmt = NULL;
	if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
	sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_TRANSIENT);
	int ret = -1;
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		const char *msg = (const char *)sqlite3_column_text(stmt, 0);
		if (msg) {
			size_t n = strlen(msg);
			if (n >= max_len) n = max_len - 1;
			memcpy(messages_out, msg, n + 1);
			messages_out[n] = '\0';
			ret = 0;
		}
	}
	sqlite3_finalize(stmt);
	return ret;
}

int session_save(const char *session_id, const char *messages)
{
	if (!g_db || !session_id || !messages) return -1;
	const char *sql = "INSERT INTO sessions(id, messages, created_at, updated_at) "
		"VALUES(?1, ?2, datetime('now'), datetime('now')) "
		"ON CONFLICT(id) DO UPDATE SET messages=excluded.messages, updated_at=datetime('now')";
	sqlite3_stmt *stmt = NULL;
	if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
	sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 2, messages, -1, SQLITE_TRANSIENT);
	int ret = sqlite3_step(stmt) == SQLITE_DONE ? 0 : -1;
	sqlite3_finalize(stmt);
	return ret;
}

int session_delete(const char *session_id)
{
	if (!g_db || !session_id) return -1;
	const char *sql = "DELETE FROM sessions WHERE id = ?1";
	sqlite3_stmt *stmt = NULL;
	if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
	sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_TRANSIENT);
	int ret = sqlite3_step(stmt) == SQLITE_DONE ? 0 : -1;
	sqlite3_finalize(stmt);
	return ret;
}

void memory_cleanup(void)
{
	if (g_db) {
		sqlite3_close(g_db);
		g_db = NULL;
	}
}
