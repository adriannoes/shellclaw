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

static int test_cache_fresh_hit(void)
{
	static const char *reg_url = "https://registry.example/asap/registry.json";
	registry_cache_t cache;
	registry_index_t out;
	char err[256];
	registry_cache_init(&cache);
	registry_cache_set_ttl(&cache, 300);
	ASSERT(registry_cache_test_load_json(&cache, reg_url, sample_registry, err, sizeof err) == 0);
	registry_index_init(&out);
	ASSERT(registry_cache_get(&cache, reg_url, NULL, &out, err, sizeof err) == 0);
	ASSERT(out.count == 2u);
	ASSERT(strcmp(out.agents[0].urn, "urn:asap:agent:a") == 0);
	registry_index_clear(&out);
	registry_cache_clear(&cache);
	return 0;
}

static int test_cache_stale_refresh_fail_uses_stale(void)
{
	static const char *reg_url = "http://127.0.0.1:9/registry.json";
	registry_cache_t cache;
	registry_index_t out;
	char err[256];
	asap_client_config_t c;
	registry_cache_init(&cache);
	registry_cache_set_ttl(&cache, 300);
	ASSERT(registry_cache_test_load_json(&cache, reg_url, sample_registry, err, sizeof err) == 0);
	registry_cache_test_backdate(&cache, 400);
	asap_client_config_init(&c);
	c.timeout_sec = 2L;
	c.connect_timeout_sec = 2L;
	registry_index_init(&out);
	ASSERT(registry_cache_get(&cache, reg_url, &c, &out, err, sizeof err) == 0);
	ASSERT(out.count == 2u);
	ASSERT(strcmp(out.agents[0].urn, "urn:asap:agent:a") == 0);
	registry_index_clear(&out);
	registry_cache_clear(&cache);
	return 0;
}

static int test_cache_first_fetch_fail(void)
{
	registry_cache_t cache;
	registry_index_t out;
	char err[256];
	asap_client_config_t c;
	registry_cache_init(&cache);
	registry_cache_set_ttl(&cache, 60);
	asap_client_config_init(&c);
	c.timeout_sec = 2L;
	c.connect_timeout_sec = 2L;
	registry_index_init(&out);
	ASSERT(registry_cache_get(&cache, "http://127.0.0.1:1/registry.json", &c, &out, err, sizeof err) == -1);
	registry_index_clear(&out);
	registry_cache_clear(&cache);
	return 0;
}

static int test_revocation_no_list_url_not_revoked(void)
{
	char err[128];
	ASSERT(registry_revocation_list_contains(NULL, "urn:asap:agent:x", NULL, err, sizeof err) == 0);
	ASSERT(registry_revocation_list_contains("", "urn:asap:agent:x", NULL, err, sizeof err) == 0);
	return 0;
}

static int test_revocation_empty_urn_invalid(void)
{
	char err[128];
	ASSERT(registry_revocation_list_contains("http://example/revoked.json", "", NULL, err, sizeof err) == -1);
	ASSERT(err[0] != '\0');
	return 0;
}

static int test_revocation_two_calls_two_fetches(void)
{
	char err[256];
	registry_test_revocation_reset();
	registry_test_revocation_set_body_override("[\"urn:asap:agent:bad\"]");
	ASSERT(registry_revocation_list_contains("http://unused.invalid/revoked.json", "urn:asap:agent:good", NULL, err,
				sizeof err)
			== 0);
	ASSERT(registry_test_revocation_fetch_count() == 1u);
	ASSERT(registry_revocation_list_contains("http://unused.invalid/revoked.json", "urn:asap:agent:good", NULL, err,
				sizeof err)
			== 0);
	ASSERT(registry_test_revocation_fetch_count() == 2u);
	registry_test_revocation_set_body_override(NULL);
	return 0;
}

static int test_revocation_detects_urn_in_array(void)
{
	char err[256];
	registry_test_revocation_reset();
	registry_test_revocation_set_body_override("[\"urn:asap:agent:a\",\"urn:asap:agent:b\"]");
	ASSERT(registry_revocation_list_contains("http://unused.invalid/r.json", "urn:asap:agent:b", NULL, err, sizeof err)
			== 1);
	ASSERT(registry_revocation_list_contains("http://unused.invalid/r.json", "urn:asap:agent:c", NULL, err, sizeof err)
			== 0);
	registry_test_revocation_set_body_override(NULL);
	return 0;
}

static int test_revocation_object_with_revoked_agents(void)
{
	char err[256];
	const char *json = "{\"revokedAgents\":[{\"id\":\"urn:asap:agent:z\"}]}";
	registry_test_revocation_reset();
	registry_test_revocation_set_body_override(json);
	ASSERT(registry_revocation_list_contains("http://unused.invalid/r.json", "urn:asap:agent:z", NULL, err, sizeof err)
			== 1);
	registry_test_revocation_set_body_override(NULL);
	return 0;
}

static int test_revocation_fetch_dead_port_counts_fetch(void)
{
	char err[256];
	asap_client_config_t c;
	registry_test_revocation_reset();
	asap_client_config_init(&c);
	c.timeout_sec = 2L;
	c.connect_timeout_sec = 2L;
	ASSERT(registry_revocation_list_contains("http://127.0.0.1:1/revoked.json", "urn:asap:agent:x", &c, err,
				sizeof err)
			== -1);
	ASSERT(registry_test_revocation_fetch_count() == 1u);
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
	RUN(test_cache_fresh_hit());
	RUN(test_cache_stale_refresh_fail_uses_stale());
	RUN(test_cache_first_fetch_fail());
	RUN(test_revocation_no_list_url_not_revoked());
	RUN(test_revocation_empty_urn_invalid());
	RUN(test_revocation_two_calls_two_fetches());
	RUN(test_revocation_detects_urn_in_array());
	RUN(test_revocation_object_with_revoked_agents());
	RUN(test_revocation_fetch_dead_port_counts_fetch());
	printf("test_asap_registry: all tests passed\n");
	return 0;
}
