/**
 * @file http.c
 * @brief HTTP+WebSocket server using libwebsockets on same port.
 */
#define _POSIX_C_SOURCE 200809L

#include "gateway/http.h"
#include "gateway/auth.h"
#include "gateway/ws.h"
#include "gateway/static.h"
#include "asap/manifest.h"
#include "core/config.h"
#include "core/memory.h"
#include "core/skill.h"
#include "tools/cron.h"
#include "cJSON.h"
#include <libwebsockets.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum http_method {
	HTTP_GET = 1,
	HTTP_POST = 2,
	HTTP_PUT = 4,
	HTTP_DELETE = 5
};

#define GATEWAY_VERSION "0.2.0"
#define RESP_BUF_SIZE 65536
#define LWS_HEADER_SPACE 2048

typedef struct http_server_ctx {
	const config_t *cfg;
	auth_ctx_t *auth;
	char *config_path;
	time_t start_time;
	struct lws_context *lws_ctx;
	pthread_t thread;
	volatile int running;
} http_server_ctx_t;

#define BODY_BUF_SIZE 32768

typedef struct http_conn {
	char *response;
	size_t response_len;
	size_t response_sent;
	int headers_sent;
	int status;
	char body[BODY_BUF_SIZE];
	size_t body_len;
	int has_body;
	int is_static;
	const unsigned char *static_data;
	size_t static_len;
	const char *static_content_type;
	size_t static_sent;
} http_conn_t;

static http_server_ctx_t *g_ctx;

static int path_match(const char *uri, int uri_len, const char *prefix)
{
	size_t plen = strlen(prefix);
	return (uri_len >= (int)plen && strncmp(uri, prefix, plen) == 0);
}

static int path_eq(const char *uri, int uri_len, const char *path)
{
	size_t plen = strlen(path);
	return (uri_len == (int)plen && strncmp(uri, path, plen) == 0);
}

static const char *get_bearer_token(struct lws *wsi, char *buf, size_t buf_size)
{
	int n = lws_hdr_copy(wsi, buf, (int)buf_size, WSI_TOKEN_HTTP_AUTHORIZATION);
	if (n <= 0) return NULL;
	if (n < 8 || strncmp(buf, "Bearer ", 7) != 0) return NULL;
	return buf + 7;
}

static int is_static_path(const char *uri, int uri_len, int method)
{
	if (method != HTTP_GET) return 0;
	if (path_eq(uri, uri_len, "/health")) return 0;
	if (path_eq(uri, uri_len, "/pair")) return 0;
	if (path_match(uri, uri_len, "/.well-known/")) return 0;
	if (path_match(uri, uri_len, "/api/")) return 0;
	return 1;
}

static int requires_auth(const char *uri, int uri_len, int method)
{
	(void)method;
	if (path_eq(uri, uri_len, "/health")) return 0;
	if (path_eq(uri, uri_len, "/pair")) return 0;
	if (path_match(uri, uri_len, "/.well-known/")) return 0;
	if (path_eq(uri, uri_len, "/")) return 0;
	if (path_match(uri, uri_len, "/api/")) return 1;
	return 0;
}

static void json_response(char *buf, size_t size, int *status, const char *json)
{
	if (!buf || size == 0 || !status) return;
	*status = 200;
	size_t len = strlen(json);
	if (len >= size) len = size - 1;
	memcpy(buf, json, len);
	buf[len] = '\0';
}

static void json_error(char *buf, size_t size, int *status, int code, const char *msg)
{
	if (!buf || size == 0 || !status) return;
	*status = code;
	cJSON *obj = cJSON_CreateObject();
	if (obj) {
		cJSON_AddItemToObject(obj, "error", cJSON_CreateString(msg));
		char *s = cJSON_PrintUnformatted(obj);
		cJSON_Delete(obj);
		if (s) {
			size_t len = strlen(s);
			if (len >= size) len = size - 1;
			memcpy(buf, s, len);
			buf[len] = '\0';
			free(s);
		}
	}
}

static void handle_health(http_server_ctx_t *ctx, char *buf, size_t size, int *status)
{
	time_t now = time(NULL);
	long uptime = (long)(now - ctx->start_time);
	cJSON *obj = cJSON_CreateObject();
	if (!obj) { json_error(buf, size, status, 500, "Internal error"); return; }
	cJSON_AddItemToObject(obj, "status", cJSON_CreateString("ok"));
	cJSON_AddItemToObject(obj, "uptime", cJSON_CreateNumber((double)uptime));
	cJSON_AddItemToObject(obj, "version", cJSON_CreateString(GATEWAY_VERSION));
	char *s = cJSON_PrintUnformatted(obj);
	cJSON_Delete(obj);
	if (s) {
		json_response(buf, size, status, s);
		free(s);
	} else {
		json_error(buf, size, status, 500, "Internal error");
	}
}

static void handle_pair(http_server_ctx_t *ctx, struct lws *wsi, const char *body, size_t body_len,
                        char *buf, size_t size, int *status)
{
	char code[16] = {0};
	if (body_len > 0 && body) {
		cJSON *root = cJSON_ParseWithLength(body, body_len);
		if (root) {
			cJSON *c = cJSON_GetObjectItem(root, "code");
			if (cJSON_IsString(c) && c->valuestring) {
				strncpy(code, c->valuestring, sizeof(code) - 1);
			}
			c = cJSON_GetObjectItem(root, "pairing_code");
			if (!code[0] && cJSON_IsString(c) && c->valuestring)
				strncpy(code, c->valuestring, sizeof(code) - 1);
			cJSON_Delete(root);
		}
	}
	if (!code[0]) {
		int n = lws_hdr_custom_copy(wsi, code, sizeof(code), "X-Pairing-Code", 14);
		(void)n;
	}
	char token[64] = {0};
	if (auth_pair(ctx->auth, code, token, sizeof(token)) != 0) {
		json_error(buf, size, status, 400, "Invalid pairing code");
		return;
	}
	cJSON *obj = cJSON_CreateObject();
	if (!obj) { json_error(buf, size, status, 500, "Internal error"); return; }
	cJSON_AddItemToObject(obj, "token", cJSON_CreateString(token));
	char *s = cJSON_PrintUnformatted(obj);
	cJSON_Delete(obj);
	if (s) {
		*status = 200;
		json_response(buf, size, status, s);
		free(s);
	} else {
		json_error(buf, size, status, 500, "Internal error");
	}
}

static void handle_config_get(const config_t *cfg, char *buf, size_t size, int *status)
{
	cJSON *obj = cJSON_CreateObject();
	if (!obj) { json_error(buf, size, status, 500, "Internal error"); return; }
	cJSON_AddItemToObject(obj, "model", cJSON_CreateString(config_agent_model(cfg) ?: ""));
	cJSON_AddItemToObject(obj, "max_tokens", cJSON_CreateNumber(config_agent_max_tokens(cfg)));
	cJSON_AddItemToObject(obj, "temperature", cJSON_CreateNumber(config_agent_temperature(cfg)));
	cJSON_AddItemToObject(obj, "gateway_host", cJSON_CreateString(config_gateway_host(cfg) ?: ""));
	cJSON_AddItemToObject(obj, "gateway_port", cJSON_CreateNumber(config_gateway_port(cfg)));
	char *s = cJSON_PrintUnformatted(obj);
	cJSON_Delete(obj);
	if (s) {
		json_response(buf, size, status, s);
		free(s);
	} else {
		json_error(buf, size, status, 500, "Internal error");
	}
}

#define CONFIG_BODY_MAX 65536

static void handle_config_put(http_server_ctx_t *ctx, const char *body, size_t body_len,
                              char *buf, size_t size, int *status)
{
	if (!ctx->config_path || !body || body_len == 0) {
		json_error(buf, size, status, 400, "Bad request");
		return;
	}
	if (body_len > CONFIG_BODY_MAX) {
		json_error(buf, size, status, 400, "Config too large");
		return;
	}
	size_t path_len = strlen(ctx->config_path);
	char *tmp_path = malloc(path_len + 8);
	if (!tmp_path) { json_error(buf, size, status, 500, "Out of memory"); return; }
	snprintf(tmp_path, path_len + 8, "%s.tmp", ctx->config_path);
	FILE *f = fopen(tmp_path, "w");
	if (!f) {
		free(tmp_path);
		json_error(buf, size, status, 500, "Failed to write config");
		return;
	}
	size_t written = fwrite(body, 1, body_len, f);
	fclose(f);
	if (written != body_len) {
		unlink(tmp_path);
		free(tmp_path);
		json_error(buf, size, status, 500, "Failed to write config");
		return;
	}
	config_t *cfg = NULL;
	char errbuf[256] = {0};
	if (config_load(tmp_path, &cfg, errbuf, sizeof(errbuf)) != 0) {
		unlink(tmp_path);
		free(tmp_path);
		json_error(buf, size, status, 400, errbuf[0] ? errbuf : "Invalid TOML");
		return;
	}
	config_free(cfg);
	if (rename(tmp_path, ctx->config_path) != 0) {
		unlink(tmp_path);
		free(tmp_path);
		json_error(buf, size, status, 500, "Failed to save config");
		return;
	}
	free(tmp_path);
	*status = 200;
	json_response(buf, size, status, "{\"ok\":true}");
}

static void handle_skills_list(const config_t *cfg, char *buf, size_t size, int *status)
{
	char *names[64];
	int n = skill_list_names(cfg, names, 64);
	cJSON *arr = cJSON_CreateArray();
	if (!arr) { json_error(buf, size, status, 500, "Internal error"); return; }
	for (int i = 0; i < n; i++) {
		cJSON_AddItemToArray(arr, cJSON_CreateString(names[i]));
		free(names[i]);
	}
	char *s = cJSON_PrintUnformatted(arr);
	cJSON_Delete(arr);
	if (s) {
		json_response(buf, size, status, s);
		free(s);
	} else {
		json_error(buf, size, status, 500, "Internal error");
	}
}

#define SKILL_CONTENT_MAX 32768
#define MEMORY_RESULTS_MAX 16384

static void handle_skill_get(const config_t *cfg, const char *name, char *buf, size_t size, int *status)
{
	char *content = malloc(SKILL_CONTENT_MAX);
	if (!content) { json_error(buf, size, status, 500, "Out of memory"); return; }
	if (skill_get_content(cfg, name, content, SKILL_CONTENT_MAX) != 0) {
		free(content);
		json_error(buf, size, status, 404, "Skill not found");
		return;
	}
	cJSON *obj = cJSON_CreateObject();
	if (!obj) { free(content); json_error(buf, size, status, 500, "Internal error"); return; }
	cJSON_AddItemToObject(obj, "name", cJSON_CreateString(name));
	cJSON_AddItemToObject(obj, "content", cJSON_CreateString(content));
	free(content);
	char *s = cJSON_PrintUnformatted(obj);
	cJSON_Delete(obj);
	if (s) {
		json_response(buf, size, status, s);
		free(s);
	} else {
		json_error(buf, size, status, 500, "Internal error");
	}
}

static void handle_skill_create(const config_t *cfg, const char *body, size_t body_len,
                               char *buf, size_t size, int *status)
{
	cJSON *root = body ? cJSON_ParseWithLength(body, body_len) : NULL;
	if (!root) { json_error(buf, size, status, 400, "Invalid JSON"); return; }
	cJSON *name = cJSON_GetObjectItem(root, "name");
	cJSON *content = cJSON_GetObjectItem(root, "content");
	if (!cJSON_IsString(name) || !cJSON_IsString(content)) {
		cJSON_Delete(root);
		json_error(buf, size, status, 400, "name and content required");
		return;
	}
	int ret = skill_create(cfg, name->valuestring, content->valuestring);
	cJSON_Delete(root);
	if (ret != 0) {
		json_error(buf, size, status, 500, "Failed to create skill");
		return;
	}
	*status = 201;
	json_response(buf, size, status, "{\"ok\":true}");
}

static void handle_skill_update(const config_t *cfg, const char *name, const char *body, size_t body_len,
                                char *buf, size_t size, int *status)
{
	cJSON *root = body ? cJSON_ParseWithLength(body, body_len) : NULL;
	if (!root) { json_error(buf, size, status, 400, "Invalid JSON"); return; }
	cJSON *content = cJSON_GetObjectItem(root, "content");
	if (!cJSON_IsString(content)) {
		cJSON_Delete(root);
		json_error(buf, size, status, 400, "content required");
		return;
	}
	int ret = skill_update(cfg, name, content->valuestring);
	cJSON_Delete(root);
	if (ret != 0) {
		json_error(buf, size, status, 500, "Failed to update skill");
		return;
	}
	*status = 200;
	json_response(buf, size, status, "{\"ok\":true}");
}

static void handle_skill_delete(const config_t *cfg, const char *name,
                                char *buf, size_t size, int *status)
{
	if (skill_delete(cfg, name) != 0) {
		json_error(buf, size, status, 404, "Skill not found");
		return;
	}
	*status = 200;
	json_response(buf, size, status, "{\"ok\":true}");
}

static void handle_memory_get(const char *query, int limit, char *buf, size_t size, int *status)
{
	char *results = malloc(MEMORY_RESULTS_MAX);
	if (!results) { json_error(buf, size, status, 500, "Out of memory"); return; }
	if (memory_recall(query ? query : "", results, MEMORY_RESULTS_MAX, limit > 0 ? limit : 20) != 0) {
		free(results);
		json_error(buf, size, status, 500, "Memory recall failed");
		return;
	}
	cJSON *obj = cJSON_CreateObject();
	if (!obj) { free(results); json_error(buf, size, status, 500, "Internal error"); return; }
	cJSON_AddItemToObject(obj, "results", cJSON_CreateString(results));
	free(results);
	char *s = cJSON_PrintUnformatted(obj);
	cJSON_Delete(obj);
	if (s) {
		json_response(buf, size, status, s);
		free(s);
	} else {
		json_error(buf, size, status, 500, "Internal error");
	}
}

static void handle_sessions_list(char *buf, size_t size, int *status)
{
	char *ids[64];
	int n = session_list(ids, 64);
	cJSON *arr = cJSON_CreateArray();
	if (!arr) { json_error(buf, size, status, 500, "Internal error"); return; }
	for (int i = 0; i < n; i++) {
		cJSON_AddItemToArray(arr, cJSON_CreateString(ids[i]));
		free(ids[i]);
	}
	char *s = cJSON_PrintUnformatted(arr);
	cJSON_Delete(arr);
	if (s) {
		json_response(buf, size, status, s);
		free(s);
	} else {
		json_error(buf, size, status, 500, "Internal error");
	}
}

static void handle_session_delete(const char *id, char *buf, size_t size, int *status)
{
	if (session_delete(id) != 0) {
		json_error(buf, size, status, 404, "Session not found");
		return;
	}
	*status = 200;
	json_response(buf, size, status, "{\"ok\":true}");
}

static void handle_cron_list(char *buf, size_t size, int *status)
{
	cron_job_row_t *rows = calloc(64, sizeof(cron_job_row_t));
	if (!rows) { json_error(buf, size, status, 500, "Out of memory"); return; }
	int n = cron_job_list(rows, 64);
	cJSON *arr = cJSON_CreateArray();
	if (!arr) { free(rows); json_error(buf, size, status, 500, "Internal error"); return; }
	for (int i = 0; i < n; i++) {
		cJSON *obj = cJSON_CreateObject();
		if (!obj) break;
		cJSON_AddItemToObject(obj, "id", cJSON_CreateString(rows[i].id));
		cJSON_AddItemToObject(obj, "schedule", cJSON_CreateString(rows[i].schedule));
		cJSON_AddItemToObject(obj, "message", cJSON_CreateString(rows[i].message));
		cJSON_AddItemToObject(obj, "channel", cJSON_CreateString(rows[i].channel));
		cJSON_AddItemToObject(obj, "recipient", cJSON_CreateString(rows[i].recipient));
		cJSON_AddItemToObject(obj, "next_run", cJSON_CreateNumber((double)rows[i].next_run));
		cJSON_AddItemToObject(obj, "enabled", cJSON_CreateBool(rows[i].enabled));
		cJSON_AddItemToArray(arr, obj);
	}
	free(rows);
	char *s = cJSON_PrintUnformatted(arr);
	cJSON_Delete(arr);
	if (s) {
		json_response(buf, size, status, s);
		free(s);
	} else {
		json_error(buf, size, status, 500, "Internal error");
	}
}

static int read_urandom(unsigned char *out, size_t n)
{
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0) return -1;
	ssize_t r = read(fd, out, n);
	close(fd);
	return (r == (ssize_t)n) ? 0 : -1;
}

static void handle_cron_create(const char *body, size_t body_len, char *buf, size_t size, int *status)
{
	cJSON *root = body ? cJSON_ParseWithLength(body, body_len) : NULL;
	if (!root) { json_error(buf, size, status, 400, "Invalid JSON"); return; }
	cJSON *schedule = cJSON_GetObjectItem(root, "schedule");
	cJSON *message = cJSON_GetObjectItem(root, "message");
	if (!cJSON_IsString(schedule) || !cJSON_IsString(message)) {
		cJSON_Delete(root);
		json_error(buf, size, status, 400, "schedule and message required");
		return;
	}
	cJSON *id_node = cJSON_GetObjectItem(root, "id");
	cJSON *channel = cJSON_GetObjectItem(root, "channel");
	cJSON *recipient = cJSON_GetObjectItem(root, "recipient");
	char id[128];
	if (cJSON_IsString(id_node) && id_node->valuestring[0]) {
		snprintf(id, sizeof(id), "%.127s", id_node->valuestring);
	} else {
		unsigned char rnd[2];
		if (read_urandom(rnd, sizeof(rnd)) != 0) {
			cJSON_Delete(root);
			json_error(buf, size, status, 500, "RNG failure");
			return;
		}
		snprintf(id, sizeof(id), "cron_%ld_%02x%02x", (long)time(NULL), rnd[0], rnd[1]);
	}
	char sched_buf[128];
	char msg_buf[512];
	char ch_buf[64];
	char rec_buf[64];
	snprintf(sched_buf, sizeof(sched_buf), "%s", schedule->valuestring);
	snprintf(msg_buf, sizeof(msg_buf), "%s", message->valuestring);
	snprintf(ch_buf, sizeof(ch_buf), "%s",
	         (channel && cJSON_IsString(channel)) ? channel->valuestring : "cli");
	snprintf(rec_buf, sizeof(rec_buf), "%s",
	         (recipient && cJSON_IsString(recipient)) ? recipient->valuestring : "default");
	long long now = (long long)time(NULL);
	long long next = 0;
	if (cron_parse_next_run(sched_buf, now, &next) != 0) {
		cJSON_Delete(root);
		json_error(buf, size, status, 400, "Invalid schedule");
		return;
	}
	cJSON_Delete(root);
	if (cron_job_create(id, sched_buf, msg_buf, ch_buf, rec_buf, next, 1) != 0) {
		json_error(buf, size, status, 500, "Failed to create job");
		return;
	}
	*status = 201;
	cJSON *resp_obj = cJSON_CreateObject();
	if (resp_obj) {
		cJSON_AddBoolToObject(resp_obj, "ok", 1);
		cJSON_AddStringToObject(resp_obj, "id", id);
		char *s = cJSON_PrintUnformatted(resp_obj);
		cJSON_Delete(resp_obj);
		if (s) { json_response(buf, size, status, s); free(s); }
	}
}

static void handle_cron_delete(const char *id, char *buf, size_t size, int *status)
{
	if (cron_job_delete(id) != 0) {
		json_error(buf, size, status, 404, "Job not found");
		return;
	}
	*status = 200;
	json_response(buf, size, status, "{\"ok\":true}");
}

static void handle_cron_toggle(const char *id, char *buf, size_t size, int *status)
{
	if (cron_job_toggle(id) != 0) {
		json_error(buf, size, status, 404, "Job not found");
		return;
	}
	*status = 200;
	json_response(buf, size, status, "{\"ok\":true}");
}

static void handle_asap_stub(char *buf, size_t size, int *status)
{
	json_error(buf, size, status, 501, "Not Implemented");
}

static void handle_well_known(http_server_ctx_t *ctx, const char *uri, int uri_len,
                             char *buf, size_t size, int *status)
{
	if (path_eq(uri, uri_len, "/.well-known/asap/manifest.json")) {
		char *json = manifest_build_json(ctx ? ctx->cfg : NULL);
		if (!json) {
			json_error(buf, size, status, 500, "Internal error");
			return;
		}
		*status = 200;
		json_response(buf, size, status, json);
		free(json);
		return;
	}
	if (path_eq(uri, uri_len, "/.well-known/asap/health")) {
		*status = 200;
		json_response(buf, size, status, manifest_health_json());
		return;
	}
	json_error(buf, size, status, 404, "Not found");
}

static int extract_path_param(const char *uri, int uri_len, const char *prefix, char *out, size_t out_size)
{
	size_t plen = strlen(prefix);
	if (uri_len <= (int)plen) return -1;
	const char *rest = uri + plen;
	int rest_len = uri_len - (int)plen;
	const char *slash = memchr(rest, '/', (size_t)rest_len);
	int seg_len = slash ? (int)(slash - rest) : rest_len;
	if (seg_len <= 0 || (size_t)seg_len >= out_size) return -1;
	memcpy(out, rest, (size_t)seg_len);
	out[seg_len] = '\0';
	return 0;
}

static int dispatch_route(http_server_ctx_t *ctx, struct lws *wsi, int method,
                          const char *uri, int uri_len, const char *body, size_t body_len,
                          char *buf, size_t size, int *status)
{
	if (path_eq(uri, uri_len, "/health")) {
		handle_health(ctx, buf, size, status);
		return 0;
	}
	if (path_eq(uri, uri_len, "/pair") && method == HTTP_POST) {
		handle_pair(ctx, wsi, body, body_len, buf, size, status);
		return 0;
	}
	if (path_match(uri, uri_len, "/.well-known/")) {
		handle_well_known(ctx, uri, uri_len, buf, size, status);
		return 0;
	}
	if (path_eq(uri, uri_len, "/api/config")) {
		if (method == HTTP_GET) handle_config_get(ctx->cfg, buf, size, status);
		else if (method == HTTP_PUT) handle_config_put(ctx, body, body_len, buf, size, status);
		else json_error(buf, size, status, 405, "Method not allowed");
		return 0;
	}
	if (path_eq(uri, uri_len, "/api/skills")) {
		if (method == HTTP_GET) handle_skills_list(ctx->cfg, buf, size, status);
		else if (method == HTTP_POST) handle_skill_create(ctx->cfg, body, body_len, buf, size, status);
		else json_error(buf, size, status, 405, "Method not allowed");
		return 0;
	}
	if (path_match(uri, uri_len, "/api/skills/")) {
		char name[128];
		if (extract_path_param(uri, uri_len, "/api/skills/", name, sizeof(name)) != 0) {
			json_error(buf, size, status, 404, "Not found");
			return 0;
		}
		if (method == HTTP_GET) handle_skill_get(ctx->cfg, name, buf, size, status);
		else if (method == HTTP_PUT) handle_skill_update(ctx->cfg, name, body, body_len, buf, size, status);
		else if (method == HTTP_DELETE) handle_skill_delete(ctx->cfg, name, buf, size, status);
		else json_error(buf, size, status, 405, "Method not allowed");
		return 0;
	}
	if (path_match(uri, uri_len, "/api/memory")) {
		if (method != HTTP_GET) { json_error(buf, size, status, 405, "Method not allowed"); return 0; }
		char qbuf[256] = {0};
		char lbuf[32] = {0};
		lws_get_urlarg_by_name_safe(wsi, "q", qbuf, sizeof(qbuf));
		lws_get_urlarg_by_name_safe(wsi, "limit", lbuf, sizeof(lbuf));
		int limit = lbuf[0] ? atoi(lbuf) : 20;
		handle_memory_get(qbuf[0] ? qbuf : NULL, limit, buf, size, status);
		return 0;
	}
	if (path_eq(uri, uri_len, "/api/sessions")) {
		if (method == HTTP_GET) handle_sessions_list(buf, size, status);
		else json_error(buf, size, status, 405, "Method not allowed");
		return 0;
	}
	if (path_match(uri, uri_len, "/api/sessions/")) {
		if (method != HTTP_DELETE) { json_error(buf, size, status, 405, "Method not allowed"); return 0; }
		char id[128];
		if (extract_path_param(uri, uri_len, "/api/sessions/", id, sizeof(id)) != 0) {
			json_error(buf, size, status, 404, "Not found");
			return 0;
		}
		handle_session_delete(id, buf, size, status);
		return 0;
	}
	if (path_eq(uri, uri_len, "/api/cron")) {
		if (method == HTTP_GET) handle_cron_list(buf, size, status);
		else if (method == HTTP_POST) handle_cron_create(body, body_len, buf, size, status);
		else json_error(buf, size, status, 405, "Method not allowed");
		return 0;
	}
	if (path_match(uri, uri_len, "/api/cron/")) {
		char id[128];
		if (extract_path_param(uri, uri_len, "/api/cron/", id, sizeof(id)) != 0) {
			json_error(buf, size, status, 404, "Not found");
			return 0;
		}
		size_t suffix = strlen("/api/cron/") + strlen(id);
		int is_toggle = (uri_len >= (int)(suffix + 8) &&
		                 strncmp(uri + suffix, "/toggle", 7) == 0);
		if (is_toggle) {
			if (method == HTTP_POST) handle_cron_toggle(id, buf, size, status);
			else json_error(buf, size, status, 405, "Method not allowed");
		} else {
			if (method == HTTP_DELETE) handle_cron_delete(id, buf, size, status);
			else json_error(buf, size, status, 405, "Method not allowed");
		}
		return 0;
	}
	if (path_eq(uri, uri_len, "/asap") && method == HTTP_POST) {
		handle_asap_stub(buf, size, status);
		return 0;
	}
	json_error(buf, size, status, 404, "Not found");
	return 0;
}

static int http_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user,
                         void *in, size_t len)
{
	http_server_ctx_t *ctx = g_ctx;
	if (!ctx) return 0;
	switch (reason) {
	case LWS_CALLBACK_HTTP: {
		char uri[256];
		int uri_len = lws_hdr_copy(wsi, uri, sizeof(uri), WSI_TOKEN_GET_URI);
		if (uri_len <= 0)
			uri_len = lws_hdr_copy(wsi, uri, sizeof(uri), WSI_TOKEN_POST_URI);
		if (uri_len <= 0 || uri_len >= (int)sizeof(uri)) {
			lws_return_http_status(wsi, 400, "Bad request");
			lws_http_transaction_completed(wsi);
			return 0;
		}
		int method = HTTP_GET;
		{
			char meth_buf[32] = {0};
			int n = lws_hdr_custom_copy(wsi, meth_buf, sizeof(meth_buf), ":method", 7);
			if (n <= 0) {
				int t = lws_hdr_total_length(wsi, WSI_TOKEN_HTTP);
				if (t > 0 && t < 16) {
					char req[16];
					lws_hdr_copy(wsi, req, sizeof(req), WSI_TOKEN_HTTP);
					if (strncmp(req, "POST", 4) == 0) method = HTTP_POST;
					else if (strncmp(req, "PUT", 3) == 0) method = HTTP_PUT;
					else if (strncmp(req, "DELETE", 6) == 0) method = HTTP_DELETE;
				}
			} else {
				if (strcmp(meth_buf, "POST") == 0) method = HTTP_POST;
				else if (strcmp(meth_buf, "PUT") == 0) method = HTTP_PUT;
				else if (strcmp(meth_buf, "DELETE") == 0) method = HTTP_DELETE;
			}
		}
		if (requires_auth(uri, uri_len, method)) {
			char auth_buf[256];
			const char *token = get_bearer_token(wsi, auth_buf, sizeof(auth_buf));
			if (!token || !auth_validate_token(ctx->auth, token)) {
				lws_return_http_status(wsi, 401, "{\"error\":\"Unauthorized\"}");
				lws_http_transaction_completed(wsi);
				return 0;
			}
		}
		http_conn_t *conn = calloc(1, sizeof(*conn));
		if (!conn) {
			lws_return_http_status(wsi, 500, "Internal error");
			lws_http_transaction_completed(wsi);
			return 0;
		}
		conn->response = malloc(RESP_BUF_SIZE);
		if (!conn->response) {
			free(conn);
			lws_return_http_status(wsi, 500, "Internal error");
			lws_http_transaction_completed(wsi);
			return 0;
		}
		conn->status = 200;
		conn->has_body = (method == HTTP_POST || method == HTTP_PUT);
		if (!conn->has_body && is_static_path(uri, uri_len, method)) {
			const unsigned char *data = NULL;
			size_t data_len = 0;
			const char *ct = NULL;
			char path_buf[256];
			if (uri_len >= (int)sizeof(path_buf)) uri_len = (int)sizeof(path_buf) - 1;
			memcpy(path_buf, uri, (size_t)uri_len);
			path_buf[uri_len] = '\0';
			if (static_lookup(path_buf, &data, &data_len, &ct) == 0) {
				conn->is_static = 1;
				conn->static_data = data;
				conn->static_len = data_len;
				conn->static_content_type = ct;
				conn->static_sent = 0;
				free(conn->response);
				conn->response = NULL;
				lws_set_wsi_user(wsi, conn);
				lws_callback_on_writable(wsi);
				return 0;
			}
		}
		if (!conn->has_body) {
			dispatch_route(ctx, wsi, method, uri, uri_len, NULL, 0, conn->response, RESP_BUF_SIZE, &conn->status);
			conn->response_len = strlen(conn->response);
			lws_set_wsi_user(wsi, conn);
			lws_callback_on_writable(wsi);
		} else {
			conn->body[0] = '\0';
			conn->body_len = 0;
			lws_set_wsi_user(wsi, conn);
		}
		return 0;
	}
	case LWS_CALLBACK_HTTP_BODY: {
		http_conn_t *conn = lws_get_wsi_user(wsi);
		if (conn && in && len > 0) {
			size_t remain = BODY_BUF_SIZE - conn->body_len - 1;
			if (len < remain) remain = len;
			memcpy(conn->body + conn->body_len, in, remain);
			conn->body_len += remain;
			conn->body[conn->body_len] = '\0';
		}
		return 0;
	}
	case LWS_CALLBACK_HTTP_BODY_COMPLETION: {
		http_conn_t *conn = lws_get_wsi_user(wsi);
		if (conn && conn->has_body) {
			char uri[256];
			int uri_len = lws_hdr_copy(wsi, uri, sizeof(uri), WSI_TOKEN_GET_URI);
			if (uri_len <= 0) uri_len = lws_hdr_copy(wsi, uri, sizeof(uri), WSI_TOKEN_POST_URI);
			int method = HTTP_POST;
			{
				char mb[32] = {0};
				if (lws_hdr_custom_copy(wsi, mb, sizeof(mb), ":method", 7) > 0) {
					if (strcmp(mb, "PUT") == 0) method = HTTP_PUT;
					else if (strcmp(mb, "DELETE") == 0) method = HTTP_DELETE;
				}
			}
			dispatch_route(ctx, wsi, method, uri, uri_len, conn->body, conn->body_len,
			               conn->response, RESP_BUF_SIZE, &conn->status);
			conn->response_len = strlen(conn->response);
			conn->has_body = 0;
		}
		lws_callback_on_writable(wsi);
		return 0;
	}
	case LWS_CALLBACK_HTTP_WRITEABLE: {
		http_conn_t *conn = lws_get_wsi_user(wsi);
		if (!conn) return 0;
		if (conn->is_static) {
			if (!conn->headers_sent) {
				unsigned char buf[LWS_PRE + LWS_HEADER_SPACE];
				unsigned char *p = buf + LWS_PRE;
				unsigned char *end = buf + sizeof(buf) - LWS_PRE;
				size_t ct_len = strlen(conn->static_content_type);
				if (lws_add_http_header_status(wsi, 200, &p, end) ||
				    lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_TYPE,
				         (unsigned char *)conn->static_content_type, (unsigned int)ct_len, &p, end) ||
				    lws_add_http_header_by_name(wsi, (unsigned char *)"content-encoding",
				         (unsigned char *)"gzip", 4, &p, end) ||
				    lws_add_http_header_content_length(wsi, (lws_filepos_t)conn->static_len, &p, end) ||
				    lws_finalize_http_header(wsi, &p, end))
					return -1;
				int n = (int)(p - (buf + LWS_PRE));
				if (lws_write(wsi, buf + LWS_PRE, (size_t)n, LWS_WRITE_HTTP_HEADERS) != n)
					return -1;
				conn->headers_sent = 1;
			}
			if (conn->static_sent < conn->static_len) {
				size_t to_send = conn->static_len - conn->static_sent;
				if (to_send > 4096) to_send = 4096;
				int m = lws_write(wsi, conn->static_data + conn->static_sent, to_send,
				                  conn->static_sent + to_send >= conn->static_len ?
				                  LWS_WRITE_HTTP_FINAL : LWS_WRITE_HTTP);
				if (m < 0) return -1;
				conn->static_sent += (size_t)m;
			}
			if (conn->static_sent >= conn->static_len) {
				free(conn);
				lws_set_wsi_user(wsi, NULL);
				lws_http_transaction_completed(wsi);
			}
			return 0;
		}
		if (!conn->response) return 0;
		if (!conn->headers_sent) {
			unsigned char buf[LWS_PRE + LWS_HEADER_SPACE];
			unsigned char *p = buf + LWS_PRE;
			unsigned char *end = buf + sizeof(buf) - LWS_PRE;
			if (lws_add_http_header_status(wsi, (unsigned int)conn->status, &p, end) ||
			    lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_TYPE,
			         (unsigned char *)"application/json", 16, &p, end) ||
			    lws_add_http_header_content_length(wsi, (lws_filepos_t)conn->response_len, &p, end) ||
			    lws_finalize_http_header(wsi, &p, end))
				return -1;
			int n = (int)(p - (buf + LWS_PRE));
			if (lws_write(wsi, buf + LWS_PRE, (size_t)n, LWS_WRITE_HTTP_HEADERS) != n)
				return -1;
			conn->headers_sent = 1;
		}
		if (conn->response_sent < conn->response_len) {
			size_t to_send = conn->response_len - conn->response_sent;
			if (to_send > 4096) to_send = 4096;
			int flags = (conn->response_sent + to_send >= conn->response_len) ?
			    LWS_WRITE_HTTP_FINAL : LWS_WRITE_HTTP;
			int m = lws_write(wsi, (unsigned char *)conn->response + conn->response_sent,
			                 to_send, flags);
			if (m < 0) return -1;
			conn->response_sent += (size_t)m;
		}
		if (conn->response_sent >= conn->response_len) {
			free(conn->response);
			free(conn);
			lws_set_wsi_user(wsi, NULL);
			lws_http_transaction_completed(wsi);
		} else {
			lws_callback_on_writable(wsi);
		}
		return 0;
	}
	case LWS_CALLBACK_CLOSED_HTTP: {
		http_conn_t *conn = lws_get_wsi_user(wsi);
		if (conn) {
			if (conn->response) free(conn->response);
			free(conn);
			lws_set_wsi_user(wsi, NULL);
		}
		return 0;
	}
	default:
		break;
	}
	return 0;
}

static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user,
                      void *in, size_t len)
{
	http_server_ctx_t *ctx = g_ctx;
	switch (reason) {
	case LWS_CALLBACK_HTTP_CONFIRM_UPGRADE: {
		if (!ctx || !ctx->auth) return -1;
		char token_buf[256] = {0};
		if (lws_get_urlarg_by_name_safe(wsi, "token", token_buf, sizeof(token_buf)) <= 0)
			return -1;
		if (!auth_validate_token(ctx->auth, token_buf))
			return -1;
		return 0;
	}
	case LWS_CALLBACK_ESTABLISHED: {
		int conn_id = ws_next_conn_id();
		lws_set_wsi_user(wsi, (void *)(intptr_t)conn_id);
		ws_register_conn(conn_id, (ws_conn_t)wsi);
		break;
	}
	case LWS_CALLBACK_SERVER_WRITEABLE: {
		int conn_id = (int)(intptr_t)lws_get_wsi_user(wsi);
		if (conn_id <= 0) break;
		char buf[8192];
		size_t len_out = 0;
		if (ws_dequeue_outgoing(conn_id, buf, sizeof(buf), &len_out)) {
			unsigned char frame[LWS_PRE + 8192];
			if (len_out < sizeof(frame) - LWS_PRE) {
				memcpy(frame + LWS_PRE, buf, len_out);
				if (lws_write(wsi, frame + LWS_PRE, len_out, LWS_WRITE_TEXT) < 0)
					break;
			}
			if (ws_has_pending_outgoing(conn_id))
				lws_callback_on_writable(wsi);
		}
		break;
	}
	case LWS_CALLBACK_RECEIVE: {
		int conn_id = (int)(intptr_t)lws_get_wsi_user(wsi);
		if (conn_id <= 0) break;
		if (len > 0 && in) {
			char *buf = malloc(len + 1);
			if (buf) {
				memcpy(buf, in, len);
				buf[len] = '\0';
				cJSON *root = cJSON_Parse(buf);
				free(buf);
				if (root) {
					cJSON *type = cJSON_GetObjectItem(root, "type");
					cJSON *text = cJSON_GetObjectItem(root, "text");
					if (cJSON_IsString(type) && strcmp(type->valuestring, "message") == 0 &&
					    cJSON_IsString(text) && text->valuestring)
						ws_push_incoming(conn_id, text->valuestring);
					cJSON_Delete(root);
				}
			}
		}
		break;
	}
	case LWS_CALLBACK_CLOSED: {
		int conn_id = (int)(intptr_t)lws_get_wsi_user(wsi);
		if (conn_id > 0) ws_unregister_conn(conn_id);
		break;
	}
	default:
		break;
	}
	return 0;
}

static const struct lws_protocols protocols[] = {
	{ "http", http_callback, 0, RESP_BUF_SIZE },
	{ "ws", ws_callback, 0, 256 },
	{ NULL, NULL, 0, 0 }
};

static const struct lws_http_mount mount_ws = {
	.mountpoint = "/ws",
	.origin = "protocol",
	.def = "ws",
	.protocol = "ws",
	.origin_protocol = LWSMPRO_CALLBACK,
	.mount_next = NULL,
};

static void *http_thread_fn(void *arg)
{
	http_server_ctx_t *ctx = arg;
	while (ctx->running && ctx->lws_ctx)
		lws_service(ctx->lws_ctx, 50);
	return NULL;
}

int http_start(const config_t *cfg, struct auth_ctx *auth_ctx, const char *config_path)
{
	if (!cfg || !auth_ctx || g_ctx) return -1;
	const char *host = config_gateway_host(cfg);
	int port = config_gateway_port(cfg);
	if (strcmp(host, "0.0.0.0") == 0 && !config_gateway_allow_bind_all(cfg))
		return -1;
	http_server_ctx_t *ctx = calloc(1, sizeof(*ctx));
	if (!ctx) return -1;
	ctx->cfg = cfg;
	ctx->auth = auth_ctx;
	ctx->config_path = config_path ? strdup(config_path) : NULL;
	ctx->start_time = time(NULL);
	ctx->running = 1;
	struct lws_context_creation_info info;
	memset(&info, 0, sizeof(info));
	info.port = port;
	info.protocols = protocols;
	info.options = LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY;
	info.mounts = &mount_ws;
	ctx->lws_ctx = lws_create_context(&info);
	if (!ctx->lws_ctx) {
		free(ctx->config_path);
		free(ctx);
		return -1;
	}
	ws_set_context(ctx->lws_ctx);
	g_ctx = ctx;
	if (pthread_create(&ctx->thread, NULL, http_thread_fn, ctx) != 0) {
		lws_context_destroy(ctx->lws_ctx);
		free(ctx->config_path);
		free(ctx);
		g_ctx = NULL;
		return -1;
	}
	return 0;
}

void http_stop(void)
{
	if (!g_ctx) return;
	g_ctx->running = 0;
	if (g_ctx->thread) {
		pthread_join(g_ctx->thread, NULL);
		g_ctx->thread = 0;
	}
	if (g_ctx->lws_ctx) {
		lws_context_destroy(g_ctx->lws_ctx);
		g_ctx->lws_ctx = NULL;
	}
	free(g_ctx->config_path);
	free(g_ctx);
	g_ctx = NULL;
}
