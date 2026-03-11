/**
 * @file test_gateway_http.c
 * @brief Integration tests for gateway HTTP: health, pair, auth, manifest, config, skills, memory, cron.
 * Requires libwebsockets and SHELLCLAW_GATEWAY. Starts server in subprocess.
 */
#define _POSIX_C_SOURCE 200809L

#include "gateway/auth.h"
#include "core/config.h"
#include "cJSON.h"
#include <curl/curl.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
#define BASE_URL "http://127.0.0.1:18789"
#define TEST_HOME "/tmp/shellclaw_test_gw_run"

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *user)
{
	size_t total = size * nmemb;
	char **buf = (char **)user;
	size_t prev = *buf ? strlen(*buf) : 0;
	char *new_buf = realloc(*buf, prev + total + 1);
	if (!new_buf) return 0;
	*buf = new_buf;
	memcpy(new_buf + prev, ptr, total);
	new_buf[prev + total] = '\0';
	return total;
}

static int http_get(const char *url, long *code_out, char **body_out)
{
	CURL *curl = curl_easy_init();
	if (!curl) return -1;
	*body_out = NULL;
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, body_out);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
	CURLcode res = curl_easy_perform(curl);
	long code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	curl_easy_cleanup(curl);
	if (code_out) *code_out = code;
	return (res == CURLE_OK) ? 0 : -1;
}

static int http_post(const char *url, const char *json, long *code_out, char **body_out)
{
	CURL *curl = curl_easy_init();
	if (!curl) return -1;
	*body_out = NULL;
	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, body_out);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
	CURLcode res = curl_easy_perform(curl);
	long code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	if (code_out) *code_out = code;
	return (res == CURLE_OK) ? 0 : -1;
}

static int http_get_auth(const char *url, const char *bearer, long *code_out, char **body_out)
{
	CURL *curl = curl_easy_init();
	if (!curl) return -1;
	*body_out = NULL;
	struct curl_slist *headers = NULL;
	char auth_hdr[256];
	snprintf(auth_hdr, sizeof(auth_hdr), "Authorization: Bearer %s", bearer);
	headers = curl_slist_append(headers, auth_hdr);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, body_out);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
	CURLcode res = curl_easy_perform(curl);
	long code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	if (code_out) *code_out = code;
	return (res == CURLE_OK) ? 0 : -1;
}

static int http_put_auth(const char *url, const char *bearer, const char *json, long *code_out, char **body_out)
{
	CURL *curl = curl_easy_init();
	if (!curl) return -1;
	*body_out = NULL;
	struct curl_slist *headers = NULL;
	char auth_hdr[256];
	snprintf(auth_hdr, sizeof(auth_hdr), "Authorization: Bearer %s", bearer);
	headers = curl_slist_append(headers, auth_hdr);
	headers = curl_slist_append(headers, "Content-Type: application/json");
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, body_out);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
	CURLcode res = curl_easy_perform(curl);
	long code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	if (code_out) *code_out = code;
	return (res == CURLE_OK) ? 0 : -1;
}

static int http_post_auth(const char *url, const char *bearer, const char *json, long *code_out, char **body_out)
{
	CURL *curl = curl_easy_init();
	if (!curl) return -1;
	*body_out = NULL;
	struct curl_slist *headers = NULL;
	char auth_hdr[256];
	snprintf(auth_hdr, sizeof(auth_hdr), "Authorization: Bearer %s", bearer);
	headers = curl_slist_append(headers, auth_hdr);
	headers = curl_slist_append(headers, "Content-Type: application/json");
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, body_out);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
	CURLcode res = curl_easy_perform(curl);
	long code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	if (code_out) *code_out = code;
	return (res == CURLE_OK) ? 0 : -1;
}

static int http_delete_auth(const char *url, const char *bearer, long *code_out, char **body_out)
{
	CURL *curl = curl_easy_init();
	if (!curl) return -1;
	*body_out = NULL;
	struct curl_slist *headers = NULL;
	char auth_hdr[256];
	snprintf(auth_hdr, sizeof(auth_hdr), "Authorization: Bearer %s", bearer);
	headers = curl_slist_append(headers, auth_hdr);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, body_out);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
	CURLcode res = curl_easy_perform(curl);
	long code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	if (code_out) *code_out = code;
	return (res == CURLE_OK) ? 0 : -1;
}

static int test_health(void)
{
	long code;
	char *body = NULL;
	int r = http_get(BASE_URL "/health", &code, &body);
	ASSERT(r == 0);
	ASSERT(code == 200);
	ASSERT(body != NULL);
	ASSERT(strstr(body, "ok") != NULL);
	ASSERT(strstr(body, "uptime") != NULL);
	free(body);
	return 0;
}

static int test_pair(const char *pairing_code, char *token_out, size_t token_size)
{
	if (!pairing_code || !token_out || token_size == 0) return 1;
	char post_json[128];
	snprintf(post_json, sizeof(post_json), "{\"code\":\"%s\"}", pairing_code);
	long code_http;
	char *body = NULL;
	int r = http_post(BASE_URL "/pair", post_json, &code_http, &body);
	ASSERT(r == 0);
	ASSERT(code_http == 200);
	ASSERT(body != NULL);
	ASSERT(strstr(body, "token") != NULL);
	cJSON *root = cJSON_Parse(body);
	ASSERT(root != NULL);
	cJSON *tok = cJSON_GetObjectItem(root, "token");
	ASSERT(tok != NULL && cJSON_IsString(tok));
	size_t len = strlen(tok->valuestring);
	if (len >= token_size) len = token_size - 1;
	memcpy(token_out, tok->valuestring, len);
	token_out[len] = '\0';
	cJSON_Delete(root);
	free(body);
	return 0;
}

static int test_api_config_401(void)
{
	long code;
	char *body = NULL;
	int r = http_get(BASE_URL "/api/config", &code, &body);
	ASSERT(r == 0);
	ASSERT(code == 401);
	free(body);
	return 0;
}

static int test_asap_501(void)
{
	long code;
	char *body = NULL;
	int r = http_post(BASE_URL "/asap", "{}", &code, &body);
	ASSERT(r == 0);
	ASSERT(code == 501);
	ASSERT(body != NULL);
	ASSERT(strstr(body, "error") != NULL);
	ASSERT(strstr(body, "Not Implemented") != NULL);
	free(body);
	return 0;
}

static int test_manifest(void)
{
	long code;
	char *body = NULL;
	int r = http_get(BASE_URL "/.well-known/asap/manifest.json", &code, &body);
	ASSERT(r == 0);
	ASSERT(code == 200);
	ASSERT(body != NULL);
	ASSERT(strstr(body, "id") != NULL);
	ASSERT(strstr(body, "urn:asap:agent") != NULL);
	ASSERT(strstr(body, "skills") != NULL);
	ASSERT(strstr(body, "endpoints") != NULL);
	free(body);
	return 0;
}

static int test_health_wellknown(void)
{
	long code;
	char *body = NULL;
	int r = http_get(BASE_URL "/.well-known/asap/health", &code, &body);
	ASSERT(r == 0);
	ASSERT(code == 200);
	ASSERT(body != NULL);
	ASSERT(strstr(body, "status") != NULL);
	ASSERT(strstr(body, "ok") != NULL);
	free(body);
	return 0;
}

static int test_api_config_get(const char *token)
{
	long code;
	char *body = NULL;
	int r = http_get_auth(BASE_URL "/api/config", token, &code, &body);
	ASSERT(r == 0);
	ASSERT(code == 200);
	ASSERT(body != NULL);
	ASSERT(strstr(body, "model") != NULL);
	free(body);
	return 0;
}

static int test_api_skills_list(const char *token)
{
	long code;
	char *body = NULL;
	int r = http_get_auth(BASE_URL "/api/skills", token, &code, &body);
	ASSERT(r == 0);
	ASSERT(code == 200);
	ASSERT(body != NULL);
	ASSERT(strstr(body, "[") != NULL);
	free(body);
	return 0;
}

static int test_api_skill_create_delete(const char *token)
{
	long code;
	char *body = NULL;
	int r = http_post_auth(BASE_URL "/api/skills", token,
		"{\"name\":\"test_integration_skill\",\"content\":\"# Test skill for integration\"}",
		&code, &body);
	ASSERT(r == 0);
	ASSERT(code == 200 || code == 201);
	free(body);
	body = NULL;
	r = http_delete_auth(BASE_URL "/api/skills/test_integration_skill", token, &code, &body);
	ASSERT(r == 0);
	ASSERT(code == 200);
	free(body);
	return 0;
}

static int test_api_memory(const char *token)
{
	long code;
	char *body = NULL;
	int r = http_get_auth(BASE_URL "/api/memory?q=test&limit=5", token, &code, &body);
	ASSERT(r == 0);
	ASSERT(code == 200);
	ASSERT(body != NULL);
	free(body);
	return 0;
}

static int test_api_cron_list(const char *token)
{
	long code;
	char *body = NULL;
	int r = http_get_auth(BASE_URL "/api/cron", token, &code, &body);
	ASSERT(r == 0);
	ASSERT(code == 200);
	ASSERT(body != NULL);
	ASSERT(strstr(body, "[") != NULL);
	free(body);
	return 0;
}

static int test_api_cron_create_delete(const char *token)
{
	long code;
	char *body = NULL;
	int r = http_post_auth(BASE_URL "/api/cron", token,
		"{\"schedule\":\"interval:3600\",\"message\":\"integration test\",\"channel\":\"cli\",\"recipient\":\"default\"}",
		&code, &body);
	ASSERT(r == 0);
	ASSERT(code == 200 || code == 201);
	ASSERT(body != NULL);
	cJSON *root = cJSON_Parse(body);
	ASSERT(root != NULL);
	cJSON *id_obj = cJSON_GetObjectItem(root, "id");
	ASSERT(id_obj != NULL && cJSON_IsString(id_obj));
	char del_url[256];
	snprintf(del_url, sizeof(del_url), BASE_URL "/api/cron/%s", id_obj->valuestring);
	cJSON_Delete(root);
	free(body);
	body = NULL;
	r = http_delete_auth(del_url, token, &code, &body);
	ASSERT(r == 0);
	ASSERT(code == 200);
	free(body);
	return 0;
}

static int test_api_sessions(const char *token)
{
	long code;
	char *body = NULL;
	int r = http_get_auth(BASE_URL "/api/sessions", token, &code, &body);
	ASSERT(r == 0);
	ASSERT(code == 200);
	ASSERT(body != NULL);
	free(body);
	return 0;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
#ifndef SHELLCLAW_GATEWAY
	fprintf(stderr, "test_gateway_http: skipped (gateway not built)\n");
	return 0;
#else
	char config_path[256];
	char skills_dir[256];
	char db_path[256];
	snprintf(config_path, sizeof(config_path), "%s/config.toml", TEST_HOME);
	snprintf(skills_dir, sizeof(skills_dir), "%s/.shellclaw/skills", TEST_HOME);
	snprintf(db_path, sizeof(db_path), "%s/.shellclaw/memory.db", TEST_HOME);
	if (mkdir(TEST_HOME, 0755) != 0 && errno != EEXIST) {
		fprintf(stderr, "test_gateway_http: mkdir failed\n");
		return 1;
	}
	if (mkdir(TEST_HOME "/.shellclaw", 0755) != 0 && errno != EEXIST) {
		fprintf(stderr, "test_gateway_http: mkdir .shellclaw failed\n");
		return 1;
	}
	if (mkdir(skills_dir, 0755) != 0 && errno != EEXIST) {
		fprintf(stderr, "test_gateway_http: mkdir skills failed\n");
		return 1;
	}
	unlink(TEST_HOME "/.shellclaw/auth_tokens.json");
	FILE *f = fopen(config_path, "w");
	if (!f) {
		fprintf(stderr, "test_gateway_http: cannot write config\n");
		return 1;
	}
	fprintf(f, "[agent]\nmodel = \"test\"\n");
	fprintf(f, "[gateway]\nenabled = true\nhost = \"127.0.0.1\"\nport = 18789\n");
	fprintf(f, "[memory]\ndb_path = \"%s/.shellclaw/memory.db\"\n", TEST_HOME);
	fprintf(f, "[skills]\ndir = \"%s\"\n", skills_dir);
	fclose(f);
	int pipefd[2];
	if (pipe(pipefd) != 0) {
		fprintf(stderr, "test_gateway_http: pipe failed\n");
		return 1;
	}
	setenv("HOME", TEST_HOME, 1);
	pid_t pid = fork();
	if (pid < 0) {
		fprintf(stderr, "test_gateway_http: fork failed\n");
		return 1;
	}
	if (pid == 0) {
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		close(pipefd[1]);
		execl("./build/shellclaw", "shellclaw", "--config", config_path, (char *)NULL);
		_exit(1);
	}
	close(pipefd[1]);
	char read_buf[512] = {0};
	size_t total = 0;
	while (total < sizeof(read_buf) - 1) {
		ssize_t n = read(pipefd[0], read_buf + total, sizeof(read_buf) - 1 - total);
		if (n <= 0) break;
		total += (size_t)n;
		if (strstr(read_buf, "ShellClaw pairing code:") != NULL) break;
	}
	close(pipefd[0]);
	char pairing_code[16] = {0};
	const char *prefix = "ShellClaw pairing code: ";
	char *p = strstr(read_buf, prefix);
	if (p) {
		p += strlen(prefix);
		for (int i = 0; i < 6 && p[i] >= '0' && p[i] <= '9'; i++)
			pairing_code[i] = p[i];
	}
	sleep(3);
	char token[128] = {0};
	int failed = 0;
	if (test_health() != 0) { fprintf(stderr, "test_health failed\n"); failed++; }
	if (pairing_code[0] && test_pair(pairing_code, token, sizeof(token)) != 0) {
		fprintf(stderr, "test_pair failed\n");
		failed++;
	}
	if (test_api_config_401() != 0) { fprintf(stderr, "test_api_config_401 failed\n"); failed++; }
	if (test_manifest() != 0) { fprintf(stderr, "test_manifest failed\n"); failed++; }
	if (test_health_wellknown() != 0) { fprintf(stderr, "test_health_wellknown failed\n"); failed++; }
	if (test_asap_501() != 0) { fprintf(stderr, "test_asap_501 failed\n"); failed++; }
	if (token[0]) {
		if (test_api_config_get(token) != 0) { fprintf(stderr, "test_api_config_get failed\n"); failed++; }
		if (test_api_skills_list(token) != 0) { fprintf(stderr, "test_api_skills_list failed\n"); failed++; }
		if (test_api_skill_create_delete(token) != 0) { fprintf(stderr, "test_api_skill_create_delete failed\n"); failed++; }
		if (test_api_memory(token) != 0) { fprintf(stderr, "test_api_memory failed\n"); failed++; }
		if (test_api_cron_list(token) != 0) { fprintf(stderr, "test_api_cron_list failed\n"); failed++; }
		if (test_api_cron_create_delete(token) != 0) { fprintf(stderr, "test_api_cron_create_delete failed\n"); failed++; }
		if (test_api_sessions(token) != 0) { fprintf(stderr, "test_api_sessions failed\n"); failed++; }
	}
	kill(pid, SIGTERM);
	waitpid(pid, NULL, 0);
	unlink(config_path);
	unlink(TEST_HOME "/.shellclaw/auth_tokens.json");
	unlink(db_path);
	if (failed == 0)
		printf("test_gateway_http: all tests passed\n");
	return failed;
#endif
}
