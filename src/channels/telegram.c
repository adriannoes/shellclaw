/**
 * @file telegram.c
 * @brief Telegram channel: long-poll getUpdates, allowlist, /reset, /status.
 */
#define _POSIX_C_SOURCE 200809L

#include "channels/channel.h"
#include "core/config.h"
#include "cJSON.h"
#include <curl/curl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TG_API_BASE "https://api.telegram.org/bot"
#define TG_GET_UPDATES "getUpdates"
#define TG_SEND_MSG "sendMessage"
#define TG_GET_FILE "getFile"
#define TG_FILE_BASE "https://api.telegram.org/file/bot"
#define POLL_TIMEOUT_SEC 30
#define RESP_BUF_SIZE (256 * 1024)

struct telegram_ctx {
	char *token;
	const config_t *cfg;
	long last_update_id;
	int initialized;
};

static struct telegram_ctx g_tg;

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

static int is_user_allowed(const config_t *cfg, long user_id)
{
	char id_buf[32];
	snprintf(id_buf, sizeof(id_buf), "%ld", user_id);
	int n = config_telegram_allowed_users_count(cfg);
	for (int i = 0; i < n; i++) {
		const char *allowed = config_telegram_allowed_user(cfg, i);
		if (allowed && strcmp(allowed, id_buf) == 0)
			return 1;
	}
	return 0;
}

static int tg_init(const config_t *cfg)
{
	if (!cfg || !config_telegram_enabled(cfg)) return 0;
	const char *env_name = config_telegram_token_env(cfg);
	if (!env_name) return -1;
	const char *token = getenv(env_name);
	if (!token || !token[0]) return -1;
	free(g_tg.token);
	g_tg.token = strdup(token);
	g_tg.cfg = cfg;
	g_tg.last_update_id = 0;
	g_tg.initialized = 1;
	return 0;
}

static char *make_session_id(long user_id)
{
	char buf[64];
	snprintf(buf, sizeof(buf), "telegram:%ld", user_id);
	return strdup(buf);
}

static int parse_update(const char *json, channel_incoming_msg_t *out, long *update_id_out)
{
	cJSON *root = cJSON_Parse(json);
	if (!root || !cJSON_IsObject(root)) {
		if (root) cJSON_Delete(root);
		return -1;
	}
	cJSON *result = cJSON_GetObjectItem(root, "result");
	if (!result || !cJSON_IsArray(result)) {
		cJSON_Delete(root);
		return -1;
	}
	int arr_len = cJSON_GetArraySize(result);
	if (arr_len == 0) {
		cJSON_Delete(root);
		return 0;
	}
	cJSON *first = cJSON_GetArrayItem(result, 0);
	if (!first || !cJSON_IsObject(first)) {
		cJSON_Delete(root);
		return -1;
	}
	cJSON *uid = cJSON_GetObjectItem(first, "update_id");
	if (!uid || !cJSON_IsNumber(uid)) {
		cJSON_Delete(root);
		return -1;
	}
	*update_id_out = (long)uid->valuedouble;
	cJSON *msg = cJSON_GetObjectItem(first, "message");
	if (!msg || !cJSON_IsObject(msg)) {
		cJSON_Delete(root);
		return 0;
	}
	cJSON *from = cJSON_GetObjectItem(msg, "from");
	if (!from || !cJSON_IsObject(from)) {
		cJSON_Delete(root);
		return -1;
	}
	cJSON *from_id = cJSON_GetObjectItem(from, "id");
	if (!from_id || !cJSON_IsNumber(from_id)) {
		cJSON_Delete(root);
		return -1;
	}
	long user_id = (long)from_id->valuedouble;
	if (!is_user_allowed(g_tg.cfg, user_id)) {
		cJSON_Delete(root);
		return 0;
	}
	out->session_id = make_session_id(user_id);
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "%ld", user_id);
		out->user_id = strdup(buf);
	}
	cJSON *text = cJSON_GetObjectItem(msg, "text");
	out->text = text && cJSON_IsString(text) ? strdup(text->valuestring) : strdup("");
	cJSON *photo = cJSON_GetObjectItem(msg, "photo");
	out->attachments = NULL;
	out->attachments_count = 0;
	if (photo && cJSON_IsArray(photo)) {
		int n = cJSON_GetArraySize(photo);
		cJSON *largest = n > 0 ? cJSON_GetArrayItem(photo, n - 1) : NULL;
		if (largest && cJSON_IsObject(largest)) {
			cJSON *fid = cJSON_GetObjectItem(largest, "file_id");
			if (fid && cJSON_IsString(fid)) {
				out->attachments = malloc(sizeof(channel_attachment_t));
				if (out->attachments) {
					out->attachments[0].path_or_base64 = strdup(fid->valuestring);
					out->attachments[0].size = strlen(fid->valuestring);
					out->attachments[0].is_base64 = 0;
					out->attachments_count = 1;
				}
			}
		}
	}
	cJSON *caption = cJSON_GetObjectItem(msg, "caption");
	if (caption && cJSON_IsString(caption) && out->text && out->text[0] == '\0') {
		free(out->text);
		out->text = strdup(caption->valuestring);
	}
	cJSON_Delete(root);
	return 1;
}

static int tg_poll(channel_incoming_msg_t *out, int timeout_ms)
{
	if (!out || !g_tg.initialized || !g_tg.token) return -1;
	memset(out, 0, sizeof(*out));
	int timeout_sec = (timeout_ms / 1000) > 0 ? (timeout_ms / 1000) : 1;
	char url[512];
	snprintf(url, sizeof(url), "%s%s/%s?timeout=%d&offset=%ld",
	         TG_API_BASE, g_tg.token, TG_GET_UPDATES, timeout_sec, g_tg.last_update_id + 1);
	CURL *curl = curl_easy_init();
	if (!curl) return -1;
	char *resp = NULL;
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)(timeout_sec + 5));
	CURLcode res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	if (res != CURLE_OK || !resp) {
		free(resp);
		return 0;
	}
	long update_id = 0;
	int r = parse_update(resp, out, &update_id);
	free(resp);
	if (r > 0) {
		g_tg.last_update_id = update_id;
		return 1;
	}
	if (r < 0) return -1;
	/* Advance offset even when we ignore the update (user not allowed, etc.) */
	if (update_id > 0)
		g_tg.last_update_id = update_id;
	return 0;
}

static int tg_send(const char *recipient, const char *text,
                   const channel_attachment_t *attachments, size_t att_count)
{
	(void)attachments;
	(void)att_count;
	if (!g_tg.initialized || !g_tg.token || !recipient || !text) return -1;
	const char *chat_id = strchr(recipient, ':');
	if (chat_id) chat_id++;
	else chat_id = recipient;
	char url[256];
	snprintf(url, sizeof(url), "%s%s/%s", TG_API_BASE, g_tg.token, TG_SEND_MSG);
	CURL *curl = curl_easy_init();
	if (!curl) return -1;
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
	char *escaped = curl_easy_escape(curl, text, (int)strlen(text));
	if (!escaped) {
		curl_easy_cleanup(curl);
		return -1;
	}
	size_t need = strlen(chat_id) + strlen(escaped) + 32;
	char *postfields = malloc(need);
	int ret = -1;
	if (postfields) {
		snprintf(postfields, need, "chat_id=%s&text=%s", chat_id, escaped);
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postfields);
		CURLcode res = curl_easy_perform(curl);
		ret = (res == CURLE_OK) ? 0 : -1;
		free(postfields);
	}
	curl_free(escaped);
	curl_easy_cleanup(curl);
	return ret;
}

static void tg_cleanup(void)
{
	free(g_tg.token);
	g_tg.token = NULL;
	g_tg.cfg = NULL;
	g_tg.initialized = 0;
}

static const channel_t tg_channel = {
	.name = "telegram",
	.init = tg_init,
	.poll = tg_poll,
	.send = tg_send,
	.cleanup = tg_cleanup,
};

const channel_t *channel_telegram_get(void)
{
	return &tg_channel;
}
