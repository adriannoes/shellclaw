/**
 * @file test_asap_log.c
 * @brief Unit tests for asap_log ring buffer: eviction, direction, snapshot.
 */
#define _POSIX_C_SOURCE 200809L

#include "asap/log.h"
#include <stdio.h>
#include <string.h>

#define ASSERT(c) do { \
	if (!(c)) { \
		fprintf(stderr, "FAIL: %s:%d %s\n", __FILE__, __LINE__, #c); \
		return 1; \
	} \
} while (0)

static int test_append_and_snapshot_basic(void)
{
	asap_log_entry_t entries[ASAP_LOG_RING_SIZE];
	int n;
	asap_log_reset();
	asap_log_append_in("task.request", "id-1", "{\"a\":1}");
	asap_log_append_out("task.response", "id-2", "{\"b\":2}");
	n = asap_log_get_snapshot(entries, ASAP_LOG_RING_SIZE);
	ASSERT(n == 2);
	ASSERT(entries[0].direction == ASAP_LOG_DIR_IN);
	ASSERT(strcmp(entries[0].payload_type, "task.request") == 0);
	ASSERT(strcmp(entries[0].id, "id-1") == 0);
	ASSERT(strcmp(entries[0].json_snippet, "{\"a\":1}") == 0);
	ASSERT(entries[1].direction == ASAP_LOG_DIR_OUT);
	ASSERT(strcmp(entries[1].payload_type, "task.response") == 0);
	return 0;
}

static int test_eviction_at_ring_size(void)
{
	asap_log_entry_t entries[ASAP_LOG_RING_SIZE + 1];
	int n;
	int i;
	char id_buf[32];
	asap_log_reset();
	for (i = 0; i < ASAP_LOG_RING_SIZE; i++) {
		snprintf(id_buf, sizeof id_buf, "id-%d", i);
		asap_log_append_in("state.query", id_buf, "{}");
	}
	n = asap_log_get_snapshot(entries, ASAP_LOG_RING_SIZE + 1);
	ASSERT(n == ASAP_LOG_RING_SIZE);
	ASSERT(strcmp(entries[0].id, "id-0") == 0);
	ASSERT(strcmp(entries[ASAP_LOG_RING_SIZE - 1].id, "id-49") == 0);
	asap_log_append_in("state.query", "id-50", "{}");
	n = asap_log_get_snapshot(entries, ASAP_LOG_RING_SIZE + 1);
	ASSERT(n == ASAP_LOG_RING_SIZE);
	ASSERT(strcmp(entries[0].id, "id-1") == 0);
	ASSERT(strcmp(entries[ASAP_LOG_RING_SIZE - 1].id, "id-50") == 0);
	return 0;
}

static int test_eviction_many_wraps(void)
{
	asap_log_entry_t entries[ASAP_LOG_RING_SIZE];
	int n;
	int i;
	char id_buf[32];
	int total = ASAP_LOG_RING_SIZE * 3 + 5;
	asap_log_reset();
	for (i = 0; i < total; i++) {
		snprintf(id_buf, sizeof id_buf, "id-%d", i);
		asap_log_append_out("mcp.tool_result", id_buf, "{}");
	}
	n = asap_log_get_snapshot(entries, ASAP_LOG_RING_SIZE);
	ASSERT(n == ASAP_LOG_RING_SIZE);
	snprintf(id_buf, sizeof id_buf, "id-%d", total - ASAP_LOG_RING_SIZE);
	ASSERT(strcmp(entries[0].id, id_buf) == 0);
	snprintf(id_buf, sizeof id_buf, "id-%d", total - 1);
	ASSERT(strcmp(entries[ASAP_LOG_RING_SIZE - 1].id, id_buf) == 0);
	return 0;
}

static int test_null_fields_handled(void)
{
	asap_log_entry_t entries[2];
	int n;
	asap_log_reset();
	asap_log_append_in(NULL, NULL, NULL);
	n = asap_log_get_snapshot(entries, 2);
	ASSERT(n == 1);
	ASSERT(entries[0].payload_type[0] == '\0');
	ASSERT(entries[0].id[0] == '\0');
	ASSERT(entries[0].json_snippet[0] == '\0');
	return 0;
}

static int test_snapshot_max_limit(void)
{
	asap_log_entry_t entries[5];
	int n;
	int i;
	char id_buf[32];
	asap_log_reset();
	for (i = 0; i < 10; i++) {
		snprintf(id_buf, sizeof id_buf, "id-%d", i);
		asap_log_append_in("task.request", id_buf, "{}");
	}
	n = asap_log_get_snapshot(entries, 5);
	ASSERT(n == 5);
	ASSERT(strcmp(entries[0].id, "id-0") == 0);
	ASSERT(strcmp(entries[4].id, "id-4") == 0);
	return 0;
}

static int test_timestamp_is_set(void)
{
	asap_log_entry_t entries[1];
	int n;
	asap_log_reset();
	asap_log_append_in("task.cancel", "id-ts", "{}");
	n = asap_log_get_snapshot(entries, 1);
	ASSERT(n == 1);
	ASSERT(entries[0].ts > 0);
	return 0;
}

int main(void)
{
	int r = 0;
	r |= test_append_and_snapshot_basic();
	r |= test_eviction_at_ring_size();
	r |= test_eviction_many_wraps();
	r |= test_null_fields_handled();
	r |= test_snapshot_max_limit();
	r |= test_timestamp_is_set();
	if (r == 0) printf("test_asap_log: all tests passed\n");
	return r;
}
