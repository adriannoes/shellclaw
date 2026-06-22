/**
 * @file test_router.c
 * @brief Composite router, fallback chain chat, error classification, and recovery throttle.
 */

#include "test_runner.h"
#include "core/config.h"
#include "providers/provider.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_status_cb_count;

static void test_status_cb(void)
{
	g_status_cb_count++;
}

/** Legacy helper: `[providers]` default is ignored by composite routing — only included for file shape. */
static int write_config_with_default(const char *path, const char *provider_default)
{
	FILE *f = fopen(path, "w");
	if (!f) return -1;
	fprintf(f, "[agent]\nmodel = \"test-model\"\n");
	if (provider_default)
		fprintf(f, "[providers]\ndefault = \"%s\"\n", provider_default);
	fclose(f);
	return 0;
}

static int write_stub_chain_config(const char *path, const char *chain_csv)
{
	FILE *f = fopen(path, "w");
	if (!f) return -1;
	fprintf(f, "[agent]\nmodel = \"x\"\n");
	fprintf(f, "[providers]\nfallback_chain = [ %s ]\n", chain_csv);
	fclose(f);
	return 0;
}

static int test_router_always_composite_named(void)
{
	char path[128];
	config_t *cfg = NULL;
	char errbuf[256];
	const provider_t *p;
	ASSERT(test_runner_mkstemp_path("shellclaw_test_router", path, sizeof(path)) == 0);
	ASSERT(write_config_with_default(path, "openai") == 0);
	ASSERT(config_load(path, &cfg, errbuf, sizeof(errbuf)) == 0);
	p = provider_router_get(cfg);
	ASSERT(p != NULL);
	ASSERT(strcmp(p->name, "shellclaw-router") == 0);
	config_free(cfg);
	remove(path);
	return 0;
}

static int test_router_case_insensitive_legacy_default_unused(void)
{
	char path[128];
	config_t *cfg = NULL;
	char errbuf[256];
	const provider_t *p;
	ASSERT(test_runner_mkstemp_path("shellclaw_test_router", path, sizeof(path)) == 0);
	ASSERT(write_config_with_default(path, "OPENAI") == 0);
	ASSERT(config_load(path, &cfg, errbuf, sizeof(errbuf)) == 0);
	p = provider_router_get(cfg);
	ASSERT(p != NULL);
	ASSERT(strcmp(p->name, "shellclaw-router") == 0);
	config_free(cfg);
	remove(path);
	return 0;
}

static int test_router_null_config_returns_null(void)
{
	ASSERT(provider_router_get(NULL) == NULL);
	return 0;
}

static int test_default_provider_key_still_reads_anthropic(void)
{
	char path[128];
	FILE *f;
	config_t *cfg = NULL;
	char errbuf[256];
	ASSERT(test_runner_mkstemp_path("shellclaw_test_router", path, sizeof(path)) == 0);
	f = fopen(path, "w");
	ASSERT(f);
	fprintf(f, "[agent]\nmodel = \"x\"\n");
	fclose(f);
	ASSERT(config_load(path, &cfg, errbuf, sizeof(errbuf)) == 0);
	ASSERT(config_default_provider(cfg) != NULL);
	ASSERT(strcmp(config_default_provider(cfg), "anthropic") == 0);
	ASSERT(provider_router_get(cfg) != NULL);
	ASSERT(strcmp(provider_router_get(cfg)->name, "shellclaw-router") == 0);
	config_free(cfg);
	remove(path);
	return 0;
}

static int test_fallback_chain_stub_init_smoke(void)
{
	char path[128];
	config_t *cfg = NULL;
	char errbuf[256];
	const provider_t *p;
	ASSERT(test_runner_mkstemp_path("shellclaw_test_router", path, sizeof(path)) == 0);
	ASSERT(write_stub_chain_config(path, "\"stub\"") == 0);
	ASSERT(config_load(path, &cfg, errbuf, sizeof(errbuf)) == 0);
	p = provider_router_get(cfg);
	ASSERT(p != NULL);
	ASSERT(p->init(cfg) == 0);
	p->cleanup();
	config_free(cfg);
	remove(path);
	return 0;
}

static int test_api_status_json_after_stub_init(void)
{
	char path[128];
	config_t *cfg = NULL;
	char errbuf[256];
	const provider_t *p;
	char *json;
	cJSON *root;
	cJSON *arr;
	ASSERT(test_runner_mkstemp_path("shellclaw_test_router", path, sizeof(path)) == 0);
	ASSERT(write_stub_chain_config(path, "\"stub\"") == 0);
	ASSERT(config_load(path, &cfg, errbuf, sizeof(errbuf)) == 0);
	p = provider_router_get(cfg);
	ASSERT(p != NULL);
	ASSERT(p->init(cfg) == 0);
	json = provider_router_api_status_json();
	ASSERT(json != NULL);
	ASSERT(strstr(json, "\"active_provider\"") != NULL);
	ASSERT(strstr(json, "\"stub\"") != NULL);
	root = cJSON_Parse(json);
	ASSERT(root != NULL);
	arr = cJSON_GetObjectItem(root, "providers");
	ASSERT(arr != NULL && cJSON_IsArray(arr));
	ASSERT(cJSON_GetArraySize(arr) == 1);
	cJSON_Delete(root);
	free(json);
	p->cleanup();
	config_free(cfg);
	remove(path);
	return 0;
}

static int test_fallback_chain_stub_b_to_stub_chat(void)
{
	char path[128];
	config_t *cfg = NULL;
	char errbuf[256];
	const provider_t *router;
	provider_message_t msg;
	provider_response_t resp;
	char snap[64];
	ASSERT(test_runner_mkstemp_path("shellclaw_test_router", path, sizeof(path)) == 0);
	ASSERT(write_stub_chain_config(path, "\"stub-b\", \"stub\"") == 0);
	ASSERT(config_load(path, &cfg, errbuf, sizeof(errbuf)) == 0);
	router = provider_router_get(cfg);
	ASSERT(router != NULL);
	ASSERT(router->init(cfg) == 0);
	provider_stub_b_set_chat_should_fail(1);
	memset(&msg, 0, sizeof(msg));
	msg.role = "user";
	msg.content = "hi";
	memset(&resp, 0, sizeof(resp));
	ASSERT(router->chat(&msg, 1, NULL, 0, &resp) == 0);
	ASSERT(resp.error == 0);
	provider_response_clear(&resp);
	provider_router_active_backend_snapshot(snap, sizeof(snap));
	ASSERT(strcmp(snap, "stub") == 0);
	provider_stub_b_set_chat_should_fail(0);
	router->cleanup();
	config_free(cfg);
	remove(path);
	return 0;
}

static int test_api_status_json_reflects_fallback_tier(void)
{
	char path[128];
	config_t *cfg = NULL;
	char errbuf[256];
	const provider_t *router;
	provider_message_t msg;
	provider_response_t resp;
	char *json;
	cJSON *root;
	cJSON *providers;
	cJSON *stub_b;
	cJSON *stub;
	cJSON *field;
	ASSERT(test_runner_mkstemp_path("shellclaw_test_router", path, sizeof(path)) == 0);
	ASSERT(write_stub_chain_config(path, "\"stub-b\", \"stub\"") == 0);
	ASSERT(config_load(path, &cfg, errbuf, sizeof(errbuf)) == 0);
	router = provider_router_get(cfg);
	ASSERT(router != NULL);
	ASSERT(router->init(cfg) == 0);
	provider_stub_b_set_chat_should_fail(1);
	memset(&msg, 0, sizeof(msg));
	msg.role = "user";
	msg.content = "hi";
	memset(&resp, 0, sizeof(resp));
	ASSERT(router->chat(&msg, 1, NULL, 0, &resp) == 0);
	ASSERT(resp.error == 0);
	provider_response_clear(&resp);
	json = provider_router_api_status_json();
	ASSERT(json != NULL);
	root = cJSON_Parse(json);
	ASSERT(root != NULL);
	field = cJSON_GetObjectItem(root, "active_provider");
	ASSERT(field != NULL && cJSON_IsString(field));
	ASSERT(strcmp(field->valuestring, "stub") == 0);
	providers = cJSON_GetObjectItem(root, "providers");
	ASSERT(providers != NULL && cJSON_IsArray(providers));
	ASSERT(cJSON_GetArraySize(providers) == 2);
	stub_b = cJSON_GetArrayItem(providers, 0);
	stub = cJSON_GetArrayItem(providers, 1);
	ASSERT(stub_b != NULL && stub != NULL);
	field = cJSON_GetObjectItem(stub_b, "name");
	ASSERT(field != NULL && cJSON_IsString(field));
	ASSERT(strcmp(field->valuestring, "stub-b") == 0);
	field = cJSON_GetObjectItem(stub_b, "role");
	ASSERT(field != NULL && cJSON_IsString(field));
	ASSERT(strcmp(field->valuestring, "unavailable") == 0);
	field = cJSON_GetObjectItem(stub_b, "reachable");
	ASSERT(field != NULL && cJSON_IsFalse(field));
	field = cJSON_GetObjectItem(stub, "name");
	ASSERT(field != NULL && cJSON_IsString(field));
	ASSERT(strcmp(field->valuestring, "stub") == 0);
	field = cJSON_GetObjectItem(stub, "role");
	ASSERT(field != NULL && cJSON_IsString(field));
	ASSERT(strcmp(field->valuestring, "fallback") == 0);
	field = cJSON_GetObjectItem(stub, "reachable");
	ASSERT(field != NULL && cJSON_IsTrue(field));
	cJSON_Delete(root);
	free(json);
	provider_stub_b_set_chat_should_fail(0);
	router->cleanup();
	config_free(cfg);
	remove(path);
	return 0;
}

static int test_error_401_terminal(void)
{
	ASSERT(provider_error_allows_fallback_retry("OpenAI API HTTP 401") == 0);
	ASSERT(provider_error_allows_fallback_retry("Anthropic API HTTP 429") == 0);
	return 0;
}

static int test_error_503_retries(void)
{
	ASSERT(provider_error_allows_fallback_retry("OpenAI API HTTP 503") != 0);
	ASSERT(provider_error_allows_fallback_retry("Local provider HTTP 500") != 0);
	return 0;
}

static int test_error_transport_retries(void)
{
	ASSERT(provider_error_allows_fallback_retry("Connection timed out after 30003 milliseconds") != 0);
	ASSERT(provider_error_allows_fallback_retry("Could not resolve host: api.example.invalid") != 0);
	return 0;
}

static int test_error_empty_retries(void)
{
	ASSERT(provider_error_allows_fallback_retry(NULL) != 0);
	ASSERT(provider_error_allows_fallback_retry("") != 0);
	return 0;
}

static int test_recovery_restores_primary_stub(void)
{
	char path[128];
	config_t *cfg = NULL;
	char errbuf[256];
	const provider_t *router;
	provider_message_t msg;
	provider_response_t resp;
	char snap[64];
	time_t t0 = 1000000;
	ASSERT(test_runner_mkstemp_path("shellclaw_test_router", path, sizeof(path)) == 0);
	ASSERT(write_stub_chain_config(path, "\"stub-b\", \"stub\"") == 0);
	ASSERT(config_load(path, &cfg, errbuf, sizeof(errbuf)) == 0);
	router = provider_router_get(cfg);
	ASSERT(router != NULL);
	ASSERT(router->init(cfg) == 0);
	provider_stub_b_set_chat_should_fail(1);
	memset(&msg, 0, sizeof(msg));
	msg.role = "user";
	msg.content = "hi";
	memset(&resp, 0, sizeof(resp));
	ASSERT(router->chat(&msg, 1, NULL, 0, &resp) == 0);
	provider_response_clear(&resp);
	provider_router_active_backend_snapshot(snap, sizeof(snap));
	ASSERT(strcmp(snap, "stub") == 0);
	provider_stub_b_set_chat_should_fail(0);
	provider_router_periodic_recovery_set_interval_seconds(30);
	provider_router_periodic_recovery_reset_timer();
	provider_router_periodic_recovery_tick(t0);
	provider_router_active_backend_snapshot(snap, sizeof(snap));
	ASSERT(strcmp(snap, "stub-b") == 0);
	provider_router_periodic_recovery_set_interval_seconds(0);
	router->cleanup();
	config_free(cfg);
	remove(path);
	return 0;
}

static int test_recovery_throttled_before_interval(void)
{
	char path[128];
	config_t *cfg = NULL;
	char errbuf[256];
	const provider_t *router;
	provider_message_t msg;
	provider_response_t resp;
	char snap[64];
	time_t t0 = 5000000;
	ASSERT(test_runner_mkstemp_path("shellclaw_test_router", path, sizeof(path)) == 0);
	ASSERT(write_stub_chain_config(path, "\"stub-b\", \"stub\"") == 0);
	ASSERT(config_load(path, &cfg, errbuf, sizeof(errbuf)) == 0);
	router = provider_router_get(cfg);
	ASSERT(router != NULL);
	ASSERT(router->init(cfg) == 0);
	provider_stub_b_set_chat_should_fail(1);
	memset(&msg, 0, sizeof(msg));
	msg.role = "user";
	msg.content = "hi";
	memset(&resp, 0, sizeof(resp));
	ASSERT(router->chat(&msg, 1, NULL, 0, &resp) == 0);
	provider_response_clear(&resp);
	provider_stub_b_set_chat_should_fail(0);
	provider_router_periodic_recovery_set_interval_seconds(300);
	provider_router_periodic_recovery_reset_timer();
	provider_router_periodic_recovery_tick(t0);
	provider_router_active_backend_snapshot(snap, sizeof(snap));
	ASSERT(strcmp(snap, "stub-b") == 0);
	provider_router_periodic_recovery_tick(t0 + 60);
	provider_router_active_backend_snapshot(snap, sizeof(snap));
	ASSERT(strcmp(snap, "stub-b") == 0);
	provider_router_periodic_recovery_tick(t0 + 301);
	provider_router_active_backend_snapshot(snap, sizeof(snap));
	ASSERT(strcmp(snap, "stub-b") == 0);
	provider_router_periodic_recovery_set_interval_seconds(0);
	router->cleanup();
	config_free(cfg);
	remove(path);
	return 0;
}

static int test_status_changed_callback_on_recovery(void)
{
	char path[128];
	config_t *cfg = NULL;
	char errbuf[256];
	const provider_t *router;
	provider_message_t msg;
	provider_response_t resp;
	time_t t0 = 3000000;
	ASSERT(test_runner_mkstemp_path("shellclaw_test_router", path, sizeof(path)) == 0);
	ASSERT(write_stub_chain_config(path, "\"stub-b\", \"stub\"") == 0);
	ASSERT(config_load(path, &cfg, errbuf, sizeof(errbuf)) == 0);
	router = provider_router_get(cfg);
	ASSERT(router != NULL);
	ASSERT(router->init(cfg) == 0);
	provider_router_set_status_changed_callback(test_status_cb);
	g_status_cb_count = 0;
	provider_stub_b_set_chat_should_fail(1);
	memset(&msg, 0, sizeof(msg));
	msg.role = "user";
	msg.content = "hi";
	memset(&resp, 0, sizeof(resp));
	ASSERT(router->chat(&msg, 1, NULL, 0, &resp) == 0);
	provider_response_clear(&resp);
	ASSERT(g_status_cb_count == 1);
	provider_stub_b_set_chat_should_fail(0);
	provider_router_periodic_recovery_set_interval_seconds(30);
	provider_router_periodic_recovery_reset_timer();
	provider_router_periodic_recovery_tick(t0);
	ASSERT(g_status_cb_count == 2);
	provider_router_periodic_recovery_set_interval_seconds(0);
	provider_router_set_status_changed_callback(NULL);
	router->cleanup();
	config_free(cfg);
	remove(path);
	return 0;
}

int main(void)
{
	RUN(test_router_null_config_returns_null());
	RUN(test_router_always_composite_named());
	RUN(test_router_case_insensitive_legacy_default_unused());
	RUN(test_default_provider_key_still_reads_anthropic());
	RUN(test_fallback_chain_stub_init_smoke());
	RUN(test_api_status_json_after_stub_init());
	RUN(test_fallback_chain_stub_b_to_stub_chat());
	RUN(test_api_status_json_reflects_fallback_tier());
	RUN(test_error_401_terminal());
	RUN(test_error_503_retries());
	RUN(test_error_transport_retries());
	RUN(test_error_empty_retries());
	RUN(test_recovery_restores_primary_stub());
	RUN(test_recovery_throttled_before_interval());
	RUN(test_status_changed_callback_on_recovery());
	printf("test_router: all tests passed\n");
	return 0;
}
