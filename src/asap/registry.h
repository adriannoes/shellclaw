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

/** When #registry_revocation_list_contains is called with NULL @a client_opt, use this total HTTP timeout (seconds). */
#define REGISTRY_REVOCATION_DEFAULT_TIMEOUT_SEC 10L
/** Connect timeout paired with #REGISTRY_REVOCATION_DEFAULT_TIMEOUT_SEC when @a client_opt is NULL. */
#define REGISTRY_REVOCATION_DEFAULT_CONNECT_TIMEOUT_SEC 5L

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
 * Free heap fields of one agent (URN, base URL, capability strings). Zeros @a a.
 */
void registry_agent_clear(registry_agent_t *a);

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
 * Fetch @a revocation_list_url each time (no cache) and check whether @a urn is listed as revoked.
 * Supports a JSON array of strings, or an object with @c revoked , @c revoked_agents , or @c revokedAgents
 * holding an array of strings or objects with @c urn / @c id .
 *
 * If @a revocation_list_url is NULL or empty, returns @c 0 (no revocation list configured).
 *
 * @return @c 1 if @a urn is revoked, @c 0 if not listed, @c -1 on invalid arguments, HTTP/curl error,
 * non-200 status, or unparseable JSON body.
 */
int registry_revocation_list_contains(const char *revocation_list_url, const char *urn,
		const asap_client_config_t *client_opt, char *errbuf, size_t errlen);

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

/** Inputs for #registry_resolve / #registry_refresh ; zero with #registry_resolve_ctx_init then set pointers. */
typedef struct registry_resolve_ctx {
	registry_cache_t *cache;
	const char *registry_url;
	const char *revocation_list_url;
	const asap_client_config_t *client_opt;
} registry_resolve_ctx_t;

void registry_resolve_ctx_init(registry_resolve_ctx_t *ctx);

/**
 * Resolve @a urn against the cached registry: optional revocation check (fresh fetch each time when URL set),
 * then #registry_cache_get for @a ctx->registry_url . On success fills @a out (caller frees via #registry_agent_clear).
 *
 * @return 0 on success, -1 if revoked, not found, missing config, or transport/parse errors (@a errbuf when provided).
 */
int registry_resolve(const registry_resolve_ctx_t *ctx, const char *urn, registry_agent_t *out, char *errbuf,
		size_t errlen);

/**
 * Force HTTP GET of @a ctx->registry_url and replace @a ctx->cache contents (ignores TTL).
 *
 * @return 0 on success, -1 on failure.
 */
int registry_refresh(registry_resolve_ctx_t *ctx, char *errbuf, size_t errlen);

/** Same as #registry_resolve (alternate name). */
static inline int registry_resolve_urn(const registry_resolve_ctx_t *ctx, const char *urn, registry_agent_t *out,
		char *errbuf, size_t errlen)
{
	return registry_resolve(ctx, urn, out, errbuf, errlen);
}

#ifdef SHELLCLAW_REGISTRY_TEST
/**
 * Load registry JSON into the cache without HTTP (unit tests only). Sets @a has_data.
 */
int registry_cache_test_load_json(registry_cache_t *cache, const char *url, const char *json,
		char *errbuf, size_t errlen);
/** Move @a cache fetch time backward by @a seconds_ago (unit tests only). */
void registry_cache_test_backdate(registry_cache_t *cache, int seconds_ago);
/** Reset revocation test counters and body override (unit tests only). */
void registry_test_revocation_reset(void);
/** Number of revocation list HTTP (or override) fetches since last #registry_test_revocation_reset (unit tests only). */
size_t registry_test_revocation_fetch_count(void);
/**
 * When non-NULL, #registry_revocation_list_contains uses this body instead of curl; fetch counter still increments.
 */
void registry_test_revocation_set_body_override(const char *json_or_null);
/** Reset #registry_fetch test override and counter (unit tests only). */
void registry_test_registry_fetch_reset(void);
/** HTTP/registry_fetch attempts via override since last #registry_test_registry_fetch_reset (unit tests only). */
size_t registry_test_registry_fetch_count(void);
/** When non-NULL, #registry_fetch parses this body instead of curl; increments #registry_test_registry_fetch_count. */
void registry_test_registry_fetch_set_body_override(const char *json_or_null);
#endif

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_ASAP_REGISTRY_H */
