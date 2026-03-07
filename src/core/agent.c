/**
 * @file agent.c
 * @brief ReAct agent loop implementation.
 */
#define _POSIX_C_SOURCE 200809L

#include "core/agent.h"
#include "core/config.h"
#include "core/memory.h"
#include "core/skill.h"
#include "providers/provider.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SYSTEM_PROMPT_MAX      65536
#define SKILLS_BUF_SIZE        32768
#define SESSION_JSON_MAX       (128 * 1024)
#define RECALL_BUF_SIZE        8192
#define RECALL_LIMIT           5
#define HISTORY_CONTENT_MAX    (128 * 1024)
#define MAX_HISTORY_MESSAGES   40
#define ROLE_LEN               16
#define MAX_TOOL_CALLS         16
#define TOOL_RESULT_SIZE       4096
#define SUMMARY_SOURCE_MAX     (64 * 1024)
#define SUMMARY_RESULT_MAX     4096

static const char SUMMARIZE_SYSTEM[] = "Summarize the following conversation in one short paragraph. Output only the summary, no preamble.";

/** Summarize oldest messages when over max_ctx; replace with one summary + trailing. */
static int compact_session_via_llm(const char *session_id, char *session_buf, size_t session_buf_size,
	int msg_count, int max_ctx, const provider_t *provider)
{
	if (msg_count <= max_ctx || max_ctx < 2) return 0;
	cJSON *root = cJSON_Parse(session_buf);
	if (!root || !cJSON_IsArray(root)) {
		if (root) cJSON_Delete(root);
		return -1;
	}
	int to_summarize_n = msg_count - max_ctx;
	if (to_summarize_n <= 0) { cJSON_Delete(root); return 0; }
	char *source_buf = malloc(SUMMARY_SOURCE_MAX);
	if (!source_buf) { cJSON_Delete(root); return -1; }
	source_buf[0] = '\0';
	size_t used = 0;
	for (int i = 0; i < to_summarize_n && used < SUMMARY_SOURCE_MAX - 1; i++) {
		cJSON *item = cJSON_GetArrayItem(root, i);
		if (!item || !cJSON_IsObject(item)) continue;
		const char *role = "user";
		const char *content = "";
		cJSON *r = cJSON_GetObjectItem(item, "role");
		cJSON *c = cJSON_GetObjectItem(item, "content");
		if (cJSON_IsString(r)) role = r->valuestring;
		if (cJSON_IsString(c)) content = c->valuestring;
		int n = snprintf(source_buf + used, SUMMARY_SOURCE_MAX - used, "%s: %s\n", role, content ? content : "");
		if (n > 0 && (size_t)n < SUMMARY_SOURCE_MAX - used) used += (size_t)n;
	}
	provider_message_t summ_msgs[2] = {
		{ .role = "system", .content = SUMMARIZE_SYSTEM, .tool_calls = NULL, .tool_calls_count = 0, .tool_use_id = NULL },
		{ .role = "user", .content = source_buf, .tool_calls = NULL, .tool_calls_count = 0, .tool_use_id = NULL },
	};
	provider_response_t resp = {0};
	int err = provider->chat(summ_msgs, 2, NULL, 0, &resp);
	free(source_buf);
	if (err != 0 || !resp.content) {
		provider_response_clear(&resp);
		cJSON_Delete(root);
		return -1;
	}
	char summary_buf[SUMMARY_RESULT_MAX];
	size_t sum_len = strlen(resp.content);
	if (sum_len >= SUMMARY_RESULT_MAX) sum_len = SUMMARY_RESULT_MAX - 1;
	memcpy(summary_buf, resp.content, sum_len + 1);
	summary_buf[sum_len] = '\0';
	provider_response_clear(&resp);
	cJSON *new_arr = cJSON_CreateArray();
	if (!new_arr) { cJSON_Delete(root); return -1; }
	cJSON *summ_obj = cJSON_CreateObject();
	if (summ_obj) {
		cJSON_AddItemToObject(summ_obj, "role", cJSON_CreateString("user"));
		cJSON_AddItemToObject(summ_obj, "content", cJSON_CreateString(summary_buf));
		cJSON_AddItemToArray(new_arr, summ_obj);
	}
	int keep = max_ctx - 1;
	int start = msg_count - keep;
	if (start < 0) start = 0;
	for (int i = start; i < msg_count; i++) {
		cJSON *item = cJSON_GetArrayItem(root, i);
		if (item) {
			cJSON *dup = cJSON_Duplicate(item, 1);
			if (dup) cJSON_AddItemToArray(new_arr, dup);
		}
	}
	cJSON_Delete(root);
	char *printed = cJSON_PrintUnformatted(new_arr);
	cJSON_Delete(new_arr);
	if (!printed) return -1;
	size_t plen = strlen(printed);
	if (plen >= session_buf_size) plen = session_buf_size - 1;
	memcpy(session_buf, printed, plen + 1);
	session_buf[plen] = '\0';
	cJSON_free(printed);
	session_save(session_id, session_buf);
	return 0;
}

/** Fallback when SOUL+IDENTITY+skills are all empty. */
static const char SYSTEM_PROMPT_FALLBACK[] = "You are a helpful assistant.";

static void copy_response_to_buf(const char *content, char *response_buf, size_t response_size)
{
	if (!response_buf || response_size == 0) return;
	if (content) {
		size_t n = strlen(content);
		if (n >= response_size) n = response_size - 1;
		memcpy(response_buf, content, n);
		response_buf[n] = '\0';
	} else
		response_buf[0] = '\0';
}

static size_t append_memories_to_system(char *system_buf, size_t buf_size, const char *recall_buf)
{
	size_t len = strlen(system_buf);
	if (len == 0 || !recall_buf || recall_buf[0] == '\0') return len;
	const char *prefix = "\n\nRelevant memories:\n\n";
	size_t prefix_len = strlen(prefix);
	size_t recall_len = strlen(recall_buf);
	if (len + prefix_len + recall_len + 1 > buf_size)
		recall_len = buf_size > len + prefix_len ? (buf_size - len - prefix_len - 1) : 0;
	if (prefix_len + recall_len == 0) return len;
	memcpy(system_buf + len, prefix, prefix_len + 1);
	len += prefix_len;
	memcpy(system_buf + len, recall_buf, recall_len + 1);
	len += recall_len;
	return len;
}

/** Parse session JSON into message slots; copy roles and content into given buffers. */
static int parse_session_into_messages(const char *session_json, int max_messages,
	provider_message_t *out_msgs, char *role_buf, size_t role_buf_size,
	char *content_buf, size_t content_buf_size, int *out_count)
{
	*out_count = 0;
	if (!session_json || session_json[0] != '[') return 0;
	cJSON *root = cJSON_Parse(session_json);
	if (!root || !cJSON_IsArray(root)) {
		if (root) cJSON_Delete(root);
		return 0;
	}
	int arr_len = cJSON_GetArraySize(root);
	int skip = arr_len > max_messages ? arr_len - max_messages : 0;
	size_t content_used = 0;
	for (int i = skip; i < arr_len && *out_count < max_messages; i++) {
		cJSON *item = cJSON_GetArrayItem(root, i);
		if (!item || !cJSON_IsObject(item)) continue;
		cJSON *role_item = cJSON_GetObjectItem(item, "role");
		cJSON *content_item = cJSON_GetObjectItem(item, "content");
		const char *role_s = role_item && cJSON_IsString(role_item) ? role_item->valuestring : "user";
		const char *content_s = content_item && cJSON_IsString(content_item) ? content_item->valuestring : "";
		size_t rlen = strlen(role_s);
		size_t clen = strlen(content_s);
		size_t role_off = (size_t)(*out_count) * ROLE_LEN;
		if (rlen >= ROLE_LEN || role_off + ROLE_LEN > role_buf_size) break;
		if (content_used + clen + 1 > content_buf_size) break;
		memcpy(role_buf + role_off, role_s, rlen + 1);
		memcpy(content_buf + content_used, content_s, clen + 1);
		out_msgs[*out_count].role = role_buf + role_off;
		out_msgs[*out_count].content = content_buf + content_used;
		content_used += clen + 1;
		(*out_count)++;
	}
	cJSON_Delete(root);
	return 0;
}

static const agent_tool_t *find_tool(const agent_tool_t *tools, size_t tool_count, const char *name)
{
	if (!tools || !name) return NULL;
	for (size_t i = 0; i < tool_count; i++) {
		if (tools[i].name && strcmp(tools[i].name, name) == 0)
			return &tools[i];
	}
	return NULL;
}

static void free_tool_calls_copy(provider_tool_call_t *copy, size_t n)
{
	if (!copy) return;
	for (size_t i = 0; i < n; i++) {
		free(copy[i].id);
		free(copy[i].name);
		free(copy[i].arguments);
	}
	free(copy);
}

/** Append user+assistant exchange to session JSON. Invalid or empty existing becomes []. */
static int append_exchange_to_session_json(const char *existing_json, const char *user_message,
	const char *assistant_content, char *out_buf, size_t out_size)
{
	cJSON *arr = NULL;
	if (existing_json && existing_json[0] == '[') {
		arr = cJSON_Parse(existing_json);
		if (arr && !cJSON_IsArray(arr)) {
			cJSON_Delete(arr);
			arr = NULL;
		}
	}
	if (!arr) arr = cJSON_CreateArray();
	if (!arr) return -1;
	cJSON *user_obj = cJSON_CreateObject();
	if (user_obj) {
		cJSON_AddItemToObject(user_obj, "role", cJSON_CreateString("user"));
		cJSON_AddItemToObject(user_obj, "content", cJSON_CreateString(user_message ? user_message : ""));
		cJSON_AddItemToArray(arr, user_obj);
	}
	cJSON *asst_obj = cJSON_CreateObject();
	if (asst_obj) {
		cJSON_AddItemToObject(asst_obj, "role", cJSON_CreateString("assistant"));
		cJSON_AddItemToObject(asst_obj, "content", cJSON_CreateString(assistant_content ? assistant_content : ""));
		cJSON_AddItemToArray(arr, asst_obj);
	}
	char *printed = cJSON_PrintUnformatted(arr);
	cJSON_Delete(arr);
	if (!printed) return -1;
	size_t len = strlen(printed);
	if (len >= out_size) len = out_size - 1;
	memcpy(out_buf, printed, len + 1);
	out_buf[len] = '\0';
	cJSON_free(printed);
	return 0;
}

static provider_tool_call_t *copy_tool_calls(const provider_tool_call_t *src, size_t n)
{
	if (!src || n == 0) return NULL;
	provider_tool_call_t *dst = malloc(n * sizeof(provider_tool_call_t));
	if (!dst) return NULL;
	for (size_t i = 0; i < n; i++) {
		dst[i].id = src[i].id ? strdup(src[i].id) : NULL;
		dst[i].name = src[i].name ? strdup(src[i].name) : NULL;
		dst[i].arguments = src[i].arguments ? strdup(src[i].arguments) : NULL;
		if ((src[i].id && !dst[i].id) || (src[i].name && !dst[i].name) || (src[i].arguments && !dst[i].arguments)) {
			free_tool_calls_copy(dst, i + 1);
			return NULL;
		}
	}
	return dst;
}

int agent_run(const config_t *cfg, const char *session_id, const char *user_message,
              const provider_t *provider, const agent_tool_t *tools, size_t tool_count,
              char *response_buf, size_t response_size)
{
	int ret = -1;
	provider_tool_def_t *tool_defs = NULL;
	char *system_buf = NULL;
	char *skills_buf = NULL;
	char *session_buf = NULL;
	char *recall_buf = NULL;
	char *history_content = NULL;
	provider_message_t *history_msgs = NULL;
	char *history_roles_buf = NULL;
	char *tool_result_bufs = NULL;
	provider_message_t *messages = NULL;
	char *prev_assistant = NULL;
	provider_tool_call_t *prev_calls = NULL;
	size_t prev_n = 0;
	if (!cfg || !session_id || !user_message || !provider || !response_buf || response_size == 0) {
		if (response_buf && response_size > 0)
			strncpy(response_buf, "agent_run: invalid arguments", response_size - 1);
		if (response_buf && response_size > 0) response_buf[response_size - 1] = '\0';
		return -1;
	}
	if (tool_count > 0 && tools) {
		tool_defs = malloc(tool_count * sizeof(provider_tool_def_t));
		if (!tool_defs) {
			if (response_buf && response_size > 0) {
				strncpy(response_buf, "agent_run: out of memory", response_size - 1);
				response_buf[response_size - 1] = '\0';
			}
			return -1;
		}
		for (size_t i = 0; i < tool_count; i++) {
			tool_defs[i].name = tools[i].name;
			tool_defs[i].description = tools[i].description;
			tool_defs[i].parameters_json = tools[i].parameters_json;
		}
	}
	system_buf = malloc(SYSTEM_PROMPT_MAX);
	skills_buf = malloc(SKILLS_BUF_SIZE);
	session_buf = malloc(SESSION_JSON_MAX);
	recall_buf = malloc(RECALL_BUF_SIZE);
	history_content = malloc(HISTORY_CONTENT_MAX);
	if (!system_buf || !skills_buf || !session_buf || !recall_buf || !history_content) {
		if (response_buf && response_size > 0) {
			strncpy(response_buf, "agent_run: out of memory", response_size - 1);
			response_buf[response_size - 1] = '\0';
		}
		goto cleanup;
	}
	history_msgs = malloc(MAX_HISTORY_MESSAGES * sizeof(provider_message_t));
	history_roles_buf = malloc(MAX_HISTORY_MESSAGES * ROLE_LEN);
	tool_result_bufs = malloc((size_t)MAX_TOOL_CALLS * TOOL_RESULT_SIZE);
	if (!history_msgs || !history_roles_buf || !tool_result_bufs) {
		if (response_buf && response_size > 0) {
			strncpy(response_buf, "agent_run: out of memory", response_size - 1);
			response_buf[response_size - 1] = '\0';
		}
		goto cleanup;
	}
	skill_load_all(cfg, skills_buf, SKILLS_BUF_SIZE);
	if (skill_build_system_prompt_base(cfg, skills_buf, system_buf, SYSTEM_PROMPT_MAX) != 0) {
		strncpy(system_buf, SYSTEM_PROMPT_FALLBACK, SYSTEM_PROMPT_MAX - 1);
		system_buf[SYSTEM_PROMPT_MAX - 1] = '\0';
	}
	recall_buf[0] = '\0';
	memory_recall(user_message, recall_buf, RECALL_BUF_SIZE, RECALL_LIMIT);
	append_memories_to_system(system_buf, SYSTEM_PROMPT_MAX, recall_buf);
	int max_ctx = config_agent_max_context_messages(cfg);
	if (max_ctx <= 0 || max_ctx > MAX_HISTORY_MESSAGES) max_ctx = MAX_HISTORY_MESSAGES;
	session_buf[0] = '\0';
	session_load(session_id, session_buf, SESSION_JSON_MAX);
	{
		cJSON *parsed = cJSON_Parse(session_buf);
		int msg_count = (parsed && cJSON_IsArray(parsed)) ? cJSON_GetArraySize(parsed) : 0;
		if (parsed) cJSON_Delete(parsed);
		if (msg_count > max_ctx)
			compact_session_via_llm(session_id, session_buf, SESSION_JSON_MAX, msg_count, max_ctx, provider);
	}
	int history_count = 0;
	parse_session_into_messages(session_buf, max_ctx, history_msgs,
		history_roles_buf, MAX_HISTORY_MESSAGES * ROLE_LEN,
		history_content, HISTORY_CONTENT_MAX, &history_count);
	size_t total_msgs = 1 + (size_t)history_count + 1;
	messages = malloc(total_msgs * sizeof(provider_message_t));
	if (!messages) {
		if (response_buf && response_size > 0) {
			strncpy(response_buf, "agent_run: out of memory", response_size - 1);
			response_buf[response_size - 1] = '\0';
		}
		goto cleanup;
	}
	memset(messages, 0, total_msgs * sizeof(provider_message_t));
	messages[0].role = "system";
	messages[0].content = system_buf;
	for (int i = 0; i < history_count; i++) {
		messages[1 + i].role = history_msgs[i].role;
		messages[1 + i].content = history_msgs[i].content;
	}
	messages[1 + history_count].role = "user";
	messages[1 + history_count].content = user_message;
	int max_iter = config_agent_max_tool_iterations(cfg);
	if (max_iter <= 0) max_iter = 20;
	int iteration = 0;
	provider_response_t response = {0};
	for (;;) {
		int err = provider->chat(messages, total_msgs, tool_defs, tool_count, &response);
		if (err != 0) {
			copy_response_to_buf(response.content, response_buf, response_size);
			provider_response_clear(&response);
			goto cleanup;
		}
		if (response.tool_calls_count == 0 || iteration >= max_iter) {
			copy_response_to_buf(response.content, response_buf, response_size);
			provider_response_clear(&response);
			{
				char *updated = malloc(SESSION_JSON_MAX);
				if (updated) {
					if (append_exchange_to_session_json(session_buf, user_message, response_buf,
						updated, SESSION_JSON_MAX) == 0)
						session_save(session_id, updated);
					free(updated);
				}
			}
			ret = 0;
			goto cleanup;
		}
		free(prev_assistant);
		free_tool_calls_copy(prev_calls, prev_n);
		prev_assistant = NULL;
		prev_calls = NULL;
		prev_n = 0;
		size_t nc = response.tool_calls_count;
		if (nc > MAX_TOOL_CALLS) nc = MAX_TOOL_CALLS;
		char *assistant_content = response.content ? strdup(response.content) : NULL;
		if (!assistant_content && response.content && response.content[0] != '\0') {
			provider_response_clear(&response);
			if (response_buf && response_size > 0) {
				strncpy(response_buf, "agent_run: out of memory", response_size - 1);
				response_buf[response_size - 1] = '\0';
			}
			goto cleanup;
		}
		if (!assistant_content) assistant_content = strdup("");
		provider_tool_call_t *our_calls = copy_tool_calls(response.tool_calls, nc);
		provider_response_clear(&response);
		if (!our_calls) {
			free(assistant_content);
			if (response_buf && response_size > 0) {
				strncpy(response_buf, "agent_run: out of memory", response_size - 1);
				response_buf[response_size - 1] = '\0';
			}
			goto cleanup;
		}
		for (size_t k = 0; k < nc; k++) {
			char *one_buf = tool_result_bufs + k * TOOL_RESULT_SIZE;
			one_buf[0] = '\0';
			const agent_tool_t *tool = find_tool(tools, tool_count, our_calls[k].name);
			if (tool && tool->execute)
				tool->execute(our_calls[k].arguments ? our_calls[k].arguments : "{}",
					one_buf, TOOL_RESULT_SIZE);
			else
				snprintf(one_buf, TOOL_RESULT_SIZE, "error: unknown tool \"%s\"",
					our_calls[k].name ? our_calls[k].name : "");
		}
		size_t new_count = total_msgs + 1 + nc;
		provider_message_t *new_messages = malloc(new_count * sizeof(provider_message_t));
		if (!new_messages) {
			free_tool_calls_copy(our_calls, nc);
			free(assistant_content);
			if (response_buf && response_size > 0) {
				strncpy(response_buf, "agent_run: out of memory", response_size - 1);
				response_buf[response_size - 1] = '\0';
			}
			goto cleanup;
		}
		memset(new_messages, 0, new_count * sizeof(provider_message_t));
		memcpy(new_messages, messages, total_msgs * sizeof(provider_message_t));
		new_messages[total_msgs].role = "assistant";
		new_messages[total_msgs].content = assistant_content;
		new_messages[total_msgs].tool_calls = our_calls;
		new_messages[total_msgs].tool_calls_count = nc;
		for (size_t k = 0; k < nc; k++) {
			new_messages[total_msgs + 1 + k].role = "user";
			new_messages[total_msgs + 1 + k].content = tool_result_bufs + k * TOOL_RESULT_SIZE;
			new_messages[total_msgs + 1 + k].tool_use_id = our_calls[k].id;
		}
		free(messages);
		messages = new_messages;
		total_msgs = new_count;
		iteration++;
		prev_assistant = assistant_content;
		prev_calls = our_calls;
		prev_n = nc;
	}
cleanup:
	free(system_buf);
	free(skills_buf);
	free(session_buf);
	free(recall_buf);
	free(history_content);
	free(tool_defs);
	free(history_msgs);
	free(history_roles_buf);
	free(tool_result_bufs);
	free(prev_assistant);
	free_tool_calls_copy(prev_calls, prev_n);
	free(messages);
	return ret;
}
