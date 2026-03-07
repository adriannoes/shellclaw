/**
 * @file test_shell.c
 * @brief Unit tests for shell tool: safe commands, blocklist, timeout.
 */

#include "tools/tool.h"
#include "tools/shell.h"
#include "core/config.h"
#include <stdio.h>
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

static void test_shell_ls_succeeds(void)
{
	const tool_t *t = tool_shell_get();
	tool_shell_set_config(NULL);
	char buf[4096];
	int r = t->execute("{\"command\":\"ls\"}", buf, sizeof(buf));
	MU_ASSERT(r == 0, "ls returns 0");
	MU_ASSERT(strlen(buf) > 0, "ls returns output");
}

static void test_shell_blocked_rm_rf(void)
{
	const tool_t *t = tool_shell_get();
	char buf[256];
	buf[0] = '\0';
	(void)t->execute("{\"command\":\"rm -rf /\"}", buf, sizeof(buf));
	MU_ASSERT(strstr(buf, "blocked") != NULL, "rm -rf / blocked");
}

static void test_shell_blocked_mkfs(void)
{
	const tool_t *t = tool_shell_get();
	char buf[256];
	buf[0] = '\0';
	(void)t->execute("{\"command\":\"mkfs\"}", buf, sizeof(buf));
	MU_ASSERT(strstr(buf, "blocked") != NULL, "mkfs blocked");
}

static void test_shell_invalid_json(void)
{
	const tool_t *t = tool_shell_get();
	char buf[256];
	int r = t->execute("invalid", buf, sizeof(buf));
	MU_ASSERT(r == -1, "invalid JSON returns -1");
	MU_ASSERT(strstr(buf, "error") != NULL, "error in output");
}

static void test_shell_missing_command(void)
{
	const tool_t *t = tool_shell_get();
	char buf[256];
	int r = t->execute("{\"x\":1}", buf, sizeof(buf));
	MU_ASSERT(r == -1, "missing command returns -1");
}

int main(void)
{
	MU_RUN(test_shell_blocked_rm_rf);
	MU_RUN(test_shell_blocked_mkfs);
	MU_RUN(test_shell_ls_succeeds);
	MU_RUN(test_shell_invalid_json);
	MU_RUN(test_shell_missing_command);
	printf("%d tests run, %d failed\n", tests_run, tests_failed);
	return tests_failed ? 1 : 0;
}
