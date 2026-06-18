/**
 * @file test_reload.c
 * @brief Unit tests for SIGHUP reload queue and config swap.
 */
#define _POSIX_C_SOURCE 200809L

#include "core/reload.h"
#include "tests/test_runner.h"
#include <stdio.h>
#include <string.h>

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

int main(void)
{
	RUN(test_on_hup_sets_reload_flag());
	RUN(test_stale_enqueue_null_is_noop());
	RUN(test_stale_enqueue_preserves_independent_configs());
	printf("test_reload: all tests passed\n");
	return 0;
}
