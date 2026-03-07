/**
 * @file test_cli.c
 * @brief Unit tests for CLI channel: one-shot, poll, send.
 */

#include "channels/channel.h"
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

static void test_cli_channel_exists(void)
{
	const channel_t *ch = channel_cli_get();
	MU_ASSERT(ch != NULL, "channel_cli_get returns non-NULL");
	MU_ASSERT(ch->name != NULL && strcmp(ch->name, "cli") == 0, "cli name is 'cli'");
	MU_ASSERT(ch->init != NULL && ch->poll != NULL && ch->send != NULL && ch->cleanup != NULL,
	          "all vtable pointers set");
}

static void test_cli_one_shot_returns_message(void)
{
	const channel_t *ch = channel_cli_get();
	channel_cli_set_one_shot("test message");
	MU_ASSERT(ch->init(NULL) == 0, "cli init succeeds");
	channel_incoming_msg_t msg;
	memset(&msg, 0, sizeof(msg));
	int r = ch->poll(&msg, 0);
	MU_ASSERT(r == 1, "poll returns 1 (message)");
	MU_ASSERT(msg.session_id != NULL && strstr(msg.session_id, "cli") != NULL, "session_id contains cli");
	MU_ASSERT(msg.text != NULL && strcmp(msg.text, "test message") == 0, "text matches one-shot");
	channel_incoming_msg_clear(&msg);
	ch->cleanup();
}

static void test_cli_one_shot_consumed_after_poll(void)
{
	const channel_t *ch = channel_cli_get();
	channel_cli_set_one_shot("once");
	ch->init(NULL);
	channel_incoming_msg_t msg;
	memset(&msg, 0, sizeof(msg));
	ch->poll(&msg, 0);
	channel_incoming_msg_clear(&msg);
	memset(&msg, 0, sizeof(msg));
	int r = ch->poll(&msg, 0);
	MU_ASSERT(r == 0, "second poll returns 0 (one-shot consumed)");
	channel_incoming_msg_clear(&msg);
	ch->cleanup();
}

static void test_cli_send_succeeds(void)
{
	const channel_t *ch = channel_cli_get();
	ch->init(NULL);
	int r = ch->send("cli:default", "Hello", NULL, 0);
	MU_ASSERT(r == 0, "send returns 0");
	ch->cleanup();
}

int main(void)
{
	MU_RUN(test_cli_channel_exists);
	MU_RUN(test_cli_one_shot_returns_message);
	MU_RUN(test_cli_one_shot_consumed_after_poll);
	MU_RUN(test_cli_send_succeeds);
	printf("%d tests run, %d failed\n", tests_run, tests_failed);
	return tests_failed ? 1 : 0;
}
