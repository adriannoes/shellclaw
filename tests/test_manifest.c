/**
 * @file test_manifest.c
 * @brief Unit tests for ASAP manifest and health JSON.
 */
#define _POSIX_C_SOURCE 200809L

#include "asap/manifest.h"
#include "core/config.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
#define TMP_CONFIG "/tmp/shellclaw_test_manifest_config.toml"

static int test_health_json(void)
{
	const char *s = manifest_health_json();
	ASSERT(s != NULL);
	ASSERT(strstr(s, "status") != NULL);
	ASSERT(strstr(s, "ok") != NULL);
	cJSON *parsed = cJSON_Parse(s);
	ASSERT(parsed != NULL);
	cJSON *status = cJSON_GetObjectItem(parsed, "status");
	ASSERT(status != NULL);
	ASSERT(cJSON_IsString(status));
	ASSERT(strcmp(status->valuestring, "ok") == 0);
	cJSON_Delete(parsed);
	return 0;
}

static int test_manifest_json_null_config(void)
{
	char *json = manifest_build_json(NULL);
	ASSERT(json != NULL);
	cJSON *parsed = cJSON_Parse(json);
	ASSERT(parsed != NULL);
	cJSON *id = cJSON_GetObjectItem(parsed, "id");
	ASSERT(id != NULL);
	ASSERT(cJSON_IsString(id));
	ASSERT(strstr(id->valuestring, "urn:asap:agent") != NULL);
	cJSON *name = cJSON_GetObjectItem(parsed, "name");
	ASSERT(name != NULL);
	ASSERT(cJSON_IsString(name));
	cJSON *version = cJSON_GetObjectItem(parsed, "version");
	ASSERT(version != NULL);
	ASSERT(cJSON_IsString(version));
	cJSON *skills = cJSON_GetObjectItem(parsed, "skills");
	ASSERT(skills != NULL);
	ASSERT(cJSON_IsArray(skills));
	cJSON *endpoints = cJSON_GetObjectItem(parsed, "endpoints");
	ASSERT(endpoints != NULL);
	ASSERT(cJSON_IsObject(endpoints));
	cJSON *asap_ep = cJSON_GetObjectItem(endpoints, "asap");
	ASSERT(asap_ep != NULL);
	ASSERT(strcmp(asap_ep->valuestring, "/asap") == 0);
	cJSON *health_ep = cJSON_GetObjectItem(endpoints, "health");
	ASSERT(health_ep != NULL);
	ASSERT(strstr(health_ep->valuestring, "health") != NULL);
	cJSON *manifest_ep = cJSON_GetObjectItem(endpoints, "manifest");
	ASSERT(manifest_ep != NULL);
	ASSERT(strstr(manifest_ep->valuestring, "manifest.json") != NULL);
	cJSON_Delete(parsed);
	free(json);
	return 0;
}

static int test_manifest_json_with_config(void)
{
	FILE *f = fopen(TMP_CONFIG, "w");
	ASSERT(f);
	fprintf(f, "[agent]\nmodel = \"test\"\n");
	fprintf(f, "[asap]\nagent_urn = \"urn:asap:agent:my-custom\"\nagent_name = \"My Agent\"\n");
	fprintf(f, "[skills]\ndir = \"/tmp/shellclaw_test_manifest_skills\"\n");
	fclose(f);
	char errbuf[256];
	config_t *cfg = NULL;
	int r = config_load(TMP_CONFIG, &cfg, errbuf, sizeof(errbuf));
	ASSERT(r == 0);
	char *json = manifest_build_json(cfg);
	ASSERT(json != NULL);
	ASSERT(strstr(json, "urn:asap:agent:my-custom") != NULL);
	ASSERT(strstr(json, "My Agent") != NULL);
	cJSON *parsed = cJSON_Parse(json);
	ASSERT(parsed != NULL);
	cJSON *id = cJSON_GetObjectItem(parsed, "id");
	ASSERT(id != NULL);
	ASSERT(strcmp(id->valuestring, "urn:asap:agent:my-custom") == 0);
	cJSON *name = cJSON_GetObjectItem(parsed, "name");
	ASSERT(name != NULL);
	ASSERT(strcmp(name->valuestring, "My Agent") == 0);
	cJSON_Delete(parsed);
	free(json);
	config_free(cfg);
	unlink(TMP_CONFIG);
	return 0;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	int failed = 0;
	if (test_health_json() != 0) { fprintf(stderr, "test_health_json failed\n"); failed++; }
	if (test_manifest_json_null_config() != 0) { fprintf(stderr, "test_manifest_json_null_config failed\n"); failed++; }
	if (test_manifest_json_with_config() != 0) { fprintf(stderr, "test_manifest_json_with_config failed\n"); failed++; }
	if (failed == 0)
		printf("test_manifest: all tests passed\n");
	return failed;
}
