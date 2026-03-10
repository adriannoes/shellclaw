/**
 * @file test_gateway_http.c
 * @brief Integration tests for gateway HTTP: health, pair, 401 on /api/config.
 * Requires libwebsockets and SHELLCLAW_GATEWAY. Starts server in subprocess.
 */
#define _POSIX_C_SOURCE 200809L

#include "gateway/auth.h"
#include "core/config.h"
#include <curl/curl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
#define BASE_URL "http://127.0.0.1:18789"

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

static int test_pair(void)
{
	config_t *cfg = NULL;
	char errbuf[256];
	const char *config_path = "/tmp/shellclaw_test_gateway_config.toml";
	FILE *f = fopen(config_path, "w");
	ASSERT(f);
	fprintf(f, "[agent]\nmodel = \"test\"\n[gateway]\nenabled = true\nhost = \"127.0.0.1\"\nport = 18789\n");
	fclose(f);
	int load_ret = config_load(config_path, &cfg, errbuf, sizeof(errbuf));
	ASSERT(load_ret == 0);
	auth_ctx_t *auth = auth_init("/tmp/shellclaw_test_gateway_tokens.json");
	ASSERT(auth != NULL);
	unlink("/tmp/shellclaw_test_gateway_tokens.json");
	char *code = auth_get_or_create_pairing_code(auth);
	ASSERT(code != NULL);
	char post_json[128];
	snprintf(post_json, sizeof(post_json), "{\"code\":\"%s\"}", code);
	long code_http;
	char *body = NULL;
	int r = http_post(BASE_URL "/pair", post_json, &code_http, &body);
	ASSERT(r == 0);
	ASSERT(code_http == 200);
	ASSERT(body != NULL);
	ASSERT(strstr(body, "token") != NULL);
	free(code);
	free(body);
	auth_cleanup(auth);
	config_free(cfg);
	unlink(config_path);
	unlink("/tmp/shellclaw_test_gateway_tokens.json");
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

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
#ifndef SHELLCLAW_GATEWAY
	fprintf(stderr, "test_gateway_http: skipped (gateway not built)\n");
	return 0;
#else
	pid_t pid = fork();
	if (pid < 0) {
		fprintf(stderr, "test_gateway_http: fork failed\n");
		return 1;
	}
	if (pid == 0) {
		const char *config_path = "/tmp/shellclaw_test_gw_config.toml";
		FILE *f = fopen(config_path, "w");
		if (f) {
			fprintf(f, "[agent]\nmodel = \"test\"\n[gateway]\nenabled = true\nhost = \"127.0.0.1\"\nport = 18789\n");
			fclose(f);
		}
		execl("./build/shellclaw", "shellclaw", "--config", config_path, (char *)NULL);
		_exit(1);
	}
	sleep(3);
	int failed = 0;
	if (test_health() != 0) { fprintf(stderr, "test_health failed\n"); failed++; }
	if (test_pair() != 0) { fprintf(stderr, "test_pair failed\n"); failed++; }
	if (test_api_config_401() != 0) { fprintf(stderr, "test_api_config_401 failed\n"); failed++; }
	kill(pid, SIGTERM);
	waitpid(pid, NULL, 0);
	if (failed == 0)
		printf("test_gateway_http: all tests passed\n");
	return failed;
#endif
}
