/**
 * @file client.c
 * @brief ASAP client: POST JSON-RPC via libcurl, parse result envelope.
 */
#define _POSIX_C_SOURCE 200809L

#include "asap/client.h"
#include "asap/ulid.h"
#include "core/config.h"
#include "providers/provider.h"
#include "cJSON.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void asap_client_config_init(asap_client_config_t *c)
{
	if (!c) return;
	c->timeout_sec = 30L;
	c->connect_timeout_sec = 30L;
	c->ssl_verifypeer = 1;
}

void asap_client_config_from_config(const config_t *cfg, asap_client_config_t *c)
{
	asap_client_config_init(c);
	if (cfg) c->timeout_sec = (long)config_asap_client_timeout_sec(cfg);
}

int asap_client_send_task(const char *url, const char *bearer, const char *jsonrpc_method,
		const asap_envelope_t *env, const asap_client_config_t *client, const config_t *cfg,
		asap_envelope_t *response, char *errbuf, size_t errlen)
{
	CURL *curl;
	struct curl_slist *headers = NULL;
	provider_curl_buf_t buf = { NULL, 0, 0 };
	cJSON *req_id = NULL;
	char *body = NULL;
	char *h_auth = NULL;
	const char *method = jsonrpc_method && jsonrpc_method[0] != '\0' ? jsonrpc_method : ASAP_DEFAULT_JSONRPC_METHOD;
	asap_client_config_t sc;
	int ret = -1;
	long code = 0;
	CURLcode cres;

	if (errbuf && errlen) errbuf[0] = '\0';
	if (!url || !env || !response) {
		if (errbuf && errlen) (void)snprintf(errbuf, errlen, "invalid argument");
		return -1;
	}
	asap_client_config_from_config(cfg, &sc);
	if (client) {
		if (client->timeout_sec > 0) sc.timeout_sec = client->timeout_sec;
		if (client->connect_timeout_sec > 0) sc.connect_timeout_sec = client->connect_timeout_sec;
		sc.ssl_verifypeer = client->ssl_verifypeer;
	}
	if (env->jsonrpc_request_id) {
		req_id = cJSON_Duplicate(env->jsonrpc_request_id, 1);
	} else {
		char u[32];
		if (ulid_generate(u, sizeof u) != 0) {
			if (errbuf && errlen) (void)snprintf(errbuf, errlen, "failed to generate id");
			return -1;
		}
		req_id = cJSON_CreateString(u);
	}
	if (!req_id) {
		if (errbuf && errlen) (void)snprintf(errbuf, errlen, "out of memory");
		return -1;
	}
	body = asap_envelope_to_jsonrpc_request_string(env, req_id, method);
	cJSON_Delete(req_id);
	req_id = NULL;
	if (!body) {
		if (errbuf && errlen) (void)snprintf(errbuf, errlen, "failed to build request JSON");
		return -1;
	}
	buf.buf = malloc(PROVIDER_RESP_BUF_INIT);
	if (!buf.buf) {
		free(body);
		if (errbuf && errlen) (void)snprintf(errbuf, errlen, "out of memory");
		return -1;
	}
	buf.cap = PROVIDER_RESP_BUF_INIT;
	if (bearer && bearer[0] != '\0') {
		size_t nb = strlen(bearer) + 32u;
		h_auth = malloc(nb);
		if (!h_auth) {
			free(body);
			free(buf.buf);
			if (errbuf && errlen) (void)snprintf(errbuf, errlen, "out of memory");
			return -1;
		}
		(void)snprintf(h_auth, nb, "Authorization: Bearer %s", bearer);
		headers = curl_slist_append(headers, "Content-Type: application/json");
		headers = curl_slist_append(headers, h_auth);
	} else {
		headers = curl_slist_append(headers, "Content-Type: application/json");
	}
	if (!headers) {
		free(h_auth);
		free(body);
		free(buf.buf);
		if (errbuf && errlen) (void)snprintf(errbuf, errlen, "out of memory");
		return -1;
	}
	curl = curl_easy_init();
	if (!curl) {
		curl_slist_free_all(headers);
		free(h_auth);
		free(body);
		free(buf.buf);
		if (errbuf && errlen) (void)snprintf(errbuf, errlen, "curl init failed");
		return -1;
	}
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, (long)sc.ssl_verifypeer);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, provider_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, sc.timeout_sec);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, sc.connect_timeout_sec);
	cres = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	if (cres != CURLE_OK) {
		if (errbuf && errlen) (void)snprintf(errbuf, errlen, "curl: %s", curl_easy_strerror(cres));
		goto out;
	}
	if (code != 200L) {
		if (errbuf && errlen) (void)snprintf(errbuf, errlen, "HTTP %ld", code);
		goto out;
	}
	if (asap_envelope_parse_jsonrpc_response(buf.buf, response, errbuf, errlen) == 0)
		ret = 0;
out:
	curl_easy_cleanup(curl);
	curl_slist_free_all(headers);
	free(h_auth);
	free(body);
	free(buf.buf);
	return ret;
}
