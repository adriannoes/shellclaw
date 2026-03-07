/**
 * @file openai.c
 * @brief OpenAI provider: Chat Completions API, function/tool_calls.
 *
 * API key and endpoint from config; key from environment only. Never logged.
 */

#include "core/config.h"
#include "providers/provider.h"
#include "cJSON.h"
#include <curl/curl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
		set_error(response, "Failed to parse OpenAI response JSON");
		return -1;
	}
	cJSON *err_obj = cJSON_GetObjectItem(root, "error");
	if (cJSON_IsObject(err_obj)) {
		cJSON *msg = cJSON_GetObjectItem(err_obj, "message");
		const char *errmsg = cJSON_IsString(msg) ? msg->valuestring : "OpenAI API error";
		set_error(response, errmsg);
		cJSON_Delete(root);
		return -1;
	}
	cJSON *choices = cJSON_GetObjectItem(root, "choices");
	if (!cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
		cJSON_Delete(root);
		return 0;
	}
	cJSON *choice = cJSON_GetArrayItem(choices, 0);
	cJSON *msg_obj = cJSON_GetObjectItem(choice, "message");
	if (!cJSON_IsObject(msg_obj)) {
		cJSON_Delete(root);
		return 0;
	}
	response->content = NULL;
	response->tool_calls = NULL;
	response->tool_calls_count = 0;
	cJSON *content_item = cJSON_GetObjectItem(msg_obj, "content");
	if (cJSON_IsString(content_item) && content_item->valuestring)
		response->content = dup_str(content_item->valuestring);
	else
		response->content = malloc(1);
	if (response->content && !cJSON_IsString(content_item))
		response->content[0] = '\0';
	cJSON *tool_calls_arr = cJSON_GetObjectItem(msg_obj, "tool_calls");
	if (cJSON_IsArray(tool_calls_arr)) {
		int n = cJSON_GetArraySize(tool_calls_arr);
		if (n > 0) {
			provider_tool_call_t *calls = malloc((size_t)n * sizeof(provider_tool_call_t));
			if (calls) {
				for (int i = 0; i < n; i++) {
					cJSON *tc = cJSON_GetArrayItem(tool_calls_arr, i);
					calls[i].id = NULL;
					calls[i].name = NULL;
					calls[i].arguments = NULL;
					cJSON *id_item = cJSON_GetObjectItem(tc, "id");
					cJSON *fn = cJSON_GetObjectItem(tc, "function");
					if (cJSON_IsString(id_item)) calls[i].id = dup_str(id_item->valuestring);
					if (cJSON_IsObject(fn)) {
						cJSON *name_item = cJSON_GetObjectItem(fn, "name");
						cJSON *args_item = cJSON_GetObjectItem(fn, "arguments");
						if (cJSON_IsString(name_item)) calls[i].name = dup_str(name_item->valuestring);
						if (cJSON_IsString(args_item)) calls[i].arguments = dup_str(args_item->valuestring);
					}
				}
				response->tool_calls = calls;
				response->tool_calls_count = (size_t)n;
			}
		}
	}
	cJSON_Delete(root);
	return 0;
}

static int do_request(const char *url, const char *body, provider_response_t *response)
{
	CURL *curl = curl_easy_init();
	if (!curl) {
		set_error(response, "Failed to initialize curl");
		return -1;
	}
	size_t key_len = g_api_key ? strlen(g_api_key) : 0;
	size_t auth_header_size = key_len + 24;
	char *auth_header = malloc(auth_header_size);
	if (!auth_header) {
		curl_easy_cleanup(curl);
		set_error(response, "Out of memory");
		return -1;
	}
	snprintf(auth_header, auth_header_size, "Authorization: Bearer %s", g_api_key ? g_api_key : "");
	curl_buf_t resp_buf = { .buf = malloc(RESPONSE_BUF_INIT), .len = 0, .cap = RESPONSE_BUF_INIT };
	if (!resp_buf.buf) {
		free(auth_header);
		curl_easy_cleanup(curl);
		set_error(response, "Out of memory");
		return -1;
	}
	resp_buf.buf[0] = '\0';
	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, auth_header);
	curl_easy_setopt(curl, CURLOPT_URL, url);
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
	free(auth_header);
	curl_easy_cleanup(curl);
	if (res != CURLE_OK) {
		free(resp_buf.buf);
		set_error(response, curl_easy_strerror(res));
		return -1;
	}
	if (code < 200 || code >= 300) {
		char errmsg[160];
		snprintf(errmsg, sizeof(errmsg), "OpenAI API HTTP %ld", code);
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
	if (!g_cfg) { set_error(response, "OpenAI provider not initialized"); return -1; }
	const char *endpoint = config_provider_openai_endpoint(g_cfg);
	if (!endpoint || !endpoint[0]) endpoint = "https://api.openai.com/v1/chat/completions";
	cJSON *root = cJSON_CreateObject();
	if (!root) { set_error(response, "Out of memory"); return -1; }
	const char *model = config_agent_model(g_cfg);
	int max_tokens = config_agent_max_tokens(g_cfg);
	if (!model) model = "gpt-4o-mini";
	if (max_tokens <= 0) max_tokens = 4096;
	cJSON_AddItemToObject(root, "model", cJSON_CreateString(model));
	cJSON_AddItemToObject(root, "max_tokens", cJSON_CreateNumber(max_tokens));
	cJSON *msg_arr = cJSON_CreateArray();
	if (!msg_arr) { cJSON_Delete(root); set_error(response, "Out of memory"); return -1; }
	for (size_t i = 0; i < message_count; i++) {
		cJSON *msg = cJSON_CreateObject();
		if (!msg) break;
		const char *role = messages[i].role ? messages[i].role : "user";
		if (messages[i].tool_use_id) {
			cJSON_AddItemToObject(msg, "role", cJSON_CreateString("tool"));
			cJSON_AddItemToObject(msg, "tool_call_id", cJSON_CreateString(messages[i].tool_use_id));
			cJSON_AddItemToObject(msg, "content", cJSON_CreateString(messages[i].content ? messages[i].content : ""));
		} else {
			cJSON_AddItemToObject(msg, "role", cJSON_CreateString(role));
			cJSON_AddItemToObject(msg, "content", cJSON_CreateString(messages[i].content ? messages[i].content : ""));
		}
		if (messages[i].tool_calls && messages[i].tool_calls_count > 0 && strcmp(role, "assistant") == 0) {
			cJSON *tc_arr = cJSON_CreateArray();
			if (tc_arr) {
				for (size_t k = 0; k < messages[i].tool_calls_count; k++) {
					const provider_tool_call_t *tc = &messages[i].tool_calls[k];
					cJSON *tc_obj = cJSON_CreateObject();
					if (!tc_obj) break;
					cJSON_AddItemToObject(tc_obj, "id", cJSON_CreateString(tc->id ? tc->id : ""));
					cJSON_AddItemToObject(tc_obj, "type", cJSON_CreateString("function"));
					cJSON *fn = cJSON_CreateObject();
					if (fn) {
						cJSON_AddItemToObject(fn, "name", cJSON_CreateString(tc->name ? tc->name : ""));
						cJSON_AddItemToObject(fn, "arguments", cJSON_CreateString(tc->arguments ? tc->arguments : "{}"));
						cJSON_AddItemToObject(tc_obj, "function", fn);
					}
					cJSON_AddItemToArray(tc_arr, tc_obj);
				}
				cJSON_AddItemToObject(msg, "tool_calls", tc_arr);
			}
		}
		cJSON_AddItemToArray(msg_arr, msg);
	}
	cJSON_AddItemToObject(root, "messages", msg_arr);
	if (tool_count > 0 && tools) {
		cJSON *tools_arr = cJSON_CreateArray();
		if (tools_arr) {
			for (size_t i = 0; i < tool_count; i++) {
				cJSON *t = cJSON_CreateObject();
				if (!t) break;
				cJSON_AddItemToObject(t, "type", cJSON_CreateString("function"));
				cJSON *fn = cJSON_CreateObject();
				if (fn) {
					cJSON_AddItemToObject(fn, "name", cJSON_CreateString(tools[i].name ? tools[i].name : ""));
					cJSON_AddItemToObject(fn, "description", cJSON_CreateString(tools[i].description ? tools[i].description : ""));
					if (tools[i].parameters_json && tools[i].parameters_json[0]) {
						cJSON *params = cJSON_Parse(tools[i].parameters_json);
						if (params) cJSON_AddItemToObject(fn, "parameters", params);
					}
					cJSON_AddItemToObject(t, "function", fn);
				}
				cJSON_AddItemToArray(tools_arr, t);
			}
			cJSON_AddItemToObject(root, "tools", tools_arr);
		}
	}
	char *body = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (!body) { set_error(response, "Out of memory"); return -1; }
	int ret = do_request(endpoint, body, response);
	cJSON_free(body);
	return ret;
}

static int openai_init(const config_t *cfg)
{
	if (!cfg) return -1;
	const char *env_name = config_provider_openai_api_key_env(cfg);
	if (!env_name || !env_name[0]) return -1;
	const char *key = getenv(env_name);
	if (!key || !key[0]) return -1;
	free(g_api_key);
	g_api_key = strdup(key);
	g_cfg = g_api_key ? cfg : NULL;
	return g_api_key ? 0 : -1;
}

static int openai_chat(const provider_message_t *messages, size_t message_count,
                       const provider_tool_def_t *tools, size_t tool_count,
                       provider_response_t *response)
{
	if (!g_api_key || !g_cfg) {
		set_error(response, "OpenAI provider not initialized or API key missing");
		return -1;
	}
	provider_response_clear(response);
	return build_and_send(messages, message_count, tools, tool_count, response);
}

static void openai_cleanup(void)
{
	if (g_api_key) {
		volatile char *p = (volatile char *)g_api_key;
		for (size_t i = 0; g_api_key[i] != '\0'; i++) p[i] = '\0';
		free(g_api_key);
		g_api_key = NULL;
	}
	g_cfg = NULL;
}

static const provider_t openai_provider = {
	.name = "openai",
	.init = openai_init,
	.chat = openai_chat,
	.cleanup = openai_cleanup,
};

const provider_t *provider_openai_get(void)
{
	return &openai_provider;
}

#ifdef SHELLCLAW_TEST
int openai_parse_response_for_test(const char *json, provider_response_t *response)
{
	if (!json || !response) return -1;
	return parse_response_body(json, response);
}
#endif
