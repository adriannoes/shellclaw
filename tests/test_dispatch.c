/**
 * @file test_dispatch.c
 * @brief Unit tests for handle_message slash commands and agent dispatch.
 */
#define _POSIX_C_SOURCE 200809L

#include "channels/channel.h"
#include "core/bootstrap.h"
#include "core/config.h"
#include "core/dispatch.h"
#include "core/memory.h"
#include "providers/provider.h"
#include "tests/test_runner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void bootstrap_set_provider_for_test(const provider_t *provider);
void bootstrap_reset_tools_for_test(void);

#define SEND_BUF_SIZE 4096
static char g_last_session[SEND_BUF_SIZE];
static char g_last_text[SEND_BUF_SIZE];
static int g_send_calls;

static int mock_send(const char *session_id, const char *text,
		     const channel_attachment_t *attachments, size_t attachments_count)
{
	(void)attachments;
	(void)attachments_count;
	g_send_calls++;
	strncpy(g_last_session, session_id ? session_id : "", sizeof(g_last_session) - 1);
	g_last_session[sizeof(g_last_session) - 1] = '\0';
	strncpy(g_last_text, text ? text : "", sizeof(g_last_text) - 1);
	g_last_text[sizeof(g_last_text) - 1] = '\0';
	return 0;
}

static const channel_t mock_channel = {
	.name = "mock",
	.init = NULL,
	.poll = NULL,
	.send = mock_send,
	.cleanup = NULL,
};

static int spy_init(const config_t *cfg)
{
	(void)cfg;
	return 0;
}

static int spy_chat(const provider_message_t *messages, size_t message_count,
		    const provider_tool_def_t *tools, size_t tool_count,
		    provider_response_t *response)
{
	(void)messages;
	(void)message_count;
	(void)tools;
	(void)tool_count;
	response->error = 0;
	response->content = strdup("agent-ok");
	response->tool_calls = NULL;
	response->tool_calls_count = 0;
	return 0;
}

static void spy_cleanup(void) {}

static const provider_t spy_provider = {
	.name = "spy",
	.init = spy_init,
	.chat = spy_chat,
	.cleanup = spy_cleanup,
};

static int fail_chat(const provider_message_t *messages, size_t message_count,
		     const provider_tool_def_t *tools, size_t tool_count,
		     provider_response_t *response)
{
	(void)messages;
	(void)message_count;
	(void)tools;
	(void)tool_count;
	response->error = 1;
	response->content = NULL;
	response->tool_calls = NULL;
	response->tool_calls_count = 0;
	return -3;
}

static const provider_t fail_provider = {
	.name = "fail",
	.init = spy_init,
	.chat = fail_chat,
	.cleanup = spy_cleanup,
};

static config_t *load_minimal_cfg(const char *path)
{
	config_t *cfg = NULL;
	char errbuf[256];

	if (config_load(path, &cfg, errbuf, sizeof(errbuf)) != 0)
		return NULL;
	return cfg;
}

static int write_minimal_toml(const char *path)
{
	FILE *fp = fopen(path, "w");

	if (!fp)
		return -1;
	fprintf(fp,
		"[channels.discord]\n"
		"enabled = false\n"
		"[agent]\n"
		"model = \"stub\"\n"
		"[providers]\n"
		"fallback_chain = [\"stub\"]\n");
	fclose(fp);
	return 0;
}

static void reset_send_spy(void)
{
	g_send_calls = 0;
	g_last_session[0] = '\0';
	g_last_text[0] = '\0';
}

static int test_reset_clears_session(void)
{
	const char *db_path = "build/test_dispatch_reset.db";
	char path[128];
	channel_incoming_msg_t msg = {0};
	config_t *cfg = NULL;
	char history[512];

	reset_send_spy();
	memory_cleanup();
	ASSERT(memory_init(db_path) == 0);
	ASSERT(session_save("ws:test", "[{\"role\":\"user\",\"content\":\"hi\"}]") == 0);

	ASSERT(test_runner_mkstemp_path("shellclaw_test_dispatch", path, sizeof(path)) == 0);
	ASSERT(write_minimal_toml(path) == 0);
	cfg = load_minimal_cfg(path);
	ASSERT(cfg != NULL);
	bootstrap_set_cfg(cfg);
	bootstrap_reset_tools_for_test();

	msg.session_id = "ws:test";
	msg.text = "/reset";
	ASSERT(handle_message(&mock_channel, &msg) == 0);
	ASSERT(g_send_calls == 1);
	ASSERT(strstr(g_last_text, "Session cleared") != NULL);
	ASSERT(session_load("ws:test", history, sizeof(history)) != 0);

	config_free(cfg);
	unlink(path);
	memory_cleanup();
	return 0;
}

static int test_status_returns_version(void)
{
	channel_incoming_msg_t msg = {0};

	reset_send_spy();
	msg.session_id = "cli:test";
	msg.text = "/status";
	ASSERT(handle_message(&mock_channel, &msg) == 0);
	ASSERT(g_send_calls == 1);
	ASSERT(strstr(g_last_text, "ShellClaw 0.2.0") != NULL);
	ASSERT(strstr(g_last_text, "agent ready") != NULL);
	return 0;
}

static int test_agent_failure_fallback_message(void)
{
	channel_incoming_msg_t msg = {0};
	char path[128];
	config_t *cfg = NULL;

	reset_send_spy();
	ASSERT(test_runner_mkstemp_path("shellclaw_test_dispatch_fail", path, sizeof(path)) == 0);
	ASSERT(write_minimal_toml(path) == 0);
	cfg = load_minimal_cfg(path);
	ASSERT(cfg != NULL);
	bootstrap_set_cfg(cfg);
	bootstrap_set_provider_for_test(&fail_provider);
	bootstrap_reset_tools_for_test();

	msg.session_id = "cli:fail";
	msg.text = "hello";
	ASSERT(handle_message(&mock_channel, &msg) == 0);
	ASSERT(g_send_calls == 1);
	ASSERT(strstr(g_last_text, "Error: agent failed (code -1)") != NULL);

	config_free(cfg);
	unlink(path);
	return 0;
}

static int test_normal_message_uses_provider(void)
{
	channel_incoming_msg_t msg = {0};
	char path[128];
	config_t *cfg = NULL;

	reset_send_spy();
	ASSERT(test_runner_mkstemp_path("shellclaw_test_dispatch_spy", path, sizeof(path)) == 0);
	ASSERT(write_minimal_toml(path) == 0);
	cfg = load_minimal_cfg(path);
	ASSERT(cfg != NULL);
	bootstrap_set_cfg(cfg);
	bootstrap_set_provider_for_test(&spy_provider);
	bootstrap_reset_tools_for_test();

	msg.session_id = "cli:spy";
	msg.text = "ping";
	ASSERT(handle_message(&mock_channel, &msg) == 0);
	ASSERT(g_send_calls == 1);
	ASSERT(strstr(g_last_text, "agent-ok") != NULL);

	config_free(cfg);
	unlink(path);
	return 0;
}

static int test_null_text_forwards_empty_to_agent(void)
{
	channel_incoming_msg_t msg = {0};
	char path[128];
	config_t *cfg = NULL;

	reset_send_spy();
	ASSERT(test_runner_mkstemp_path("shellclaw_test_dispatch_null", path, sizeof(path)) == 0);
	ASSERT(write_minimal_toml(path) == 0);
	cfg = load_minimal_cfg(path);
	ASSERT(cfg != NULL);
	bootstrap_set_cfg(cfg);
	bootstrap_set_provider_for_test(&spy_provider);
	bootstrap_reset_tools_for_test();

	msg.session_id = "cli:null";
	msg.text = NULL;
	ASSERT(handle_message(&mock_channel, &msg) == 0);
	ASSERT(g_send_calls == 1);
	ASSERT(strstr(g_last_text, "agent-ok") != NULL);

	config_free(cfg);
	unlink(path);
	return 0;
}

int main(void)
{
	RUN(test_reset_clears_session());
	RUN(test_status_returns_version());
	RUN(test_agent_failure_fallback_message());
	RUN(test_normal_message_uses_provider());
	RUN(test_null_text_forwards_empty_to_agent());
	printf("test_dispatch: all tests passed\n");
	return 0;
}
