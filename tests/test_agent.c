/**
 * @file test_agent.c
 * @brief Unit tests for agent_run: API, stub, context assembly, ReAct loop (Tasks 5.1, 5.2, 5.3).
 */

#include "core/agent.h"
#include "core/config.h"
#include "core/memory.h"
#include "providers/provider.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
#define RUN(t) do { int r = (t); if (r) return r; } while (0)

#define SPY_CONTENT_SIZE 4096
#define SPY_SLOTS 8
static size_t spy_message_count;
static char spy_content[SPY_SLOTS][SPY_CONTENT_SIZE];
static const char *spy_roles[SPY_SLOTS];

static int spy_init(const config_t *cfg) { (void)cfg; return 0; }
static int spy_chat(const provider_message_t *messages, size_t message_count,
	const provider_tool_def_t *tools, size_t tool_count, provider_response_t *response)
{
	(void)tools;
	(void)tool_count;
	response->error = 0;
	response->content = strdup("ok");
	response->tool_calls = NULL;
	response->tool_calls_count = 0;
	spy_message_count = message_count;
	for (size_t i = 0; i < message_count && i < SPY_SLOTS; i++) {
		spy_roles[i] = messages[i].role;
		if (messages[i].content) {
			size_t n = strlen(messages[i].content);
			if (n >= SPY_CONTENT_SIZE) n = SPY_CONTENT_SIZE - 1;
			memcpy(spy_content[i], messages[i].content, n + 1);
		} else
			spy_content[i][0] = '\0';
	}
	return 0;
}
static void spy_cleanup(void) {}
static const provider_t spy_provider = {
	.name = "spy",
	.init = spy_init,
	.chat = spy_chat,
	.cleanup = spy_cleanup,
};

static int tool_then_text_call_count;
static int tool_then_text_init(const config_t *cfg) { (void)cfg; tool_then_text_call_count = 0; return 0; }
static int tool_then_text_chat(const provider_message_t *messages, size_t message_count,
	const provider_tool_def_t *tools, size_t tool_count, provider_response_t *response)
{
	(void)messages;
	(void)message_count;
	(void)tools;
	(void)tool_count;
	response->error = 0;
	response->tool_calls = NULL;
	response->tool_calls_count = 0;
	response->content = NULL;
	tool_then_text_call_count++;
	if (tool_then_text_call_count == 1) {
		response->tool_calls = malloc(sizeof(provider_tool_call_t));
		if (response->tool_calls) {
			response->tool_calls[0].id = strdup("tc1");
			response->tool_calls[0].name = strdup("echo");
			response->tool_calls[0].arguments = strdup("{}");
			response->tool_calls_count = 1;
		}
		response->content = strdup("");
	} else {
		response->content = strdup("final text");
	}
	return 0;
}
static void tool_then_text_cleanup(void) {}
static const provider_t tool_then_text_provider = {
	.name = "tool_then_text",
	.init = tool_then_text_init,
	.chat = tool_then_text_chat,
	.cleanup = tool_then_text_cleanup,
};

static int always_tool_call_count;
static int always_tool_init(const config_t *cfg) { (void)cfg; always_tool_call_count = 0; return 0; }
static int always_tool_chat(const provider_message_t *messages, size_t message_count,
	const provider_tool_def_t *tools, size_t tool_count, provider_response_t *response)
{
	(void)messages;
	(void)message_count;
	(void)tools;
	(void)tool_count;
	response->error = 0;
	always_tool_call_count++;
	response->content = strdup("partial");
	response->tool_calls = malloc(sizeof(provider_tool_call_t));
	if (!response->tool_calls) {
		free(response->content);
		response->content = NULL;
		response->error = 1;
		return -1;
	}
	response->tool_calls[0].id = strdup("tid");
	response->tool_calls[0].name = strdup("echo");
	response->tool_calls[0].arguments = strdup("{}");
	response->tool_calls_count = 1;
	return 0;
}
static void always_tool_cleanup(void) {}
static const provider_t always_tool_provider = {
	.name = "always_tool",
	.init = always_tool_init,
	.chat = always_tool_chat,
	.cleanup = always_tool_cleanup,
};

static int mock_echo_execute(const char *args_json, char *result_buf, size_t max_len)
{
	(void)args_json;
	if (max_len > 0) {
		strncpy(result_buf, "echo result", max_len - 1);
		result_buf[max_len - 1] = '\0';
	}
	return 0;
}
static const agent_tool_t mock_echo_tool = {
	.name = "echo",
	.description = "Echo test",
	.parameters_json = "{}",
	.execute = mock_echo_execute,
};

static int test_agent_run_with_stub_and_no_tools(void)
{
	const char *path = "/tmp/shellclaw_test_agent_config.toml";
	FILE *f = fopen(path, "w");
	ASSERT(f);
	fprintf(f, "[agent]\nmodel = \"test\"\n");
	fclose(f);
	config_t *cfg = NULL;
	char errbuf[256];
	ASSERT(config_load(path, &cfg, errbuf, sizeof(errbuf)) == 0);
	ASSERT(cfg != NULL);
	const provider_t *provider = provider_stub_get();
	ASSERT(provider != NULL);
	char response_buf[4096];
	int ret = agent_run(cfg, "cli:test", "Hello", provider, NULL, 0, response_buf, sizeof(response_buf));
	ASSERT(ret == 0);
	/* Stub may leave response_buf empty; success is sufficient for 5.1. */
	config_free(cfg);
	remove(path);
	return 0;
}

static int test_agent_run_invalid_args_returns_error(void)
{
	const char *path = "/tmp/shellclaw_test_agent_invalid.toml";
	FILE *f = fopen(path, "w");
	ASSERT(f);
	fprintf(f, "[agent]\nmodel = \"x\"\n");
	fclose(f);
	config_t *cfg = NULL;
	config_load(path, &cfg, NULL, 0);
	const provider_t *provider = provider_stub_get();
	char buf[256];
	ASSERT(agent_run(NULL, "s", "Hi", provider, NULL, 0, buf, sizeof(buf)) != 0);
	ASSERT(agent_run(cfg, NULL, "Hi", provider, NULL, 0, buf, sizeof(buf)) != 0);
	ASSERT(agent_run(cfg, "s", NULL, provider, NULL, 0, buf, sizeof(buf)) != 0);
	ASSERT(agent_run(cfg, "s", "Hi", NULL, NULL, 0, buf, sizeof(buf)) != 0);
	ASSERT(agent_run(cfg, "s", "Hi", provider, NULL, 0, NULL, 256) != 0);
	ASSERT(agent_run(cfg, "s", "Hi", provider, NULL, 0, buf, 0) != 0);
	config_free(cfg);
	remove(path);
	return 0;
}

static int test_context_assembly_system_prompt_history_memories(void)
{
	const char *db_path = "build/test_agent_ctx.db";
	const char *skills_dir = "build/test_agent_skills";
	const char *config_path = "build/test_agent_ctx.toml";
	memory_cleanup();
	ASSERT(memory_init(db_path) == 0);
	ASSERT(session_save("test_sess", "[{\"role\":\"user\",\"content\":\"old user\"},{\"role\":\"assistant\",\"content\":\"old assistant\"}]") == 0);
	/* Memory content must match FTS5 query (user message "new message") to be recalled. */
	ASSERT(memory_save("pref", "User likes coffee. New message context.", NULL) == 0);
#ifdef _WIN32
	ASSERT(_mkdir(skills_dir) == 0);
#else
	ASSERT(mkdir(skills_dir, 0755) == 0);
#endif
	char skill_path[512];
	snprintf(skill_path, sizeof(skill_path), "%s/skill.md", skills_dir);
	FILE *sf = fopen(skill_path, "w");
	ASSERT(sf);
	fprintf(sf, "Test skill line for context.");
	fclose(sf);
	FILE *cf = fopen(config_path, "w");
	ASSERT(cf);
	fprintf(cf, "[agent]\nmodel = \"test\"\n[memory]\npath = \"%s\"\n[skills]\ndir = \"%s\"\n", db_path, skills_dir);
	fclose(cf);
	config_t *cfg = NULL;
	char errbuf[256];
	ASSERT(config_load(config_path, &cfg, errbuf, sizeof(errbuf)) == 0);
	ASSERT(cfg != NULL);
	char response_buf[4096];
	int ret = agent_run(cfg, "test_sess", "new message", &spy_provider, NULL, 0, response_buf, sizeof(response_buf));
	ASSERT(ret == 0);
	ASSERT(spy_message_count >= 4);
	ASSERT(spy_roles[0] && strcmp(spy_roles[0], "system") == 0);
	ASSERT(strstr(spy_content[0], "User likes coffee") != NULL);
	ASSERT(strstr(spy_content[0], "Test skill line for context.") != NULL);
	ASSERT(strstr(spy_content[1], "old user") != NULL);
	ASSERT(strstr(spy_content[2], "old assistant") != NULL);
	ASSERT(spy_roles[3] && strcmp(spy_roles[3], "user") == 0);
	ASSERT(strstr(spy_content[3], "new message") != NULL);
	config_free(cfg);
	remove(config_path);
	remove(skill_path);
	rmdir(skills_dir);
	remove(db_path);
	memory_cleanup();
	return 0;
}

static int test_react_loop_tool_then_text(void)
{
	const char *path = "build/test_agent_react.toml";
	FILE *f = fopen(path, "w");
	ASSERT(f);
	fprintf(f, "[agent]\nmodel = \"test\"\nmax_tool_iterations = 5\n");
	fclose(f);
	config_t *cfg = NULL;
	char errbuf[256];
	ASSERT(config_load(path, &cfg, errbuf, sizeof(errbuf)) == 0);
	ASSERT(cfg != NULL);
	const agent_tool_t *tools = &mock_echo_tool;
	size_t tool_count = 1;
	char response_buf[4096];
	response_buf[0] = '\0';
	int ret = agent_run(cfg, "cli:react", "hi", &tool_then_text_provider, tools, tool_count, response_buf, sizeof(response_buf));
	ASSERT(ret == 0);
	ASSERT(strstr(response_buf, "final text") != NULL);
	ASSERT(tool_then_text_call_count == 2);
	config_free(cfg);
	remove(path);
	return 0;
}

static int test_react_loop_max_iterations(void)
{
	const char *path = "build/test_agent_maxiter.toml";
	FILE *f = fopen(path, "w");
	ASSERT(f);
	fprintf(f, "[agent]\nmodel = \"test\"\nmax_tool_iterations = 2\n");
	fclose(f);
	config_t *cfg = NULL;
	char errbuf[256];
	ASSERT(config_load(path, &cfg, errbuf, sizeof(errbuf)) == 0);
	ASSERT(cfg != NULL);
	const agent_tool_t *tools = &mock_echo_tool;
	size_t tool_count = 1;
	char response_buf[4096];
	response_buf[0] = '\0';
	int ret = agent_run(cfg, "cli:maxiter", "hi", &always_tool_provider, tools, tool_count, response_buf, sizeof(response_buf));
	ASSERT(ret == 0);
	ASSERT(strstr(response_buf, "partial") != NULL);
	/* First chat + 2 tool rounds (max_tool_iterations=2) = 3 provider calls. */
	ASSERT(always_tool_call_count == 3);
	config_free(cfg);
	remove(path);
	return 0;
}

static int persist_reply_init(const config_t *cfg) { (void)cfg; return 0; }
static int persist_reply_chat(const provider_message_t *messages, size_t message_count,
	const provider_tool_def_t *tools, size_t tool_count, provider_response_t *response)
{
	(void)messages;
	(void)message_count;
	(void)tools;
	(void)tool_count;
	response->error = 0;
	response->content = strdup("persisted reply");
	response->tool_calls = NULL;
	response->tool_calls_count = 0;
	return 0;
}
static void persist_reply_cleanup(void) {}
static const provider_t persist_reply_provider = {
	.name = "persist_reply",
	.init = persist_reply_init,
	.chat = persist_reply_chat,
	.cleanup = persist_reply_cleanup,
};

static int test_session_persisted_after_exchange(void)
{
	const char *db_path = "build/test_agent_persist.db";
	const char *config_path = "build/test_agent_persist.toml";
	memory_cleanup();
	ASSERT(memory_init(db_path) == 0);
	FILE *cf = fopen(config_path, "w");
	ASSERT(cf);
	fprintf(cf, "[agent]\nmodel = \"test\"\n[memory]\npath = \"%s\"\n", db_path);
	fclose(cf);
	config_t *cfg = NULL;
	char errbuf[256];
	ASSERT(config_load(config_path, &cfg, errbuf, sizeof(errbuf)) == 0);
	ASSERT(cfg != NULL);
	char response_buf[4096];
	response_buf[0] = '\0';
	const char *session_id = "cli:persist";
	const char *user_msg = "user message for persist test";
	int ret = agent_run(cfg, session_id, user_msg, &persist_reply_provider, NULL, 0, response_buf, sizeof(response_buf));
	ASSERT(ret == 0);
	ASSERT(strstr(response_buf, "persisted reply") != NULL);
	char loaded[8192];
	loaded[0] = '\0';
	ASSERT(session_load(session_id, loaded, sizeof(loaded)) == 0);
	ASSERT(strstr(loaded, user_msg) != NULL);
	ASSERT(strstr(loaded, "persisted reply") != NULL);
	config_free(cfg);
	remove(config_path);
	remove(db_path);
	memory_cleanup();
	return 0;
}

static int compaction_call_count;
static int compaction_init(const config_t *cfg) { (void)cfg; compaction_call_count = 0; return 0; }
static int compaction_chat(const provider_message_t *messages, size_t message_count,
	const provider_tool_def_t *tools, size_t tool_count, provider_response_t *response)
{
	(void)tools;
	(void)tool_count;
	response->error = 0;
	response->tool_calls = NULL;
	response->tool_calls_count = 0;
	compaction_call_count++;
	if (compaction_call_count == 1) {
		response->content = strdup("Summary of earlier conversation.");
		return 0;
	}
	response->content = strdup("ok");
	spy_message_count = message_count;
	for (size_t i = 0; i < message_count && i < SPY_SLOTS; i++) {
		spy_roles[i] = messages[i].role;
		if (messages[i].content) {
			size_t n = strlen(messages[i].content);
			if (n >= SPY_CONTENT_SIZE) n = SPY_CONTENT_SIZE - 1;
			memcpy(spy_content[i], messages[i].content, n + 1);
		} else
			spy_content[i][0] = '\0';
	}
	return 0;
}
static void compaction_cleanup(void) {}
static const provider_t compaction_provider = {
	.name = "compaction",
	.init = compaction_init,
	.chat = compaction_chat,
	.cleanup = compaction_cleanup,
};

static int test_context_compaction_when_history_exceeds_max(void)
{
	const char *db_path = "build/test_agent_compact.db";
	const char *config_path = "build/test_agent_compact.toml";
	memory_cleanup();
	ASSERT(memory_init(db_path) == 0);
	char session_json[65536];
	size_t off = 0;
	off += (size_t)snprintf(session_json + off, sizeof(session_json) - off, "[");
	for (int i = 0; i < 50; i++) {
		const char *role = (i % 2 == 0) ? "user" : "assistant";
		off += (size_t)snprintf(session_json + off, sizeof(session_json) - off,
			"%s{\"role\":\"%s\",\"content\":\"msg_%d\"}", i ? "," : "", role, i);
	}
	off += (size_t)snprintf(session_json + off, sizeof(session_json) - off, "]");
	ASSERT(session_save("cli:compact", session_json) == 0);
	FILE *cf = fopen(config_path, "w");
	ASSERT(cf);
	fprintf(cf, "[agent]\nmodel = \"test\"\nmax_context_messages = 40\n[memory]\npath = \"%s\"\n", db_path);
	fclose(cf);
	config_t *cfg = NULL;
	char errbuf[256];
	ASSERT(config_load(config_path, &cfg, errbuf, sizeof(errbuf)) == 0);
	ASSERT(cfg != NULL);
	char response_buf[4096];
	response_buf[0] = '\0';
	int ret = agent_run(cfg, "cli:compact", "hi", &compaction_provider, NULL, 0, response_buf, sizeof(response_buf));
	ASSERT(ret == 0);
	ASSERT(compaction_call_count >= 2);
	ASSERT(strstr(response_buf, "ok") != NULL);
	/* Second chat context must contain the summary and tail (e.g. msg_11 from kept raw messages). */
	int found_summary = 0;
	int found_tail = 0;
	for (int i = 0; i < SPY_SLOTS && (i < (int)spy_message_count); i++) {
		if (strstr(spy_content[i], "Summary of earlier") != NULL) found_summary = 1;
		if (strstr(spy_content[i], "msg_11") != NULL) found_tail = 1;
	}
	ASSERT(found_summary);
	ASSERT(found_tail);
	config_free(cfg);
	remove(config_path);
	remove(db_path);
	memory_cleanup();
	return 0;
}

int main(void)
{
	RUN(test_agent_run_with_stub_and_no_tools());
	RUN(test_agent_run_invalid_args_returns_error());
	RUN(test_context_assembly_system_prompt_history_memories());
	RUN(test_react_loop_tool_then_text());
	RUN(test_react_loop_max_iterations());
	RUN(test_session_persisted_after_exchange());
	RUN(test_context_compaction_when_history_exceeds_max());
	printf("test_agent: all tests passed\n");
	return 0;
}
