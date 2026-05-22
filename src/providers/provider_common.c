/**
 * @file provider_common.c
 * @brief Shared provider helpers: response cleanup, curl buffer, error helpers.
 */

#include "providers/provider.h"
#include "cJSON.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void provider_response_clear(provider_response_t *r)
{
	if (!r) return;
	free(r->content);
	r->content = NULL;
	if (r->tool_calls) {
		for (size_t i = 0; i < r->tool_calls_count; i++) {
			free(r->tool_calls[i].id);
			free(r->tool_calls[i].name);
			free(r->tool_calls[i].arguments);
		}
		free(r->tool_calls);
		r->tool_calls = NULL;
		r->tool_calls_count = 0;
	}
	r->error = 0;
}

size_t provider_write_cb(const char *ptr, size_t size, size_t nmemb, void *userdata)
{
	provider_curl_buf_t *b = (provider_curl_buf_t *)userdata;
	if (!b || !b->buf) return 0;
	if (nmemb != 0 && size > SIZE_MAX / nmemb) return 0;
	size_t n = size * nmemb;
	size_t need = b->len + n + 1;
	if (need > PROVIDER_RESP_BUF_INIT * 4) return 0;
	if (need > b->cap) {
		size_t new_cap = b->cap ? b->cap * 2 : PROVIDER_RESP_BUF_INIT;
		while (new_cap < need && new_cap <= PROVIDER_RESP_BUF_INIT * 4) new_cap *= 2;
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

void provider_set_error(provider_response_t *response, const char *msg)
{
	if (!response)
		return;
	response->error = 1;
	response->content = msg ? provider_dup_str(msg) : NULL;
}

char *provider_dup_str(const char *s)
{
	if (!s) return NULL;
	size_t n = strlen(s) + 1;
	char *p = malloc(n);
	if (p) memcpy(p, s, n);
	return p;
}

int provider_error_allows_fallback_retry(const char *msg)
{
	const char *h;
	char *endp;
	long code;
	if (!msg || msg[0] == '\0') return 1;
	h = strstr(msg, "HTTP ");
	if (!h) return 1;
	h += 5;
	code = strtol(h, &endp, 10);
	if (endp == h) return 1;
	if (code >= 400 && code <= 499) return 0;
	return 1;
}

int provider_parse_chat_completions_json(const char *response_body, provider_response_t *response)
{
	cJSON *root;
	cJSON *err_obj;
	cJSON *choices;
	cJSON *choice;
	cJSON *msg_obj;
	cJSON *content_item;
	cJSON *tool_calls_arr;
	if (!response_body || !response) {
		if (response)
			provider_set_error(response, "missing response body");
		return -1;
	}
	root = cJSON_Parse(response_body);
	if (!root) {
		provider_set_error(response, "Failed to parse OpenAI response JSON");
		return -1;
	}
	err_obj = cJSON_GetObjectItem(root, "error");
	if (cJSON_IsObject(err_obj)) {
		cJSON *msg = cJSON_GetObjectItem(err_obj, "message");
		const char *errmsg = cJSON_IsString(msg) ? msg->valuestring : "OpenAI API error";
		provider_set_error(response, errmsg);
		cJSON_Delete(root);
		return -1;
	}
	choices = cJSON_GetObjectItem(root, "choices");
	if (!cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
		cJSON_Delete(root);
		return 0;
	}
	choice = cJSON_GetArrayItem(choices, 0);
	msg_obj = cJSON_GetObjectItem(choice, "message");
	if (!cJSON_IsObject(msg_obj)) {
		cJSON_Delete(root);
		return 0;
	}
	response->content = NULL;
	response->tool_calls = NULL;
	response->tool_calls_count = 0;
	content_item = cJSON_GetObjectItem(msg_obj, "content");
	if (cJSON_IsString(content_item) && content_item->valuestring)
		response->content = provider_dup_str(content_item->valuestring);
	else
		response->content = malloc(1);
	if (response->content && !cJSON_IsString(content_item))
		response->content[0] = '\0';
	tool_calls_arr = cJSON_GetObjectItem(msg_obj, "tool_calls");
	if (cJSON_IsArray(tool_calls_arr)) {
		int n = cJSON_GetArraySize(tool_calls_arr);
		if (n > 0) {
			provider_tool_call_t *calls = malloc((size_t)n * sizeof(provider_tool_call_t));
			if (calls) {
				for (int i = 0; i < n; i++) {
					cJSON *tc = cJSON_GetArrayItem(tool_calls_arr, i);
					cJSON *id_item;
					cJSON *fn;
					calls[i].id = NULL;
					calls[i].name = NULL;
					calls[i].arguments = NULL;
					id_item = cJSON_GetObjectItem(tc, "id");
					fn = cJSON_GetObjectItem(tc, "function");
					if (cJSON_IsString(id_item)) calls[i].id = provider_dup_str(id_item->valuestring);
					if (cJSON_IsObject(fn)) {
						cJSON *name_item = cJSON_GetObjectItem(fn, "name");
						cJSON *args_item = cJSON_GetObjectItem(fn, "arguments");
						if (cJSON_IsString(name_item)) calls[i].name = provider_dup_str(name_item->valuestring);
						if (cJSON_IsString(args_item)) calls[i].arguments = provider_dup_str(args_item->valuestring);
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
