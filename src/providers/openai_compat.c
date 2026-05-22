/**
 * @file openai_compat.c
 * @brief Shared OpenAI Chat Completions JSON body build and HTTP POST.
 */
#define _POSIX_C_SOURCE 200809L

#include "providers/openai_compat.h"
#include "cJSON.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

static int append_messages_array(cJSON *msg_arr, const provider_message_t *messages,
                                 size_t message_count, provider_response_t *response)
{
	for (size_t i = 0; i < message_count; i++) {
		cJSON *msg = cJSON_CreateObject();
		const char *role;
		if (!msg) {
			provider_set_error(response, "Out of memory");
			return -1;
		}
		role = messages[i].role ? messages[i].role : "user";
		if (messages[i].tool_use_id) {
			cJSON_AddItemToObject(msg, "role", cJSON_CreateString("tool"));
			cJSON_AddItemToObject(msg, "tool_call_id",
			                      cJSON_CreateString(messages[i].tool_use_id));
			cJSON_AddItemToObject(msg, "content",
			                      cJSON_CreateString(messages[i].content ? messages[i].content
			                                                               : ""));
		} else {
			cJSON_AddItemToObject(msg, "role", cJSON_CreateString(role));
			cJSON_AddItemToObject(msg, "content",
			                      cJSON_CreateString(messages[i].content ? messages[i].content
			                                                               : ""));
		}
		if (messages[i].tool_calls && messages[i].tool_calls_count > 0 &&
		    strcmp(role, "assistant") == 0) {
			cJSON *tc_arr = cJSON_CreateArray();
			if (tc_arr) {
				for (size_t k = 0; k < messages[i].tool_calls_count; k++) {
					const provider_tool_call_t *tc = &messages[i].tool_calls[k];
					cJSON *tc_obj = cJSON_CreateObject();
					cJSON *fn;
					if (!tc_obj)
						break;
					cJSON_AddItemToObject(tc_obj, "id",
					                      cJSON_CreateString(tc->id ? tc->id : ""));
					cJSON_AddItemToObject(tc_obj, "type", cJSON_CreateString("function"));
					fn = cJSON_CreateObject();
					if (fn) {
						cJSON_AddItemToObject(fn, "name",
						                      cJSON_CreateString(tc->name ? tc->name : ""));
						cJSON_AddItemToObject(fn, "arguments",
						                      cJSON_CreateString(tc->arguments ? tc->arguments
						                                                       : "{}"));
						cJSON_AddItemToObject(tc_obj, "function", fn);
					}
					cJSON_AddItemToArray(tc_arr, tc_obj);
				}
				cJSON_AddItemToObject(msg, "tool_calls", tc_arr);
			}
		}
		cJSON_AddItemToArray(msg_arr, msg);
	}
	return 0;
}

static int append_tools_array(cJSON *root, const provider_tool_def_t *tools, size_t tool_count)
{
	cJSON *tools_arr;
	if (tool_count == 0 || !tools)
		return 0;
	tools_arr = cJSON_CreateArray();
	if (!tools_arr)
		return -1;
	for (size_t i = 0; i < tool_count; i++) {
		cJSON *t = cJSON_CreateObject();
		cJSON *fn;
		if (!t)
			break;
		cJSON_AddItemToObject(t, "type", cJSON_CreateString("function"));
		fn = cJSON_CreateObject();
		if (fn) {
			cJSON_AddItemToObject(fn, "name",
			                      cJSON_CreateString(tools[i].name ? tools[i].name : ""));
			cJSON_AddItemToObject(fn, "description",
			                      cJSON_CreateString(tools[i].description ? tools[i].description
			                                                                : ""));
			if (tools[i].parameters_json && tools[i].parameters_json[0]) {
				cJSON *params = cJSON_Parse(tools[i].parameters_json);
				if (params)
					cJSON_AddItemToObject(fn, "parameters", params);
			}
			cJSON_AddItemToObject(t, "function", fn);
		}
		cJSON_AddItemToArray(tools_arr, t);
	}
	cJSON_AddItemToObject(root, "tools", tools_arr);
	return 0;
}

char *openai_compat_build_chat_body(const char *model, int max_tokens, double temperature,
                                    const provider_message_t *messages, size_t message_count,
                                    const provider_tool_def_t *tools, size_t tool_count,
                                    provider_response_t *response)
{
	cJSON *root;
	cJSON *msg_arr;
	char *body;
	if (!model || !model[0]) {
		provider_set_error(response, "Model not configured");
		return NULL;
	}
	if (max_tokens <= 0)
		max_tokens = 4096;
	root = cJSON_CreateObject();
	if (!root) {
		provider_set_error(response, "Out of memory");
		return NULL;
	}
	cJSON_AddItemToObject(root, "model", cJSON_CreateString(model));
	cJSON_AddItemToObject(root, "max_tokens", cJSON_CreateNumber(max_tokens));
	cJSON_AddItemToObject(root, "temperature", cJSON_CreateNumber(temperature));
	msg_arr = cJSON_CreateArray();
	if (!msg_arr) {
		cJSON_Delete(root);
		provider_set_error(response, "Out of memory");
		return NULL;
	}
	if (append_messages_array(msg_arr, messages, message_count, response) != 0) {
		cJSON_Delete(root);
		return NULL;
	}
	cJSON_AddItemToObject(root, "messages", msg_arr);
	if (append_tools_array(root, tools, tool_count) != 0) {
		cJSON_Delete(root);
		provider_set_error(response, "Out of memory");
		return NULL;
	}
	body = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (!body)
		provider_set_error(response, "Out of memory");
	return body;
}

int openai_compat_post_chat(const char *url, const char *body, const char *bearer_token,
                            const char *http_error_label, int nosignal, long request_timeout_sec,
                            long connect_timeout_sec, provider_response_t *response)
{
	CURL *curl;
	struct curl_slist *headers = NULL;
	provider_curl_buf_t resp_buf = {.buf = malloc(PROVIDER_RESP_BUF_INIT),
	                                .len = 0,
	                                .cap = PROVIDER_RESP_BUF_INIT};
	char *auth_header = NULL;
	CURLcode res;
	long code = 0;
	int parse_ret;
	if (!url || !body) {
		provider_set_error(response, "Invalid request");
		return -1;
	}
	curl = curl_easy_init();
	if (!curl) {
		provider_set_error(response, "Failed to initialize curl");
		return -1;
	}
	if (!resp_buf.buf) {
		curl_easy_cleanup(curl);
		provider_set_error(response, "Out of memory");
		return -1;
	}
	resp_buf.buf[0] = '\0';
	headers = curl_slist_append(headers, "Content-Type: application/json");
	if (bearer_token && bearer_token[0]) {
		size_t auth_header_size;
		auth_header_size = strlen(bearer_token) + 24U;
		auth_header = malloc(auth_header_size);
		if (!auth_header) {
			curl_slist_free_all(headers);
			free(resp_buf.buf);
			curl_easy_cleanup(curl);
			provider_set_error(response, "Out of memory");
			return -1;
		}
		snprintf(auth_header, auth_header_size, "Authorization: Bearer %s", bearer_token);
		headers = curl_slist_append(headers, auth_header);
	}
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, provider_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_buf);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, request_timeout_sec);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connect_timeout_sec);
	if (nosignal)
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	res = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	curl_slist_free_all(headers);
	free(auth_header);
	curl_easy_cleanup(curl);
	if (res != CURLE_OK) {
		free(resp_buf.buf);
		provider_set_error(response, curl_easy_strerror(res));
		return -1;
	}
	if (code < 200 || code >= 300) {
		char errmsg[160];
		const char *label = http_error_label ? http_error_label : "API";
		snprintf(errmsg, sizeof(errmsg), "%s HTTP %ld", label, code);
		free(resp_buf.buf);
		provider_set_error(response, errmsg);
		return -1;
	}
	parse_ret = provider_parse_chat_completions_json(resp_buf.buf, response);
	free(resp_buf.buf);
	return parse_ret;
}
