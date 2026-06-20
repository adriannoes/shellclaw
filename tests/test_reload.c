/**
 * @file test_reload.c
 * @brief Unit tests for SIGHUP reload queue and config swap.
 */
#define _POSIX_C_SOURCE 200809L

#include "core/reload.h"
#include "core/bootstrap.h"
#include "channels/channel.h"
#include "channels/heartbeat.h"
#include "providers/provider.h"
#include "tools/tool.h"
#include "tests/test_runner.h"
#include <stdio.h>
#include <string.h>

/* Stubs for try_config_reload live-pointer propagation (no full subsystem link). */
static const config_t *g_stub_live_cfg;
static config_t *g_stub_bootstrap_cfg;
static const char *g_stub_config_path;

void bootstrap_set_config_path(const char *path)
{
	g_stub_config_path = path;
}

const char *bootstrap_get_config_path(void)
{
	return g_stub_config_path;
}

void bootstrap_set_cfg(config_t *cfg)
{
	g_stub_bootstrap_cfg = cfg;
}

void provider_router_set_live_config(const config_t *cfg)
{
	g_stub_live_cfg = cfg;
}

void provider_openai_set_live_config(const config_t *cfg)
{
	g_stub_live_cfg = cfg;
}

void provider_local_set_live_config(const config_t *cfg)
{
	g_stub_live_cfg = cfg;
}

void tool_set_config(const config_t *cfg)
{
	g_stub_live_cfg = cfg;
}

void heartbeat_set_live_config(const config_t *cfg)
{
	g_stub_live_cfg = cfg;
}

void shellclaw_telegram_set_live_cfg(const config_t *cfg)
{
	g_stub_live_cfg = cfg;
}

void shellclaw_discord_set_live_cfg(const config_t *cfg)
{
	g_stub_live_cfg = cfg;
}

static int test_on_hup_sets_reload_flag(void)
{
	g_reload_requested = 0;
	on_hup(SIGHUP);
	ASSERT(g_reload_requested == 1);
	g_reload_requested = 0;
	return 0;
}

static int test_stale_enqueue_null_is_noop(void)
{
	stale_free_all();
	ASSERT(stale_enqueue(NULL) == 0);
	stale_free_all();
	return 0;
}

static int test_stale_enqueue_preserves_independent_configs(void)
{
	char path[128];
	FILE *f;
	config_t *stale_a = NULL;
	config_t *stale_b = NULL;
	config_t *live = NULL;
	char errbuf[256];

	ASSERT(test_runner_mkstemp_path("shellclaw_test_reload", path, sizeof(path)) == 0);
	f = fopen(path, "w");
	ASSERT(f);
	fprintf(f, "[agent]\nmodel = \"stale-a\"\nmax_tokens = 100\n");
	fclose(f);
	ASSERT(config_load(path, &stale_a, errbuf, sizeof(errbuf)) == 0);

	f = fopen(path, "w");
	ASSERT(f);
	fprintf(f, "[agent]\nmodel = \"stale-b\"\nmax_tokens = 200\n");
	fclose(f);
	ASSERT(config_load(path, &stale_b, errbuf, sizeof(errbuf)) == 0);

	f = fopen(path, "w");
	ASSERT(f);
	fprintf(f, "[agent]\nmodel = \"live-model\"\nmax_tokens = 300\n");
	fclose(f);
	ASSERT(config_load(path, &live, errbuf, sizeof(errbuf)) == 0);

	stale_free_all();
	ASSERT(stale_enqueue(stale_a) == 0);
	ASSERT(stale_enqueue(stale_b) == 0);
	ASSERT(strcmp(config_agent_model(stale_a), "stale-a") == 0);
	ASSERT(strcmp(config_agent_model(stale_b), "stale-b") == 0);
	ASSERT(strcmp(config_agent_model(live), "live-model") == 0);

	stale_free_all();
	config_free(live);
	remove(path);
	return 0;
}

static int test_try_config_reload_swaps_live_config(void)
{
	char path[128];
	FILE *f;
	config_t *live = NULL;
	char errbuf[256];

	ASSERT(test_runner_mkstemp_path("shellclaw_test_reload", path, sizeof(path)) == 0);
	f = fopen(path, "w");
	ASSERT(f);
	fprintf(f, "[agent]\nmodel = \"before-reload\"\nmax_tokens = 100\n");
	fclose(f);
	ASSERT(config_load(path, &live, errbuf, sizeof(errbuf)) == 0);

	g_stub_config_path = path;
	g_stub_live_cfg = NULL;
	g_stub_bootstrap_cfg = NULL;
	bootstrap_set_config_path(path);

	f = fopen(path, "w");
	ASSERT(f);
	fprintf(f, "[agent]\nmodel = \"after-reload\"\nmax_tokens = 200\n");
	fclose(f);

	try_config_reload(&live);

	ASSERT(live != NULL);
	ASSERT(strcmp(config_agent_model(live), "after-reload") == 0);
	ASSERT(config_agent_max_tokens(live) == 200);
	ASSERT(g_stub_live_cfg == live);
	ASSERT(g_stub_bootstrap_cfg == live);

	stale_free_all();
	config_free(live);
	g_stub_config_path = NULL;
	remove(path);
	return 0;
}

static int test_try_config_reload_keeps_config_on_invalid_toml(void)
{
	char path[128];
	FILE *f;
	config_t *live = NULL;
	char errbuf[256];

	ASSERT(test_runner_mkstemp_path("shellclaw_test_reload", path, sizeof(path)) == 0);
	f = fopen(path, "w");
	ASSERT(f);
	fprintf(f, "[agent]\nmodel = \"stable\"\nmax_tokens = 100\n");
	fclose(f);
	ASSERT(config_load(path, &live, errbuf, sizeof(errbuf)) == 0);

	g_stub_config_path = path;
	bootstrap_set_config_path(path);

	f = fopen(path, "w");
	ASSERT(f);
	fprintf(f, "not valid toml [[[\n");
	fclose(f);

	try_config_reload(&live);

	ASSERT(live != NULL);
	ASSERT(strcmp(config_agent_model(live), "stable") == 0);
	ASSERT(config_agent_max_tokens(live) == 100);

	stale_free_all();
	config_free(live);
	g_stub_config_path = NULL;
	remove(path);
	return 0;
}

int main(void)
{
	RUN(test_on_hup_sets_reload_flag());
	RUN(test_stale_enqueue_null_is_noop());
	RUN(test_stale_enqueue_preserves_independent_configs());
	RUN(test_try_config_reload_swaps_live_config());
	RUN(test_try_config_reload_keeps_config_on_invalid_toml());
	printf("test_reload: all tests passed\n");
	return 0;
}
