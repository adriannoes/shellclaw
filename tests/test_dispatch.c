/**
 * @file test_dispatch.c
 * @brief Unit tests for handle_message slash commands and agent dispatch.
 */
#define _POSIX_C_SOURCE 200809L

#include "test_runner.h"
#include "channels/channel.h"
#include "core/config.h"
#include "core/dispatch.h"
#include "core/memory.h"
#include "providers/provider.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void bootstrap_set_test_state(config_t *cfg, const provider_t *provider);

static char g_last_session[128];
static char g_last_response[4096];
static int g_send_calls;

static int mock_send(const char *session_id, const char *text,
		     const char *attachments_json, size_t attachments_count)
{
	(void)attachments_json;
	(void)attachments_count;
	g_send_calls++;
	if (session_id)
		strncpy(g_last_session, session_id, sizeof(g_last_session) - 1);
	g_last_session[sizeof(g_last_session) - 1] = '\0';
	if (text)
		strncpy(g_last_response, text, sizeof(g_last_response) - 1);
	g_last_response[sizeof(g_last_response) - 1] = '\0';
	return 0;
}

static const channel_t mock_channel = {
	.name = "mock",
	.init = NULL,
	.poll = NULL,
	.send = mock_send,
	.cleanup = NULL,
};

static int spy_chat(const provider_message_t *messages, size_t message_count,
		    const provider_tool_def_t *tools, size_t tool_count,
		    provider_response_t *response)
{
	(void)tools;
	(void)tool_count;
	response->error = 0;
	response->tool_calls = NULL;
	response->tool_calls_count = 0;
	if (message_count > 0 && messages[message_count - 1].content)
		response->content = strdup(messages[message_count - 1].content);
	else
		response->content = strdup("ok");
	return 0;
}

static int spy_init(const config_t *cfg)
{
	(void)cfg;
	return 0;
}

static void spy_cleanup(void) {}

static const provider_t spy_provider = {
	.name = "spy",
	.init = spy_init,
	.chat = spy_chat,
	.cleanup = spy_cleanup,
};

static int write_dispatch_config(const char *path)
{
	FILE *f = fopen(path, "w");
	if (!f)
		return -1;
	fprintf(f, "[agent]\nmodel = \"dispatch-test\"\n");
	fprintf(f, "[providers]\nfallback_chain = [ \"stub\" ]\n");
	fprintf(f, "[memory]\ndb_path = \"%s\"\n", path);
	fclose(f);
	return 0;
}

static int test_status_command_returns_version(void)
{
	channel_incoming_msg_t msg;
	memset(&msg, 0, sizeof(msg));
	msg.session_id = "sess-status";
	msg.text = "/status";
	g_send_calls = 0;
	ASSERT(handle_message(&mock_channel, &msg) == 0);
	ASSERT(g_send_calls == 1);
	ASSERT(strstr(g_last_response, "ShellClaw") != NULL);
	ASSERT(strstr(g_last_response, "agent ready") != NULL);
	return 0;
}

static int test_reset_command_clears_session(void)
{
	char db_path[128];
	char cfg_path[128];
	config_t *cfg = NULL;
	char errbuf[256];

	ASSERT(test_runner_mkstemp_path("shellclaw_test_dispatch_db", db_path, sizeof(db_path)) == 0);
	ASSERT(test_runner_mkstemp_path("shellclaw_test_dispatch_cfg", cfg_path, sizeof(cfg_path)) == 0);
	FILE *f = fopen(cfg_path, "w");
	ASSERT(f);
	fprintf(f, "[agent]\nmodel = \"dispatch-test\"\n[memory]\ndb_path = \"%s\"\n", db_path);
	fclose(f);
	ASSERT(config_load(cfg_path, &cfg, errbuf, sizeof(errbuf)) == 0);
	ASSERT(memory_init(db_path) == 0);
	ASSERT(session_save("sess-reset", "[{\"role\":\"user\",\"content\":\"hi\"}]") == 0);

	channel_incoming_msg_t msg;
	memset(&msg, 0, sizeof(msg));
	msg.session_id = "sess-reset";
	msg.text = "/reset";
	g_send_calls = 0;
	ASSERT(handle_message(&mock_channel, &msg) == 0);
	ASSERT(g_send_calls == 1);
	ASSERT(strcmp(g_last_response, "Session cleared.") == 0);

	char *history = NULL;
	ASSERT(session_load("sess-reset", &history) == 0);
	ASSERT(history == NULL);
	free(history);
	memory_cleanup();
	config_free(cfg);
	remove(db_path);
	remove(cfg_path);
	return 0;
}

static int test_agent_message_forwards_provider_response(void)
{
	char cfg_path[128];
	config_t *cfg = NULL;
	char errbuf[256];

	ASSERT(test_runner_mkstemp_path("shellclaw_test_dispatch_agent", cfg_path, sizeof(cfg_path)) == 0);
	ASSERT(write_dispatch_config(cfg_path) == 0);
	ASSERT(config_load(cfg_path, &cfg, errbuf, sizeof(errbuf)) == 0);
	bootstrap_set_test_state(cfg, &spy_provider);

	channel_incoming_msg_t msg;
	memset(&msg, 0, sizeof(msg));
	msg.session_id = "sess-agent";
	msg.text = "hello dispatch";
	g_send_calls = 0;
	ASSERT(handle_message(&mock_channel, &msg) == 0);
	ASSERT(g_send_calls == 1);
	ASSERT(strcmp(g_last_response, "hello dispatch") == 0);

	bootstrap_set_test_state(NULL, NULL);
	config_free(cfg);
	remove(cfg_path);
	return 0;
}

int main(void)
{
	RUN(test_status_command_returns_version());
	RUN(test_reset_command_clears_session());
	RUN(test_agent_message_forwards_provider_response());
	printf("test_dispatch: all tests passed\n");
	return 0;
}
