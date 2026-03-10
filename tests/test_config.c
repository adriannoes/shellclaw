/**
 * @file test_config.c
 * @brief Unit tests for config_load: TOML parsing, env overrides, validation.
 */
#define _POSIX_C_SOURCE 200809L

#include "src/core/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
#define RUN(t) do { int r = (t); if (r) return r; } while (0)

static int test_load_valid_minimal(void)
{
	const char *path = "/tmp/shellclaw_test_config_valid.toml";
	FILE *f = fopen(path, "w");
	ASSERT(f);
	fprintf(f, "[agent]\nmodel = \"claude-sonnet\"\nmax_tool_iterations = 10\n");
	fclose(f);
	config_t *cfg = NULL;
	char errbuf[256];
	int ret = config_load(path, &cfg, errbuf, sizeof(errbuf));
	ASSERT(ret == 0);
	ASSERT(cfg != NULL);
	ASSERT(config_agent_model(cfg) != NULL);
	ASSERT(strcmp(config_agent_model(cfg), "claude-sonnet") == 0);
	ASSERT(config_agent_max_tool_iterations(cfg) == 10);
	config_free(cfg);
	remove(path);
	return 0;
}

static int test_load_missing_agent_section(void)
{
	const char *path = "/tmp/shellclaw_test_config_no_agent.toml";
	FILE *f = fopen(path, "w");
	ASSERT(f);
	fprintf(f, "[memory]\ndb_path = \"/tmp/db\"\n");
	fclose(f);
	config_t *cfg = NULL;
	char errbuf[256];
	int ret = config_load(path, &cfg, errbuf, sizeof(errbuf));
	ASSERT(ret != 0);
	ASSERT(cfg == NULL);
	ASSERT(strstr(errbuf, "agent") != NULL);
	remove(path);
	return 0;
}

static int test_load_missing_required_model(void)
{
	const char *path = "/tmp/shellclaw_test_config_no_model.toml";
	FILE *f = fopen(path, "w");
	ASSERT(f);
	fprintf(f, "[agent]\nmax_tokens = 2048\n");
	fclose(f);
	config_t *cfg = NULL;
	char errbuf[256];
	int ret = config_load(path, &cfg, errbuf, sizeof(errbuf));
	ASSERT(ret != 0);
	ASSERT(cfg == NULL);
	ASSERT(strstr(errbuf, "model") != NULL);
	remove(path);
	return 0;
}

static int test_env_override(void)
{
	const char *path = "/tmp/shellclaw_test_config_env.toml";
	FILE *f = fopen(path, "w");
	ASSERT(f);
	fprintf(f, "[agent]\nmodel = \"from-file\"\n");
	fclose(f);
	setenv("SHELLCLAW_AGENT_MODEL", "from-env", 1);
	config_t *cfg = NULL;
	char errbuf[256];
	int ret = config_load(path, &cfg, errbuf, sizeof(errbuf));
	unsetenv("SHELLCLAW_AGENT_MODEL");
	ASSERT(ret == 0);
	ASSERT(cfg != NULL);
	ASSERT(strcmp(config_agent_model(cfg), "from-env") == 0);
	config_free(cfg);
	remove(path);
	return 0;
}

static int test_defaults(void)
{
	const char *path = "/tmp/shellclaw_test_config_defaults.toml";
	FILE *f = fopen(path, "w");
	ASSERT(f);
	fprintf(f, "[agent]\nmodel = \"default-model\"\n");
	fclose(f);
	config_t *cfg = NULL;
	int ret = config_load(path, &cfg, NULL, 0);
	ASSERT(ret == 0);
	ASSERT(config_agent_max_tool_iterations(cfg) == 20);
	ASSERT(config_agent_max_context_messages(cfg) == 40);
	config_free(cfg);
	remove(path);
	return 0;
}

static int test_gateway_section(void)
{
	const char *path = "/tmp/shellclaw_test_config_gateway.toml";
	FILE *f = fopen(path, "w");
	ASSERT(f);
	fprintf(f, "[agent]\nmodel = \"test\"\n");
	fprintf(f, "[gateway]\nenabled = true\nhost = \"0.0.0.0\"\nport = 18789\nallow_bind_all = true\n");
	fclose(f);
	config_t *cfg = NULL;
	char errbuf[256];
	int ret = config_load(path, &cfg, errbuf, sizeof(errbuf));
	ASSERT(ret == 0);
	ASSERT(cfg != NULL);
	ASSERT(config_gateway_enabled(cfg) == 1);
	ASSERT(config_gateway_host(cfg) != NULL);
	ASSERT(strcmp(config_gateway_host(cfg), "0.0.0.0") == 0);
	ASSERT(config_gateway_port(cfg) == 18789);
	ASSERT(config_gateway_allow_bind_all(cfg) == 1);
	config_free(cfg);
	remove(path);
	return 0;
}

static int test_gateway_defaults(void)
{
	const char *path = "/tmp/shellclaw_test_config_gateway_defaults.toml";
	FILE *f = fopen(path, "w");
	ASSERT(f);
	fprintf(f, "[agent]\nmodel = \"test\"\n");
	fclose(f);
	config_t *cfg = NULL;
	int ret = config_load(path, &cfg, NULL, 0);
	ASSERT(ret == 0);
	ASSERT(config_gateway_enabled(cfg) == 0);
	ASSERT(config_gateway_host(cfg) != NULL);
	ASSERT(strcmp(config_gateway_host(cfg), "127.0.0.1") == 0);
	ASSERT(config_gateway_port(cfg) == 18789);
	ASSERT(config_gateway_allow_bind_all(cfg) == 0);
	config_free(cfg);
	remove(path);
	return 0;
}

int main(void)
{
	RUN(test_load_valid_minimal());
	RUN(test_load_missing_agent_section());
	RUN(test_load_missing_required_model());
	RUN(test_env_override());
	RUN(test_defaults());
	RUN(test_gateway_section());
	RUN(test_gateway_defaults());
	printf("test_config: all tests passed\n");
	return 0;
}
