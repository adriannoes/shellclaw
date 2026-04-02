/**
 * @file provider_common.c
 * @brief Shared provider helpers: response cleanup, curl buffer, error helpers.
 */

#include "providers/provider.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void provider_response_clear(provider_response_t *r)
{
	if (!r) return;
	free(r->content);
	r->content = NULL;
	if (r->tool_calls) {
		for (size_t i = 0; i < r->tool_calls_count; i++) {
			free(r->tool_calls[i].id);
			free(r->tool_calls[i].name);
			free(r->tool_calls[i].arguments);
		}
		free(r->tool_calls);
		r->tool_calls = NULL;
		r->tool_calls_count = 0;
	}
	r->error = 0;
}

size_t provider_write_cb(const char *ptr, size_t size, size_t nmemb, void *userdata)
{
	provider_curl_buf_t *b = (provider_curl_buf_t *)userdata;
	if (!b || !b->buf) return 0;
	if (nmemb != 0 && size > SIZE_MAX / nmemb) return 0;
	size_t n = size * nmemb;
	size_t need = b->len + n + 1;
	if (need > PROVIDER_RESP_BUF_INIT * 4) return 0;
	if (need > b->cap) {
		size_t new_cap = b->cap ? b->cap * 2 : PROVIDER_RESP_BUF_INIT;
		while (new_cap < need && new_cap <= PROVIDER_RESP_BUF_INIT * 4) new_cap *= 2;
		if (need > new_cap) return 0;
		char *p = realloc(b->buf, new_cap);
		if (!p) return 0;
		b->buf = p;
		b->cap = new_cap;
	}
	memcpy(b->buf + b->len, ptr, n);
	b->len += n;
	b->buf[b->len] = '\0';
	return n;
}

void provider_set_error(provider_response_t *response, const char *msg)
{
	response->error = 1;
	response->content = msg ? provider_dup_str(msg) : NULL;
}

char *provider_dup_str(const char *s)
{
	if (!s) return NULL;
	size_t n = strlen(s) + 1;
	char *p = malloc(n);
	if (p) memcpy(p, s, n);
	return p;
}
