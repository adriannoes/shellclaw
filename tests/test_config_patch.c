/**
 * @file test_config_patch.c
 * @brief Unit tests for dashboard JSON config patching.
 */

#include "test_runner.h"
#include "src/core/config.h"
#include "src/core/config_patch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_patch_model_and_temperature(void)
{
	char path[128];
	FILE *f;
	char *patched = NULL;
	size_t patched_len = 0;
	char errbuf[256];
	config_t *cfg = NULL;

	ASSERT(test_runner_mkstemp_path("shellclaw_test_config_patch", path, sizeof(path)) == 0);
	f = fopen(path, "w");
	ASSERT(f);
	fprintf(f,
	        "[agent]\nmodel = \"old-model\"\nmax_tokens = 1024\ntemperature = 0.2\n"
	        "[gateway]\nhost = \"127.0.0.1\"\nport = 18789\n");
	fclose(f);

	ASSERT(config_patch_dashboard_json(
	           path, "{\"model\":\"new-model\",\"temperature\":0.9}", &patched, &patched_len, errbuf,
	           sizeof(errbuf)) == 0);
	ASSERT(patched != NULL);
	ASSERT(strstr(patched, "model = \"new-model\"") != NULL);
	ASSERT(strstr(patched, "temperature = 0.9") != NULL);
	ASSERT(strstr(patched, "max_tokens = 1024") != NULL);

	f = fopen(path, "w");
	ASSERT(f);
	fwrite(patched, 1, patched_len, f);
	fclose(f);
	free(patched);

	ASSERT(config_load(path, &cfg, errbuf, sizeof(errbuf)) == 0);
	ASSERT(strcmp(config_agent_model(cfg), "new-model") == 0);
	ASSERT(config_agent_temperature(cfg) == 0.9);
	ASSERT(config_agent_max_tokens(cfg) == 1024);
	config_free(cfg);
	remove(path);
	return 0;
}

static int test_patch_rejects_invalid_json(void)
{
	char path[128];
	FILE *f;
	char *patched = NULL;
	size_t patched_len = 0;
	char errbuf[256];

	ASSERT(test_runner_mkstemp_path("shellclaw_test_config_patch", path, sizeof(path)) == 0);
	f = fopen(path, "w");
	ASSERT(f);
	fprintf(f, "[agent]\nmodel = \"old-model\"\n");
	fclose(f);

	ASSERT(config_patch_dashboard_json(path, "not-json", &patched, &patched_len, errbuf,
	                                   sizeof(errbuf)) != 0);
	ASSERT(patched == NULL);
	remove(path);
	return 0;
}

int main(void)
{
	int failed = 0;

	if (test_patch_model_and_temperature() != 0) {
		fprintf(stderr, "test_patch_model_and_temperature failed\n");
		failed++;
	}
	if (test_patch_rejects_invalid_json() != 0) {
		fprintf(stderr, "test_patch_rejects_invalid_json failed\n");
		failed++;
	}
	if (failed == 0)
		printf("test_config_patch: all tests passed\n");
	return failed;
}
