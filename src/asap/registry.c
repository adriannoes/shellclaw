/**
 * @file registry.c
 * @brief ASAP registry.json fetch (GET) and JSON parse into #registry_index_t.
 */
#define _POSIX_C_SOURCE 200809L

#include "asap/registry.h"
#include "asap/client.h"
#include "providers/provider.h"
#include "cJSON.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define REGISTRY_DEFAULT_TTL_SEC 300

void registry_index_init(registry_index_t *out)
{
	if (!out) return;
	out->agents = NULL;
	out->count = 0;
}

void registry_index_clear(registry_index_t *out)
{
	size_t i, j;
	if (!out) return;
	for (i = 0; i < out->count; i++) {
		free(out->agents[i].urn);
		free(out->agents[i].base_url);
		for (j = 0; j < out->agents[i].capabilities_count; j++)
			free(out->agents[i].capabilities[j]);
		free(out->agents[i].capabilities);
	}
	free(out->agents);
	out->agents = NULL;
	out->count = 0;
}

static void err_copy(char *errbuf, size_t errlen, const char *msg)
{
	if (!errbuf || errlen == 0) return;
	(void)snprintf(errbuf, errlen, "%s", msg ? msg : "error");
}

static const char *json_string_cstr(cJSON *obj, const char *key)
{
	const cJSON *j = cJSON_GetObjectItemCaseSensitive(obj, key);
	if (!j || !cJSON_IsString(j)) return NULL;
	if (!j->valuestring || j->valuestring[0] == '\0') return NULL;
	return j->valuestring;
}

static const char *pick_urn(cJSON *obj)
{
	const char *s = json_string_cstr(obj, "urn");
	if (!s) s = json_string_cstr(obj, "id");
	return s;
}

static const char *pick_base_url(cJSON *obj)
{
	const char *s = json_string_cstr(obj, "base_url");
	if (!s) s = json_string_cstr(obj, "baseUrl");
	if (!s) s = json_string_cstr(obj, "endpoint");
	return s;
}

static void free_agent_fields(registry_agent_t *a)
{
	size_t j;
	free(a->urn);
	free(a->base_url);
	for (j = 0; j < a->capabilities_count; j++)
		free(a->capabilities[j]);
	free(a->capabilities);
	a->urn = NULL;
	a->base_url = NULL;
	a->capabilities = NULL;
	a->capabilities_count = 0;
}

static int parse_capabilities(cJSON *obj, registry_agent_t *a, char *errbuf, size_t errlen)
{
	const cJSON *cap = cJSON_GetObjectItemCaseSensitive(obj, "capabilities");
	int n, k;
	char **arr;
	if (!cap || cJSON_IsNull(cap)) {
		a->capabilities = NULL;
		a->capabilities_count = 0;
		return 0;
	}
	if (!cJSON_IsArray(cap)) {
		err_copy(errbuf, errlen, "capabilities must be a JSON array");
		return -1;
	}
	n = cJSON_GetArraySize(cap);
	if (n <= 0) {
		a->capabilities = NULL;
		a->capabilities_count = 0;
		return 0;
	}
	arr = calloc((size_t)n, sizeof(char *));
	if (!arr) {
		err_copy(errbuf, errlen, "out of memory");
		return -1;
	}
	for (k = 0; k < n; k++) {
		const cJSON *it = cJSON_GetArrayItem(cap, k);
		if (!it || !cJSON_IsString(it) || !it->valuestring) {
			a->capabilities = arr;
			a->capabilities_count = (size_t)k;
			err_copy(errbuf, errlen, "capabilities entries must be strings");
			return -1;
		}
		arr[k] = provider_dup_str(it->valuestring);
		if (!arr[k]) {
			a->capabilities = arr;
			a->capabilities_count = (size_t)k;
			err_copy(errbuf, errlen, "out of memory");
			return -1;
		}
	}
	a->capabilities = arr;
	a->capabilities_count = (size_t)n;
	return 0;
}

static int push_agent(registry_index_t *idx, registry_agent_t *a)
{
	registry_agent_t *p = realloc(idx->agents, (idx->count + 1u) * sizeof(*p));
	if (!p) return -1;
	idx->agents = p;
	idx->agents[idx->count] = *a;
	idx->count++;
	return 0;
}

int registry_index_from_json(const char *json, registry_index_t *out, char *errbuf, size_t errlen)
{
	cJSON *root;
	cJSON *agent_list;
	int i, n;
	registry_index_t tmp;
	if (errbuf && errlen) errbuf[0] = '\0';
	if (!json || !out) {
		err_copy(errbuf, errlen, "invalid argument");
		return -1;
	}
	registry_index_init(&tmp);
	root = cJSON_Parse(json);
	if (!root) {
		err_copy(errbuf, errlen, "invalid JSON");
		return -1;
	}
	if (cJSON_IsArray(root)) {
		agent_list = root;
	} else if (cJSON_IsObject(root)) {
		agent_list = cJSON_GetObjectItemCaseSensitive(root, "agents");
		if (!agent_list || !cJSON_IsArray(agent_list)) {
			cJSON_Delete(root);
			err_copy(errbuf, errlen, "missing agents array");
			return -1;
		}
	} else {
		cJSON_Delete(root);
		err_copy(errbuf, errlen, "registry root must be object or array");
		return -1;
	}
	n = cJSON_GetArraySize(agent_list);
	for (i = 0; i < n; i++) {
		cJSON *item = cJSON_GetArrayItem(agent_list, i);
		const char *urn;
		const char *base;
		registry_agent_t a;
		memset(&a, 0, sizeof(a));
		if (!item || !cJSON_IsObject(item)) continue;
		urn = pick_urn(item);
		base = pick_base_url(item);
		if (!urn || !base) continue;
		a.urn = provider_dup_str(urn);
		a.base_url = provider_dup_str(base);
		if (!a.urn || !a.base_url) {
			free_agent_fields(&a);
			err_copy(errbuf, errlen, "out of memory");
			goto fail;
		}
		if (parse_capabilities(item, &a, errbuf, errlen) != 0) {
			free_agent_fields(&a);
			goto fail;
		}
		if (push_agent(&tmp, &a) != 0) {
			free_agent_fields(&a);
			err_copy(errbuf, errlen, "out of memory");
			goto fail;
		}
	}
	cJSON_Delete(root);
	registry_index_clear(out);
	*out = tmp;
	return 0;
fail:
	cJSON_Delete(root);
	registry_index_clear(&tmp);
	return -1;
}

/**
 * GET @a url ; on HTTP 200 allocates @a *body_out (caller frees). Uses @a sc for TLS and timeouts.
 */
static int registry_http_get_body(const char *url, const asap_client_config_t *sc, char **body_out, char *errbuf,
		size_t errlen)
{
	CURL *curl;
	provider_curl_buf_t buf = { NULL, 0, 0 };
	long code = 0;
	CURLcode cres;
	struct curl_slist *headers = NULL;
	if (errbuf && errlen) errbuf[0] = '\0';
	if (!url || !sc || !body_out) {
		err_copy(errbuf, errlen, "invalid argument");
		return -1;
	}
	*body_out = NULL;
	buf.buf = malloc(PROVIDER_RESP_BUF_INIT);
	if (!buf.buf) {
		err_copy(errbuf, errlen, "out of memory");
		return -1;
	}
	buf.cap = PROVIDER_RESP_BUF_INIT;
	curl = curl_easy_init();
	if (!curl) {
		free(buf.buf);
		err_copy(errbuf, errlen, "curl init failed");
		return -1;
	}
	headers = curl_slist_append(headers, "Accept: application/json");
	if (!headers) {
		curl_easy_cleanup(curl);
		free(buf.buf);
		err_copy(errbuf, errlen, "out of memory");
		return -1;
	}
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, (long)sc->ssl_verifypeer);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, provider_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, sc->timeout_sec);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, sc->connect_timeout_sec);
	cres = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	if (cres != CURLE_OK) {
		free(buf.buf);
		err_copy(errbuf, errlen, curl_easy_strerror(cres));
		return -1;
	}
	if (code != 200L) {
		free(buf.buf);
		if (errbuf && errlen) (void)snprintf(errbuf, errlen, "HTTP %ld", code);
		return -1;
	}
	*body_out = buf.buf;
	return 0;
}

int registry_fetch(const char *url, const asap_client_config_t *client_opt, registry_index_t *out,
		char *errbuf, size_t errlen)
{
	asap_client_config_t sc;
	char *body = NULL;
	int ret = -1;
	if (errbuf && errlen) errbuf[0] = '\0';
	if (!url || !out) {
		err_copy(errbuf, errlen, "invalid argument");
		return -1;
	}
	registry_index_clear(out);
	asap_client_config_init(&sc);
	if (client_opt) {
		if (client_opt->timeout_sec > 0) sc.timeout_sec = client_opt->timeout_sec;
		if (client_opt->connect_timeout_sec > 0) sc.connect_timeout_sec = client_opt->connect_timeout_sec;
		sc.ssl_verifypeer = client_opt->ssl_verifypeer;
	}
	if (registry_http_get_body(url, &sc, &body, errbuf, errlen) != 0) return -1;
	if (registry_index_from_json(body, out, errbuf, errlen) == 0) ret = 0;
	free(body);
	return ret;
}

static const cJSON *revocation_pick_array(const cJSON *root)
{
	const cJSON *arr;
	if (!root) return NULL;
	if (cJSON_IsArray(root)) return root;
	if (!cJSON_IsObject(root)) return NULL;
	arr = cJSON_GetObjectItemCaseSensitive((cJSON *)root, "revoked");
	if (arr && cJSON_IsArray(arr)) return arr;
	arr = cJSON_GetObjectItemCaseSensitive((cJSON *)root, "revoked_agents");
	if (arr && cJSON_IsArray(arr)) return arr;
	arr = cJSON_GetObjectItemCaseSensitive((cJSON *)root, "revokedAgents");
	if (arr && cJSON_IsArray(arr)) return arr;
	return NULL;
}

/**
 * @return 1 if @a urn found, 0 if not, -1 on parse error.
 */
static int revocation_json_contains_urn(const char *json, const char *urn, char *errbuf, size_t errlen)
{
	cJSON *root;
	const cJSON *arr;
	int i, n;
	root = cJSON_Parse(json);
	if (!root) {
		err_copy(errbuf, errlen, "invalid JSON");
		return -1;
	}
	arr = revocation_pick_array(root);
	if (!arr) {
		cJSON_Delete(root);
		err_copy(errbuf, errlen, "revocation list must be array or object with revoked array");
		return -1;
	}
	n = cJSON_GetArraySize(arr);
	for (i = 0; i < n; i++) {
		const cJSON *it = cJSON_GetArrayItem(arr, i);
		const char *u;
		if (cJSON_IsString(it) && it->valuestring && strcmp(it->valuestring, urn) == 0) {
			cJSON_Delete(root);
			return 1;
		}
		if (cJSON_IsObject(it)) {
			u = json_string_cstr((cJSON *)it, "urn");
			if (!u) u = json_string_cstr((cJSON *)it, "id");
			if (u && strcmp(u, urn) == 0) {
				cJSON_Delete(root);
				return 1;
			}
		}
	}
	cJSON_Delete(root);
	return 0;
}

#ifdef SHELLCLAW_REGISTRY_TEST
static size_t g_revocation_fetch_count;
static const char *g_revocation_body_override;

void registry_test_revocation_reset(void)
{
	g_revocation_fetch_count = 0;
	g_revocation_body_override = NULL;
}

size_t registry_test_revocation_fetch_count(void)
{
	return g_revocation_fetch_count;
}

void registry_test_revocation_set_body_override(const char *json_or_null)
{
	g_revocation_body_override = json_or_null;
}
#endif

int registry_revocation_list_contains(const char *revocation_list_url, const char *urn,
		const asap_client_config_t *client_opt, char *errbuf, size_t errlen)
{
	char *body = NULL;
	asap_client_config_t sc;
	int r;
	if (errbuf && errlen) errbuf[0] = '\0';
	if (!revocation_list_url || revocation_list_url[0] == '\0') return 0;
	if (!urn || urn[0] == '\0') {
		err_copy(errbuf, errlen, "invalid argument");
		return -1;
	}
#ifdef SHELLCLAW_REGISTRY_TEST
	g_revocation_fetch_count++;
	if (g_revocation_body_override) {
		body = provider_dup_str(g_revocation_body_override);
		if (!body) {
			err_copy(errbuf, errlen, "out of memory");
			return -1;
		}
		r = revocation_json_contains_urn(body, urn, errbuf, errlen);
		free(body);
		return r;
	}
#endif
	asap_client_config_init(&sc);
	if (client_opt) {
		if (client_opt->timeout_sec > 0) sc.timeout_sec = client_opt->timeout_sec;
		if (client_opt->connect_timeout_sec > 0) sc.connect_timeout_sec = client_opt->connect_timeout_sec;
		sc.ssl_verifypeer = client_opt->ssl_verifypeer;
	} else {
		sc.timeout_sec = REGISTRY_REVOCATION_DEFAULT_TIMEOUT_SEC;
		sc.connect_timeout_sec = REGISTRY_REVOCATION_DEFAULT_CONNECT_TIMEOUT_SEC;
	}
	if (registry_http_get_body(revocation_list_url, &sc, &body, errbuf, errlen) != 0) return -1;
	r = revocation_json_contains_urn(body, urn, errbuf, errlen);
	free(body);
	return r;
}

static int monotonic_now(struct timespec *tp)
{
	if (clock_gettime(CLOCK_MONOTONIC, tp) != 0)
		return -1;
	return 0;
}

static int cache_stale(const registry_cache_t *cache, const struct timespec *now)
{
	long long age_sec;
	if (!cache->has_data) return 1;
	age_sec = (long long)(now->tv_sec - cache->fetched_at.tv_sec);
	if (cache->ttl_sec <= 0) return 1;
	return age_sec >= (long long)cache->ttl_sec;
}

static int registry_index_clone(const registry_index_t *src, registry_index_t *dst, char *errbuf, size_t errlen)
{
	size_t i, j;
	registry_index_t tmp;
	if (!src || !dst) {
		err_copy(errbuf, errlen, "invalid argument");
		return -1;
	}
	if (src->count == 0) {
		registry_index_clear(dst);
		return 0;
	}
	registry_index_init(&tmp);
	tmp.agents = calloc(src->count, sizeof(registry_agent_t));
	if (!tmp.agents) {
		err_copy(errbuf, errlen, "out of memory");
		return -1;
	}
	tmp.count = src->count;
	for (i = 0; i < src->count; i++) {
		registry_agent_t *d = &tmp.agents[i];
		const registry_agent_t *s = &src->agents[i];
		d->urn = provider_dup_str(s->urn);
		d->base_url = provider_dup_str(s->base_url);
		if (!d->urn || !d->base_url) {
			registry_index_clear(&tmp);
			err_copy(errbuf, errlen, "out of memory");
			return -1;
		}
		d->capabilities_count = s->capabilities_count;
		if (s->capabilities_count > 0) {
			d->capabilities = calloc(s->capabilities_count, sizeof(char *));
			if (!d->capabilities) {
				registry_index_clear(&tmp);
				err_copy(errbuf, errlen, "out of memory");
				return -1;
			}
			for (j = 0; j < s->capabilities_count; j++) {
				d->capabilities[j] = provider_dup_str(s->capabilities[j]);
				if (!d->capabilities[j]) {
					registry_index_clear(&tmp);
					err_copy(errbuf, errlen, "out of memory");
					return -1;
				}
			}
		} else {
			d->capabilities = NULL;
		}
	}
	registry_index_clear(dst);
	*dst = tmp;
	return 0;
}

void registry_cache_init(registry_cache_t *cache)
{
	if (!cache) return;
	memset(cache, 0, sizeof(*cache));
	cache->ttl_sec = REGISTRY_DEFAULT_TTL_SEC;
}

void registry_cache_clear(registry_cache_t *cache)
{
	if (!cache) return;
	registry_index_clear(&cache->index);
	free(cache->url);
	cache->url = NULL;
	cache->has_data = 0;
}

void registry_cache_set_ttl(registry_cache_t *cache, int ttl_sec)
{
	if (!cache) return;
	cache->ttl_sec = ttl_sec > 0 ? ttl_sec : REGISTRY_DEFAULT_TTL_SEC;
}

int registry_cache_get(registry_cache_t *cache, const char *url, const asap_client_config_t *client_opt,
		registry_index_t *out, char *errbuf, size_t errlen)
{
	struct timespec now;
	registry_index_t fresh;
	int st;
	if (errbuf && errlen) errbuf[0] = '\0';
	if (!cache || !url || !out) {
		err_copy(errbuf, errlen, "invalid argument");
		return -1;
	}
	if (monotonic_now(&now) != 0) {
		err_copy(errbuf, errlen, "clock_gettime failed");
		return -1;
	}
	if (cache->has_data && cache->url && strcmp(cache->url, url) == 0 && !cache_stale(cache, &now))
		return registry_index_clone(&cache->index, out, errbuf, errlen);
	registry_index_init(&fresh);
	if (registry_fetch(url, client_opt, &fresh, errbuf, errlen) == 0) {
		free(cache->url);
		cache->url = provider_dup_str(url);
		if (!cache->url) {
			registry_index_clear(&fresh);
			err_copy(errbuf, errlen, "out of memory");
			return -1;
		}
		registry_index_clear(&cache->index);
		cache->index = fresh;
		registry_index_init(&fresh);
		if (monotonic_now(&cache->fetched_at) != 0)
			cache->fetched_at = now;
		cache->has_data = 1;
		return registry_index_clone(&cache->index, out, errbuf, errlen);
	}
	if (cache->has_data && cache->url && strcmp(cache->url, url) == 0) {
		(void)fprintf(stderr, "registry: refresh failed for %s (%s); using stale cache\n", url,
				errbuf && errlen && errbuf[0] ? errbuf : "unknown error");
		st = registry_index_clone(&cache->index, out, errbuf, errlen);
		return st;
	}
	return -1;
}

#ifdef SHELLCLAW_REGISTRY_TEST
int registry_cache_test_load_json(registry_cache_t *cache, const char *url, const char *json,
		char *errbuf, size_t errlen)
{
	struct timespec now;
	if (!cache || !url || !json) {
		err_copy(errbuf, errlen, "invalid argument");
		return -1;
	}
	if (registry_index_from_json(json, &cache->index, errbuf, errlen) != 0) return -1;
	free(cache->url);
	cache->url = provider_dup_str(url);
	if (!cache->url) {
		registry_index_clear(&cache->index);
		err_copy(errbuf, errlen, "out of memory");
		return -1;
	}
	if (monotonic_now(&now) != 0) {
		err_copy(errbuf, errlen, "clock_gettime failed");
		return -1;
	}
	cache->fetched_at = now;
	cache->has_data = 1;
	return 0;
}

void registry_cache_test_backdate(registry_cache_t *cache, int seconds_ago)
{
	if (!cache || seconds_ago < 0) return;
	cache->fetched_at.tv_sec -= (time_t)seconds_ago;
}
#endif
