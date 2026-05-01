/**
 * @file test_asap_registry.c
 * @brief Unit tests for ASAP registry JSON parse and fetch error paths.
 */

#include "asap/registry.h"
#include "asap/client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
#define RUN(t) do { int r = (t); if (r) return r; } while (0)

static const char *sample_registry =
	"{\"agents\":["
	"{\"urn\":\"urn:asap:agent:a\",\"base_url\":\"https://a.example/asap\",\"capabilities\":[\"task.request\"]},"
	"{\"urn\":\"urn:asap:agent:b\",\"baseUrl\":\"https://b.example/\",\"capabilities\":[]}"
	"]}";

static int test_parse_agents_object(void)
{
	registry_index_t idx;
	char err[128];
	registry_index_init(&idx);
	ASSERT(registry_index_from_json(sample_registry, &idx, err, sizeof err) == 0);
	ASSERT(idx.count == 2u);
	ASSERT(strcmp(idx.agents[0].urn, "urn:asap:agent:a") == 0);
	ASSERT(strcmp(idx.agents[0].base_url, "https://a.example/asap") == 0);
	ASSERT(idx.agents[0].capabilities_count == 1u);
	ASSERT(strcmp(idx.agents[0].capabilities[0], "task.request") == 0);
	ASSERT(strcmp(idx.agents[1].urn, "urn:asap:agent:b") == 0);
	ASSERT(strcmp(idx.agents[1].base_url, "https://b.example/") == 0);
	ASSERT(idx.agents[1].capabilities_count == 0u);
	registry_index_clear(&idx);
	return 0;
}

static int test_parse_top_level_array(void)
{
	const char *json = "[{\"id\":\"urn:asap:agent:x\",\"endpoint\":\"https://x.test/\"}]";
	registry_index_t idx;
	char err[128];
	registry_index_init(&idx);
	ASSERT(registry_index_from_json(json, &idx, err, sizeof err) == 0);
	ASSERT(idx.count == 1u);
	ASSERT(strcmp(idx.agents[0].urn, "urn:asap:agent:x") == 0);
	ASSERT(strcmp(idx.agents[0].base_url, "https://x.test/") == 0);
	registry_index_clear(&idx);
	return 0;
}

static int test_parse_invalid_json(void)
{
	registry_index_t idx;
	char err[128];
	registry_index_init(&idx);
	ASSERT(registry_index_from_json("not json", &idx, err, sizeof err) == -1);
	ASSERT(err[0] != '\0');
	registry_index_clear(&idx);
	return 0;
}

static int test_parse_missing_agents(void)
{
	registry_index_t idx;
	char err[128];
	registry_index_init(&idx);
	ASSERT(registry_index_from_json("{\"version\":1}", &idx, err, sizeof err) == -1);
	ASSERT(strstr(err, "agents") != NULL);
	registry_index_clear(&idx);
	return 0;
}

static int test_parse_capabilities_not_array(void)
{
	const char *json = "{\"agents\":[{\"urn\":\"u\",\"base_url\":\"b\",\"capabilities\":1}]}";
	registry_index_t idx;
	char err[128];
	registry_index_init(&idx);
	ASSERT(registry_index_from_json(json, &idx, err, sizeof err) == -1);
	registry_index_clear(&idx);
	return 0;
}

static int test_fetch_dead_port(void)
{
	registry_index_t idx;
	char err[256];
	asap_client_config_t c;
	registry_index_init(&idx);
	asap_client_config_init(&c);
	c.timeout_sec = 2L;
	c.connect_timeout_sec = 2L;
	ASSERT(registry_fetch("http://127.0.0.1:1/registry.json", &c, &idx, err, sizeof err) == -1);
	ASSERT(err[0] != '\0');
	registry_index_clear(&idx);
	return 0;
}

int main(void)
{
	RUN(test_parse_agents_object());
	RUN(test_parse_top_level_array());
	RUN(test_parse_invalid_json());
	RUN(test_parse_missing_agents());
	RUN(test_parse_capabilities_not_array());
	RUN(test_fetch_dead_port());
	printf("test_asap_registry: all tests passed\n");
	return 0;
}
