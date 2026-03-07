/**
 * @file anthropic.c
 * @brief Anthropic provider: Claude Messages API, tool_use blocks.
 *
 * API key from environment via config; never logged.
 */
#define _POSIX_C_SOURCE 200809L

#include "core/config.h"
#include "providers/provider.h"
#include "cJSON.h"
#include <curl/curl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ANTHROPIC_URL "https://api.anthropic.com/v1/messages"
#define ANTHROPIC_VERSION "2023-06-01"
#define REQUEST_TIMEOUT_SEC 120
#define CONNECT_TIMEOUT_SEC 30
#define RESPONSE_BUF_INIT 65536

typedef struct {
	char *buf;
	size_t len;
	size_t cap;
} curl_buf_t;

static char *g_api_key;
static const config_t *g_cfg;

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	curl_buf_t *b = (curl_buf_t *)userdata;
	if (!b || !b->buf) return 0;
	if (nmemb != 0 && size > SIZE_MAX / nmemb) return 0;
	size_t n = size * nmemb;
	size_t need = b->len + n + 1;
	if (need > RESPONSE_BUF_INIT * 4) return 0;
	if (need > b->cap) {
		size_t new_cap = b->cap ? b->cap * 2 : RESPONSE_BUF_INIT;
		while (new_cap < need && new_cap <= RESPONSE_BUF_INIT * 4) new_cap *= 2;
		if (need > new_cap) return 0;
		char *p = realloc(b->buf, new_cap);
		if (!p) return 0;
		b->buf = p;
		b->cap = new_cap;
	}
	memcpy(b->buf + b->len, ptr, n);
	b->len += n;
	b->buf[b->len] = '\0';
	return n;
}

static void set_error(provider_response_t *response, const char *msg)
{
	response->error = 1;
	response->content = msg ? strdup(msg) : NULL;
}

static char *dup_str(const char *s)
{
	if (!s) return NULL;
	size_t n = strlen(s) + 1;
	char *p = malloc(n);
	if (p) memcpy(p, s, n);
	return p;
}

static int parse_response_body(const char *response_buf, provider_response_t *response)
{
	cJSON *root = cJSON_Parse(response_buf);
	if (!root) {
		set_error(response, "Failed to parse Anthropic response JSON");
		return -1;
	}
	cJSON *err_obj = cJSON_GetObjectItem(root, "error");
	if (cJSON_IsObject(err_obj)) {
		cJSON *msg = cJSON_GetObjectItem(err_obj, "message");
		const char *errmsg = cJSON_IsString(msg) ? msg->valuestring : "Anthropic API error";
		set_error(response, errmsg);
		cJSON_Delete(root);
		return -1;
	}
	cJSON *content = cJSON_GetObjectItem(root, "content");
	if (!cJSON_IsArray(content)) {
		cJSON_Delete(root);
		return 0;
	}
	size_t text_len = 0;
	size_t text_cap = 256;
	char *text = malloc(text_cap);
	if (text) text[0] = '\0';
	size_t tool_cap = 4;
	size_t tool_count = 0;
	provider_tool_call_t *tool_calls = malloc(tool_cap * sizeof(provider_tool_call_t));
	if (!tool_calls) tool_cap = 0;
	cJSON *block;
	cJSON_ArrayForEach(block, content) {
		cJSON *type_item = cJSON_GetObjectItem(block, "type");
		const char *type = cJSON_IsString(type_item) ? type_item->valuestring : NULL;
		if (type && strcmp(type, "text") == 0) {
			cJSON *text_item = cJSON_GetObjectItem(block, "text");
			const char *t = cJSON_IsString(text_item) ? text_item->valuestring : "";
			if (t) {
				size_t tlen = strlen(t);
				while (text_len + tlen + 1 >= text_cap) {
					text_cap *= 2;
					char *n = realloc(text, text_cap);
					if (!n) break;
					text = n;
				}
				if (text && text_len + tlen + 1 < text_cap) {
					memcpy(text + text_len, t, tlen + 1);
					text_len += tlen;
				}
			}
		} else if (type && strcmp(type, "tool_use") == 0) {
			if (tool_count >= tool_cap) {
				tool_cap *= 2;
				provider_tool_call_t *n = realloc(tool_calls, tool_cap * sizeof(provider_tool_call_t));
				if (!n) continue;
				tool_calls = n;
			}
			provider_tool_call_t *tc = &tool_calls[tool_count];
			tc->id = NULL;
			tc->name = NULL;
			tc->arguments = NULL;
			cJSON *id_item = cJSON_GetObjectItem(block, "id");
			cJSON *name_item = cJSON_GetObjectItem(block, "name");
			cJSON *input_item = cJSON_GetObjectItem(block, "input");
			if (cJSON_IsString(id_item)) tc->id = dup_str(id_item->valuestring);
			if (cJSON_IsString(name_item)) tc->name = dup_str(name_item->valuestring);
			if (input_item) {
				char *printed = cJSON_PrintUnformatted(input_item);
				tc->arguments = printed;
			}
			tool_count++;
		}
	}
	cJSON_Delete(root);
	response->content = text;
	response->tool_calls = tool_calls;
	response->tool_calls_count = tool_count;
	return 0;
}

static int do_request(const char *body, provider_response_t *response)
{
	CURL *curl = curl_easy_init();
	if (!curl) {
		set_error(response, "Failed to initialize curl");
		return -1;
	}
	size_t key_len = g_api_key ? strlen(g_api_key) : 0;
	size_t key_header_size = key_len + 16;
	char *key_header = malloc(key_header_size);
	if (!key_header) {
		curl_easy_cleanup(curl);
		set_error(response, "Out of memory");
		return -1;
	}
	snprintf(key_header, key_header_size, "x-api-key: %s", g_api_key ? g_api_key : "");
	curl_buf_t resp_buf = { .buf = malloc(RESPONSE_BUF_INIT), .len = 0, .cap = RESPONSE_BUF_INIT };
	if (!resp_buf.buf) {
		free(key_header);
		curl_easy_cleanup(curl);
		set_error(response, "Out of memory");
		return -1;
	}
	resp_buf.buf[0] = '\0';
	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, "anthropic-version: " ANTHROPIC_VERSION);
	headers = curl_slist_append(headers, key_header);
	curl_easy_setopt(curl, CURLOPT_URL, ANTHROPIC_URL);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_buf);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)REQUEST_TIMEOUT_SEC);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)CONNECT_TIMEOUT_SEC);
	CURLcode res = curl_easy_perform(curl);
	long code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	curl_slist_free_all(headers);
	free(key_header);
	curl_easy_cleanup(curl);
	if (res != CURLE_OK) {
		free(resp_buf.buf);
		set_error(response, curl_easy_strerror(res));
		return -1;
	}
	if (code < 200 || code >= 300) {
		char errmsg[160];
		snprintf(errmsg, sizeof(errmsg), "Anthropic API HTTP %ld", code);
		free(resp_buf.buf);
		set_error(response, errmsg);
		return -1;
	}
	int ret = parse_response_body(resp_buf.buf, response);
	free(resp_buf.buf);
	return ret;
}

static int build_and_send(const provider_message_t *messages, size_t message_count,
                          const provider_tool_def_t *tools, size_t tool_count,
                          provider_response_t *response)
{
	if (!g_cfg) { set_error(response, "Anthropic provider not initialized"); return -1; }
	cJSON *root = cJSON_CreateObject();
	if (!root) { set_error(response, "Out of memory"); return -1; }
	const char *model = config_agent_model(g_cfg);
	int max_tokens = config_agent_max_tokens(g_cfg);
	if (!model) model = "claude-3-5-sonnet-20241022";
	if (max_tokens <= 0) max_tokens = 4096;
	cJSON_AddItemToObject(root, "model", cJSON_CreateString(model));
	cJSON_AddItemToObject(root, "max_tokens", cJSON_CreateNumber(max_tokens));
	for (size_t i = 0; i < message_count; i++) {
		if (messages[i].role && strcmp(messages[i].role, "system") == 0) {
			cJSON_AddItemToObject(root, "system", cJSON_CreateString(messages[i].content ? messages[i].content : ""));
			break;
		}
	}
	cJSON *msg_arr = cJSON_CreateArray();
	if (!msg_arr) { cJSON_Delete(root); set_error(response, "Out of memory"); return -1; }
	for (size_t i = 0; i < message_count; i++) {
		const char *role = messages[i].role ? messages[i].role : "user";
		if (strcmp(role, "system") == 0) continue;
		cJSON *msg = cJSON_CreateObject();
		if (!msg) break;
		cJSON_AddItemToObject(msg, "role", cJSON_CreateString(role));
		if (messages[i].tool_use_id) {
			cJSON *content_arr = cJSON_CreateArray();
			if (content_arr) {
				cJSON *tr = cJSON_CreateObject();
				if (tr) {
					cJSON_AddItemToObject(tr, "type", cJSON_CreateString("tool_result"));
					cJSON_AddItemToObject(tr, "tool_use_id", cJSON_CreateString(messages[i].tool_use_id));
					cJSON_AddItemToObject(tr, "content", cJSON_CreateString(messages[i].content ? messages[i].content : ""));
					cJSON_AddItemToArray(content_arr, tr);
				}
				cJSON_AddItemToObject(msg, "content", content_arr);
			} else
				cJSON_AddItemToObject(msg, "content", cJSON_CreateString(""));
		} else if (messages[i].tool_calls && messages[i].tool_calls_count > 0 && strcmp(role, "assistant") == 0) {
			cJSON *content_arr = cJSON_CreateArray();
			if (content_arr) {
				if (messages[i].content && messages[i].content[0] != '\0') {
					cJSON *text_block = cJSON_CreateObject();
					if (text_block) {
						cJSON_AddItemToObject(text_block, "type", cJSON_CreateString("text"));
						cJSON_AddItemToObject(text_block, "text", cJSON_CreateString(messages[i].content));
						cJSON_AddItemToArray(content_arr, text_block);
					}
				}
				for (size_t k = 0; k < messages[i].tool_calls_count; k++) {
					const provider_tool_call_t *tc = &messages[i].tool_calls[k];
					cJSON *tu = cJSON_CreateObject();
					if (!tu) break;
					cJSON_AddItemToObject(tu, "type", cJSON_CreateString("tool_use"));
					cJSON_AddItemToObject(tu, "id", cJSON_CreateString(tc->id ? tc->id : ""));
					cJSON_AddItemToObject(tu, "name", cJSON_CreateString(tc->name ? tc->name : ""));
					cJSON *input = tc->arguments && tc->arguments[0] ? cJSON_Parse(tc->arguments) : cJSON_CreateObject();
					if (input) cJSON_AddItemToObject(tu, "input", input);
					cJSON_AddItemToArray(content_arr, tu);
				}
				cJSON_AddItemToObject(msg, "content", content_arr);
			} else
				cJSON_AddItemToObject(msg, "content", cJSON_CreateString(messages[i].content ? messages[i].content : ""));
		} else
			cJSON_AddItemToObject(msg, "content", cJSON_CreateString(messages[i].content ? messages[i].content : ""));
		cJSON_AddItemToArray(msg_arr, msg);
	}
	cJSON_AddItemToObject(root, "messages", msg_arr);
	if (tool_count > 0 && tools) {
		cJSON *tools_arr = cJSON_CreateArray();
		if (tools_arr) {
			for (size_t i = 0; i < tool_count; i++) {
				cJSON *t = cJSON_CreateObject();
				if (!t) break;
				cJSON_AddItemToObject(t, "name", cJSON_CreateString(tools[i].name ? tools[i].name : ""));
				cJSON_AddItemToObject(t, "description", cJSON_CreateString(tools[i].description ? tools[i].description : ""));
				if (tools[i].parameters_json && tools[i].parameters_json[0]) {
					cJSON *schema = cJSON_Parse(tools[i].parameters_json);
					if (schema) {
						cJSON_AddItemToObject(t, "input_schema", schema);
					}
				}
				cJSON_AddItemToArray(tools_arr, t);
			}
			cJSON_AddItemToObject(root, "tools", tools_arr);
		}
	}
	char *body = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (!body) { set_error(response, "Out of memory"); return -1; }
	int ret = do_request(body, response);
	cJSON_free(body);
	return ret;
}

static int anthropic_init(const config_t *cfg)
{
	if (!cfg) return -1;
	const char *env_name = config_provider_anthropic_api_key_env(cfg);
	if (!env_name || !env_name[0]) return -1;
	const char *key = getenv(env_name);
	if (!key || !key[0]) return -1;
	free(g_api_key);
	g_api_key = strdup(key);
	g_cfg = g_api_key ? cfg : NULL;
	return g_api_key ? 0 : -1;
}

static int anthropic_chat(const provider_message_t *messages, size_t message_count,
                          const provider_tool_def_t *tools, size_t tool_count,
                          provider_response_t *response)
{
	if (!g_api_key || !g_cfg) {
		set_error(response, "Anthropic provider not initialized or API key missing");
		return -1;
	}
	provider_response_clear(response);
	return build_and_send(messages, message_count, tools, tool_count, response);
}

static void anthropic_cleanup(void)
{
	if (g_api_key) {
		volatile char *p = (volatile char *)g_api_key;
		for (size_t i = 0; g_api_key[i] != '\0'; i++) p[i] = '\0';
		free(g_api_key);
		g_api_key = NULL;
	}
	g_cfg = NULL;
}

static const provider_t anthropic_provider = {
	.name = "anthropic",
	.init = anthropic_init,
	.chat = anthropic_chat,
	.cleanup = anthropic_cleanup,
};

const provider_t *provider_anthropic_get(void)
{
	return &anthropic_provider;
}

#ifdef SHELLCLAW_TEST
int anthropic_parse_response_for_test(const char *json, provider_response_t *response)
{
	if (!json || !response) return -1;
	return parse_response_body(json, response);
}
#endif
