/**
 * @file test_dispatch.c
 * @brief Unit tests for handle_message slash commands and dispatch wiring.
 */

#include "test_runner.h"
#include "core/dispatch.h"
#include "core/memory.h"
#include "channels/channel.h"
#include <stdio.h>
#include <string.h>

static char spy_session_id[64];
static char spy_text[512];
static int spy_send_calls;

static int spy_send(const char *session_id, const char *text,
                    const channel_attachment_t *attachments, size_t att_count)
{
	(void)attachments;
	(void)att_count;
	spy_send_calls++;
	if (session_id)
		strncpy(spy_session_id, session_id, sizeof(spy_session_id) - 1);
	else
		spy_session_id[0] = '\0';
	if (text)
		strncpy(spy_text, text, sizeof(spy_text) - 1);
	else
		spy_text[0] = '\0';
	spy_session_id[sizeof(spy_session_id) - 1] = '\0';
	spy_text[sizeof(spy_text) - 1] = '\0';
	return 0;
}

static const channel_t spy_channel = {
	.name = "spy",
	.init = NULL,
	.poll = NULL,
	.send = spy_send,
	.cleanup = NULL,
};

static int test_status_command_replies(void)
{
	channel_incoming_msg_t msg;

	spy_send_calls = 0;
	memset(&msg, 0, sizeof(msg));
	msg.session_id = "sess-status";
	msg.text = "/status";
	ASSERT(handle_message(&spy_channel, &msg) == 0);
	ASSERT(spy_send_calls == 1);
	ASSERT(strstr(spy_text, "ShellClaw") != NULL);
	ASSERT(strstr(spy_text, "agent ready") != NULL);
	return 0;
}

static int test_reset_command_clears_session(void)
{
	char db_path[128];
	channel_incoming_msg_t msg;

	ASSERT(test_runner_mkstemp_path("shellclaw_test_dispatch", db_path, sizeof(db_path)) == 0);
	ASSERT(memory_init(db_path) == 0);
	ASSERT(session_save("sess-reset", "[]") == 0);

	spy_send_calls = 0;
	memset(&msg, 0, sizeof(msg));
	msg.session_id = "sess-reset";
	msg.text = "/reset";
	ASSERT(handle_message(&spy_channel, &msg) == 0);
	ASSERT(spy_send_calls == 1);
	ASSERT(strcmp(spy_text, "Session cleared.") == 0);
	ASSERT(session_delete("sess-reset") != 0);

	memory_cleanup();
	remove(db_path);
	return 0;
}

static int test_null_text_treated_as_empty(void)
{
	channel_incoming_msg_t msg;

	spy_send_calls = 0;
	memset(&msg, 0, sizeof(msg));
	msg.session_id = "sess-empty";
	msg.text = NULL;
	ASSERT(handle_message(&spy_channel, &msg) == 0);
	ASSERT(spy_send_calls == 1);
	return 0;
}

int main(void)
{
	RUN(test_status_command_replies());
	RUN(test_reset_command_clears_session());
	RUN(test_null_text_treated_as_empty());
	printf("test_dispatch: all tests passed\n");
	return 0;
}
