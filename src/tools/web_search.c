/**
 * @file web_search.c
 * @brief Web search tool: Brave Search (when API key set) or DuckDuckGo fallback.
 */
#define _POSIX_C_SOURCE 200809L

#include "tools/tool.h"
#include "tools/web_search.h"
#include "core/config.h"
#include "cJSON.h"
#include <curl/curl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DDG_URL "https://api.duckduckgo.com/?q=%s&format=json"
#define BRAVE_URL "https://api.search.brave.com/res/v1/web/search?q=%s&count=10"
#define RESP_BUF_SIZE (64 * 1024)

static const config_t *g_web_search_cfg;

static const char WEB_SEARCH_PARAMS[] =
	"{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\",\"description\":\"Search query\"}},\"required\":[\"query\"]}";

static size_t write_cb(const char *ptr, size_t size, size_t nmemb, void *userdata)
{
	if (nmemb != 0 && size > SIZE_MAX / nmemb) return 0;
	size_t total = size * nmemb;
	char **buf = (char **)userdata;
	size_t cur = *buf ? strlen(*buf) : 0;
	char *new_buf = realloc(*buf, cur + total + 1);
	if (!new_buf) return 0;
	*buf = new_buf;
	memcpy(new_buf + cur, ptr, total);
	new_buf[cur + total] = '\0';
	return total;
}

void tool_web_search_set_config(const config_t *cfg)
{
	g_web_search_cfg = cfg;
}

static void append_result(char *out, size_t max_len, size_t *used, const char *text)
{
	if (!text || !text[0]) return;
	size_t len = strlen(text);
	if (*used + len + 2 >= max_len) return;
	if (*used > 0) {
		out[*used] = '\n';
		(*used)++;
	}
	memcpy(out + *used, text, len + 1);
	*used += len;
}

static int search_brave(const char *query, char *result_buf, size_t max_len)
{
	const char *env_name = (g_web_search_cfg != NULL) ? config_brave_api_key_env(g_web_search_cfg) : "BRAVE_API_KEY";
	const char *api_key = getenv(env_name);
	if (!api_key || !api_key[0]) return -1;
	char url[1024];
	CURL *curl = curl_easy_init();
	if (!curl) return -1;
	char *escaped = curl_easy_escape(curl, query, (int)strlen(query));
	if (!escaped) { curl_easy_cleanup(curl); return -1; }
	snprintf(url, sizeof(url), BRAVE_URL, escaped);
	curl_free(escaped);
	char auth_header[256];
	snprintf(auth_header, sizeof(auth_header), "X-Subscription-Token: %s", api_key);
	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, auth_header);
	char *resp = NULL;
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
	CURLcode res = curl_easy_perform(curl);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	if (res != CURLE_OK || !resp) {
		free(resp);
		return -1;
	}
	cJSON *root = cJSON_Parse(resp);
	free(resp);
	if (!root || !cJSON_IsObject(root)) {
		if (root) cJSON_Delete(root);
		return -1;
	}
	cJSON *web = cJSON_GetObjectItem(root, "web");
	cJSON *results = web && cJSON_IsObject(web) ? cJSON_GetObjectItem(web, "results") : NULL;
	size_t used = 0;
	result_buf[0] = '\0';
	if (results && cJSON_IsArray(results)) {
		int n = cJSON_GetArraySize(results);
		for (int i = 0; i < n && i < 10; i++) {
			cJSON *item = cJSON_GetArrayItem(results, i);
			if (!item || !cJSON_IsObject(item)) continue;
			cJSON *title = cJSON_GetObjectItem(item, "title");
			cJSON *desc = cJSON_GetObjectItem(item, "description");
			cJSON *url_obj = cJSON_GetObjectItem(item, "url");
			if (title && cJSON_IsString(title) && title->valuestring[0])
				append_result(result_buf, max_len, &used, title->valuestring);
			if (desc && cJSON_IsString(desc) && desc->valuestring[0])
				append_result(result_buf, max_len, &used, desc->valuestring);
			if (url_obj && cJSON_IsString(url_obj) && url_obj->valuestring[0])
				append_result(result_buf, max_len, &used, url_obj->valuestring);
		}
	}
	cJSON_Delete(root);
	return (used > 0) ? 0 : -1;
}

static int search_duckduckgo(const char *query, char *result_buf, size_t max_len)
{
	char url[1024];
	CURL *curl = curl_easy_init();
	if (!curl) return -1;
	char *escaped = curl_easy_escape(curl, query, (int)strlen(query));
	if (!escaped) { curl_easy_cleanup(curl); return -1; }
	snprintf(url, sizeof(url), DDG_URL, escaped);
	curl_free(escaped);
	char *resp = NULL;
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
	CURLcode res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	if (res != CURLE_OK || !resp) {
		free(resp);
		return -1;
	}
	cJSON *ddg = cJSON_Parse(resp);
	free(resp);
	if (!ddg || !cJSON_IsObject(ddg)) {
		if (ddg) cJSON_Delete(ddg);
		return -1;
	}
	size_t used = 0;
	result_buf[0] = '\0';
	cJSON *abstract = cJSON_GetObjectItem(ddg, "AbstractText");
	if (abstract && cJSON_IsString(abstract) && abstract->valuestring[0])
		append_result(result_buf, max_len, &used, abstract->valuestring);
	cJSON *answer = cJSON_GetObjectItem(ddg, "Answer");
	if (answer && cJSON_IsString(answer) && answer->valuestring[0])
		append_result(result_buf, max_len, &used, answer->valuestring);
	cJSON *related = cJSON_GetObjectItem(ddg, "RelatedTopics");
	if (related && cJSON_IsArray(related)) {
		int n = cJSON_GetArraySize(related);
		for (int i = 0; i < n && i < 5; i++) {
			cJSON *item = cJSON_GetArrayItem(related, i);
			if (!item || !cJSON_IsObject(item)) continue;
			cJSON *text = cJSON_GetObjectItem(item, "Text");
			if (text && cJSON_IsString(text) && text->valuestring[0])
				append_result(result_buf, max_len, &used, text->valuestring);
		}
	}
	cJSON *results = cJSON_GetObjectItem(ddg, "Results");
	if (results && cJSON_IsArray(results)) {
		int n = cJSON_GetArraySize(results);
		for (int i = 0; i < n && i < 5; i++) {
			cJSON *item = cJSON_GetArrayItem(results, i);
			if (!item || !cJSON_IsObject(item)) continue;
			cJSON *text = cJSON_GetObjectItem(item, "Text");
			if (text && cJSON_IsString(text) && text->valuestring[0])
				append_result(result_buf, max_len, &used, text->valuestring);
		}
	}
	cJSON_Delete(ddg);
	return (used > 0) ? 0 : -1;
}

static int web_search_execute(const char *args_json, char *result_buf, size_t max_len)
{
	if (!args_json || !result_buf || max_len == 0) return -1;
	cJSON *root = cJSON_Parse(args_json);
	if (!root || !cJSON_IsObject(root)) {
		if (root) cJSON_Delete(root);
		snprintf(result_buf, max_len, "{\"error\":\"invalid JSON\"}");
		return -1;
	}
	cJSON *q = cJSON_GetObjectItem(root, "query");
	if (!q || !cJSON_IsString(q)) {
		cJSON_Delete(root);
		snprintf(result_buf, max_len, "{\"error\":\"missing or invalid 'query'\"}");
		return -1;
	}
	char *query = strdup(q->valuestring ? q->valuestring : "");
	cJSON_Delete(root);
	if (!query) {
		snprintf(result_buf, max_len, "{\"error\":\"out of memory\"}");
		return -1;
	}
	int r = search_brave(query, result_buf, max_len);
	if (r != 0)
		r = search_duckduckgo(query, result_buf, max_len);
	if (r != 0 && result_buf[0] == '\0')
		snprintf(result_buf, max_len, "No results found for: %s", query);
	free(query);
	return 0;
}

static const tool_t WEB_SEARCH_TOOL = {
	.name = "web_search",
	.description = "Search the web. Uses Brave Search when BRAVE_API_KEY is set, else DuckDuckGo. Returns snippets and links.",
	.parameters_json = WEB_SEARCH_PARAMS,
	.execute = web_search_execute,
};

const tool_t *tool_web_search_get(void)
{
	return &WEB_SEARCH_TOOL;
}
