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

int registry_fetch(const char *url, const asap_client_config_t *client_opt, registry_index_t *out,
		char *errbuf, size_t errlen)
{
	CURL *curl;
	provider_curl_buf_t buf = { NULL, 0, 0 };
	asap_client_config_t sc;
	long code = 0;
	CURLcode cres;
	int ret = -1;
	struct curl_slist *headers = NULL;
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
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, (long)sc.ssl_verifypeer);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, provider_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, sc.timeout_sec);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, sc.connect_timeout_sec);
	cres = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	if (cres != CURLE_OK) {
		err_copy(errbuf, errlen, curl_easy_strerror(cres));
		goto out;
	}
	if (code != 200L) {
		if (errbuf && errlen) (void)snprintf(errbuf, errlen, "HTTP %ld", code);
		goto out;
	}
	if (registry_index_from_json(buf.buf, out, errbuf, errlen) == 0)
		ret = 0;
out:
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	free(buf.buf);
	return ret;
}
