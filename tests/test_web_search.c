/**
 * @file test_web_search.c
 * @brief Unit tests for web_search tool: parameter validation, vtable, edge cases.
 *
 * Note: actual HTTP requests are not tested here (would need mocking).
 * These tests cover input parsing, error handling, and tool vtable integrity.
 */
#define _POSIX_C_SOURCE 200809L

#include "tools/tool.h"
#include "tools/web_search.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_failed = 0;

#define MU_ASSERT(cond, msg) do { \
	tests_run++; \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", (msg)); \
		tests_failed++; \
		return; \
	} \
} while (0)

#define MU_RUN(test) do { test(); } while (0)

static void test_vtable_fields(void)
{
	const tool_t *t = tool_web_search_get();
	MU_ASSERT(t != NULL, "tool not null");
	MU_ASSERT(t->name != NULL && strcmp(t->name, "web_search") == 0, "name is web_search");
	MU_ASSERT(t->description != NULL && strlen(t->description) > 0, "has description");
	MU_ASSERT(t->parameters_json != NULL && strlen(t->parameters_json) > 0, "has parameters_json");
	MU_ASSERT(t->execute != NULL, "execute not null");
}

static void test_invalid_json_args(void)
{
	const tool_t *t = tool_web_search_get();
	char buf[256];
	int r = t->execute("not json", buf, sizeof(buf));
	MU_ASSERT(r == -1, "invalid JSON returns -1");
	MU_ASSERT(strstr(buf, "error") != NULL, "error message in buffer");
}

static void test_missing_query_field(void)
{
	const tool_t *t = tool_web_search_get();
	char buf[256];
	int r = t->execute("{\"q\":\"wrong field\"}", buf, sizeof(buf));
	MU_ASSERT(r == -1, "missing 'query' returns -1");
	MU_ASSERT(strstr(buf, "query") != NULL, "error mentions query");
}

static void test_empty_query(void)
{
	const tool_t *t = tool_web_search_get();
	char buf[4096];
	/* Empty string query — the DuckDuckGo request will happen but return no useful results.
	   Without mocking curl, we just verify it doesn't crash and returns something. */
	int r = t->execute("{\"query\":\"\"}", buf, sizeof(buf));
	/* Could be 0 (DuckDuckGo returns something) or -1 (network error). Either is fine. */
	MU_ASSERT(r == 0 || r == -1, "empty query does not crash");
	MU_ASSERT(strlen(buf) > 0, "some output produced");
}

static void test_null_args(void)
{
	const tool_t *t = tool_web_search_get();
	char buf[256];
	int r = t->execute(NULL, buf, sizeof(buf));
	MU_ASSERT(r == -1, "NULL args returns -1");
}

static void test_null_result_buf(void)
{
	const tool_t *t = tool_web_search_get();
	int r = t->execute("{\"query\":\"test\"}", NULL, 0);
	MU_ASSERT(r == -1, "NULL result_buf returns -1");
}

static void test_zero_max_len(void)
{
	const tool_t *t = tool_web_search_get();
	char buf[1];
	int r = t->execute("{\"query\":\"test\"}", buf, 0);
	MU_ASSERT(r == -1, "zero max_len returns -1");
}

static void test_query_number_type(void)
{
	const tool_t *t = tool_web_search_get();
	char buf[256];
	int r = t->execute("{\"query\":42}", buf, sizeof(buf));
	MU_ASSERT(r == -1, "numeric query returns -1 (not string)");
	MU_ASSERT(strstr(buf, "query") != NULL, "error mentions query");
}

int main(void)
{
	MU_RUN(test_vtable_fields);
	MU_RUN(test_invalid_json_args);
	MU_RUN(test_missing_query_field);
	MU_RUN(test_empty_query);
	MU_RUN(test_null_args);
	MU_RUN(test_null_result_buf);
	MU_RUN(test_zero_max_len);
	MU_RUN(test_query_number_type);
	printf("%d tests run, %d failed\n", tests_run, tests_failed);
	return tests_failed ? 1 : 0;
}
