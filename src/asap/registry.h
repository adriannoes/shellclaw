/**
 * @file registry.h
 * @brief ASAP registry: fetch registry.json and parse agent entries (URN, base URL, capabilities).
 */

#ifndef SHELLCLAW_ASAP_REGISTRY_H
#define SHELLCLAW_ASAP_REGISTRY_H

#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

struct asap_client_config;
typedef struct asap_client_config asap_client_config_t;

/** One agent row from the registry (caller frees via #registry_index_clear). */
typedef struct registry_agent {
	char *urn;
	char *base_url;
	char **capabilities;
	size_t capabilities_count;
} registry_agent_t;

/** Parsed registry: array of #registry_agent_t. */
typedef struct registry_index {
	registry_agent_t *agents;
	size_t count;
} registry_index_t;

/** Zero @a out without freeing (use before first parse or on stack). */
void registry_index_init(registry_index_t *out);

/**
 * Free all agents and reset @a out. Safe on cleared or zeroed struct.
 */
void registry_index_clear(registry_index_t *out);

/**
 * Parse ASAP registry JSON into @a out. Replaces previous contents of @a out.
 * Supported shapes: @c { "agents": [ { "urn", "base_url", "capabilities": [] } ] }
 * or a top-level JSON array of agent objects. Alternate keys: @c id (URN),
 * @c baseUrl / @c endpoint (base URL).
 *
 * @param json   UTF-8 JSON body (not modified).
 * @param out    Output index; must not be NULL.
 * @param errbuf Optional human-readable error (truncated to @a errlen).
 * @param errlen Buffer size for @a errbuf.
 * @return 0 on success (including empty agent list), -1 on parse or schema error.
 */
int registry_index_from_json(const char *json, registry_index_t *out, char *errbuf, size_t errlen);

/**
 * HTTP GET @a url and parse the response body as a registry document.
 *
 * @param url         Full URL to registry.json (or equivalent).
 * @param client_opt  Optional timeouts and TLS flags; NULL uses defaults (30s, verify peer).
 * @param out         Filled on success; cleared first.
 * @param errbuf      Optional error message buffer.
 * @param errlen      Size of @a errbuf.
 * @return 0 on HTTP 200 and successful parse, -1 on curl, non-200, or parse error.
 */
int registry_fetch(const char *url, const asap_client_config_t *client_opt, registry_index_t *out,
		char *errbuf, size_t errlen);

/**
 * Cached registry for one URL: parsed index, monotonic fetch time, TTL in seconds.
 * Default TTL after #registry_cache_init is five minutes (300 s); override with
 * #registry_cache_set_ttl.
 */
typedef struct registry_cache {
	registry_index_t index;
	char *url;
	struct timespec fetched_at;
	int ttl_sec;
	int has_data;
} registry_cache_t;

void registry_cache_init(registry_cache_t *cache);
void registry_cache_clear(registry_cache_t *cache);
/** Set TTL in seconds; values @c <= 0 restore the default (300). */
void registry_cache_set_ttl(registry_cache_t *cache, int ttl_sec);

/**
 * Return registry agents for @a url using the cache when younger than TTL; otherwise
 * refreshes via HTTP GET. On refresh failure, logs a warning to stderr and returns
 * the last successful payload for the same URL when available.
 *
 * @a out is always replaced on success (caller #registry_index_clear when done).
 * @return 0 on success, -1 when there is no usable data (first fetch failed or OOM).
 */
int registry_cache_get(registry_cache_t *cache, const char *url, const asap_client_config_t *client_opt,
		registry_index_t *out, char *errbuf, size_t errlen);

#ifdef SHELLCLAW_REGISTRY_TEST
/**
 * Load registry JSON into the cache without HTTP (unit tests only). Sets @a has_data.
 */
int registry_cache_test_load_json(registry_cache_t *cache, const char *url, const char *json,
		char *errbuf, size_t errlen);
/** Move @a cache fetch time backward by @a seconds_ago (unit tests only). */
void registry_cache_test_backdate(registry_cache_t *cache, int seconds_ago);
#endif

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_ASAP_REGISTRY_H */
