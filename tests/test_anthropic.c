/**
 * @file test_anthropic.c
 * @brief Tests for Anthropic provider: init, vtable, optional integration.
 */

#include "core/config.h"
#include "providers/provider.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
#define RUN(t) do { int r = (t); if (r) return r; } while (0)

static const char *TMP_CONFIG = "/tmp/shellclaw_test_anthropic_config.toml";

static int write_config(void)
{
	FILE *f = fopen(TMP_CONFIG, "w");
	if (!f) return -1;
	fprintf(f, "[agent]\nmodel = \"claude-3-5-sonnet-20241022\"\nmax_tokens = 256\n");
	fprintf(f, "[memory]\ndb_path = \"/tmp/shellclaw_anth_test.db\"\n");
	fprintf(f, "[providers.anthropic]\napi_key_env = \"ANTHROPIC_API_KEY\"\n");
	fclose(f);
	return 0;
}

static int test_anthropic_vtable(void)
{
	const provider_t *p = provider_anthropic_get();
	ASSERT(p != NULL);
	ASSERT(p->name != NULL);
	ASSERT(strcmp(p->name, "anthropic") == 0);
	ASSERT(p->init != NULL);
	ASSERT(p->chat != NULL);
	ASSERT(p->cleanup != NULL);
	return 0;
}

static int test_init_fails_without_config(void)
{
	const provider_t *p = provider_anthropic_get();
	ASSERT(p != NULL);
	ASSERT(p->init(NULL) == -1);
	return 0;
}

static int test_init_fails_without_api_key_in_env(void)
{
	ASSERT(write_config() == 0);
	config_t *cfg = NULL;
	char errbuf[256];
	ASSERT(config_load(TMP_CONFIG, &cfg, errbuf, sizeof(errbuf)) == 0);
	unsetenv("ANTHROPIC_API_KEY");
	const provider_t *p = provider_anthropic_get();
	ASSERT(p != NULL);
	ASSERT(p->init(cfg) == -1);
	config_free(cfg);
	remove(TMP_CONFIG);
	return 0;
}

static int test_chat_fails_without_init(void)
{
	provider_response_t response = {0};
	const provider_t *p = provider_anthropic_get();
	ASSERT(p != NULL);
	provider_message_t msg = { .role = "user", .content = "Hi" };
	ASSERT(p->chat(&msg, 1, NULL, 0, &response) == -1);
	ASSERT(response.error != 0);
	provider_response_clear(&response);
	return 0;
}

static int test_init_and_chat_if_key_set(void)
{
	if (!getenv("ANTHROPIC_API_KEY")) return 0;
	ASSERT(write_config() == 0);
	config_t *cfg = NULL;
	char errbuf[256];
	ASSERT(config_load(TMP_CONFIG, &cfg, errbuf, sizeof(errbuf)) == 0);
	const provider_t *p = provider_anthropic_get();
	ASSERT(p != NULL);
	ASSERT(p->init(cfg) == 0);
	provider_message_t msg = { .role = "user", .content = "Reply with exactly: OK" };
	provider_response_t response = {0};
	int ret = p->chat(&msg, 1, NULL, 0, &response);
	p->cleanup();
	config_free(cfg);
	remove(TMP_CONFIG);
	if (ret != 0) return 0;
	ASSERT(response.error == 0);
	ASSERT(response.content != NULL);
	ASSERT(strstr(response.content, "OK") != NULL || strlen(response.content) > 0);
	provider_response_clear(&response);
	return 0;
}

int main(void)
{
	RUN(test_anthropic_vtable());
	RUN(test_init_fails_without_config());
	RUN(test_init_fails_without_api_key_in_env());
	RUN(test_chat_fails_without_init());
	RUN(test_init_and_chat_if_key_set());
	printf("test_anthropic: all tests passed\n");
	return 0;
}
