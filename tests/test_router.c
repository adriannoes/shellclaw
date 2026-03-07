/**
 * @file test_router.c
 * @brief Unit tests for provider_router_get: config default_provider -> anthropic/openai/NULL.
 */

#include "core/config.h"
#include "providers/provider.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
#define RUN(t) do { int r = (t); if (r) return r; } while (0)

static int write_config(const char *path, const char *provider_default)
{
	FILE *f = fopen(path, "w");
	if (!f) return -1;
	fprintf(f, "[agent]\nmodel = \"test-model\"\n");
	if (provider_default)
		fprintf(f, "[providers]\ndefault = \"%s\"\n", provider_default);
	fclose(f);
	return 0;
}

static int test_router_anthropic(void)
{
	const char *path = "/tmp/shellclaw_test_router_anthropic.toml";
	ASSERT(write_config(path, "anthropic") == 0);
	config_t *cfg = NULL;
	char errbuf[256];
	ASSERT(config_load(path, &cfg, errbuf, sizeof(errbuf)) == 0);
	ASSERT(cfg != NULL);
	const provider_t *p = provider_router_get(cfg);
	ASSERT(p != NULL);
	ASSERT(p->name != NULL);
	ASSERT(strcmp(p->name, "anthropic") == 0);
	config_free(cfg);
	remove(path);
	return 0;
}

static int test_router_openai(void)
{
	const char *path = "/tmp/shellclaw_test_router_openai.toml";
	ASSERT(write_config(path, "openai") == 0);
	config_t *cfg = NULL;
	char errbuf[256];
	ASSERT(config_load(path, &cfg, errbuf, sizeof(errbuf)) == 0);
	ASSERT(cfg != NULL);
	const provider_t *p = provider_router_get(cfg);
	ASSERT(p != NULL);
	ASSERT(p->name != NULL);
	ASSERT(strcmp(p->name, "openai") == 0);
	config_free(cfg);
	remove(path);
	return 0;
}

static int test_router_case_insensitive(void)
{
	const char *path = "/tmp/shellclaw_test_router_case.toml";
	ASSERT(write_config(path, "OPENAI") == 0);
	config_t *cfg = NULL;
	char errbuf[256];
	ASSERT(config_load(path, &cfg, errbuf, sizeof(errbuf)) == 0);
	const provider_t *p = provider_router_get(cfg);
	ASSERT(p != NULL);
	ASSERT(strcmp(p->name, "openai") == 0);
	config_free(cfg);
	remove(path);
	return 0;
}

static int test_router_invalid_returns_null(void)
{
	const char *path = "/tmp/shellclaw_test_router_invalid.toml";
	ASSERT(write_config(path, "invalid_provider") == 0);
	config_t *cfg = NULL;
	char errbuf[256];
	ASSERT(config_load(path, &cfg, errbuf, sizeof(errbuf)) == 0);
	const provider_t *p = provider_router_get(cfg);
	ASSERT(p == NULL);
	config_free(cfg);
	remove(path);
	return 0;
}

static int test_router_null_config_returns_null(void)
{
	ASSERT(provider_router_get(NULL) == NULL);
	return 0;
}

static int test_default_provider_is_anthropic(void)
{
	const char *path = "/tmp/shellclaw_test_router_default.toml";
	FILE *f = fopen(path, "w");
	ASSERT(f);
	fprintf(f, "[agent]\nmodel = \"x\"\n");
	fclose(f);
	config_t *cfg = NULL;
	char errbuf[256];
	ASSERT(config_load(path, &cfg, errbuf, sizeof(errbuf)) == 0);
	ASSERT(config_default_provider(cfg) != NULL);
	ASSERT(strcmp(config_default_provider(cfg), "anthropic") == 0);
	const provider_t *p = provider_router_get(cfg);
	ASSERT(p != NULL);
	ASSERT(strcmp(p->name, "anthropic") == 0);
	config_free(cfg);
	remove(path);
	return 0;
}

int main(void)
{
	RUN(test_router_null_config_returns_null());
	RUN(test_router_anthropic());
	RUN(test_router_openai());
	RUN(test_router_case_insensitive());
	RUN(test_router_invalid_returns_null());
	RUN(test_default_provider_is_anthropic());
	printf("test_router: all tests passed\n");
	return 0;
}
