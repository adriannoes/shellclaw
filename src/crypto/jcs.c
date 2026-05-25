/**
 * @file jcs.c
 * @brief RFC 8785 JCS canonicalization for cJSON trees (manifest signing subset).
 */
#include "crypto/jcs.h"

#include "cJSON.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JCS_INIT_CAP 256U

typedef struct {
	unsigned char *buf;
	size_t len;
	size_t cap;
} jcs_buf_t;

static int jcs_buf_reserve(jcs_buf_t *b, size_t need)
{
	size_t new_cap;
	unsigned char *p;
	if (!b)
		return -1;
	if (need <= b->cap)
		return 0;
	new_cap = b->cap ? b->cap : JCS_INIT_CAP;
	while (new_cap < need) {
		if (new_cap > (SIZE_MAX / 2U))
			return -1;
		new_cap *= 2U;
	}
	p = (unsigned char *)realloc(b->buf, new_cap);
	if (!p)
		return -1;
	b->buf = p;
	b->cap = new_cap;
	return 0;
}

static int jcs_buf_append(jcs_buf_t *b, const char *s, size_t n)
{
	if (!b || !s)
		return -1;
	if (jcs_buf_reserve(b, b->len + n + 1U) != 0)
		return -1;
	memcpy(b->buf + b->len, s, n);
	b->len += n;
	b->buf[b->len] = '\0';
	return 0;
}

static int jcs_buf_append_cstr(jcs_buf_t *b, const char *s)
{
	if (!s)
		return -1;
	return jcs_buf_append(b, s, strlen(s));
}

static int jcs_buf_append_byte(jcs_buf_t *b, char c)
{
	return jcs_buf_append(b, &c, 1U);
}

static int jcs_key_cmp(const void *a, const void *b)
{
	const char *ka = *(const char *const *)a;
	const char *kb = *(const char *const *)b;
	return strcmp(ka, kb);
}

static int jcs_append_escaped_string(jcs_buf_t *b, const char *s)
{
	const unsigned char *p;

	if (!s)
		return -1;
	if (jcs_buf_append_byte(b, '"') != 0)
		return -1;
	for (p = (const unsigned char *)s; *p != '\0'; p++) {
		unsigned char c = *p;
		if (c == '"') {
			if (jcs_buf_append_cstr(b, "\\\"") != 0)
				return -1;
		} else if (c == '\\') {
			if (jcs_buf_append_cstr(b, "\\\\") != 0)
				return -1;
		} else if (c == '\b') {
			if (jcs_buf_append_cstr(b, "\\b") != 0)
				return -1;
		} else if (c == '\f') {
			if (jcs_buf_append_cstr(b, "\\f") != 0)
				return -1;
		} else if (c == '\n') {
			if (jcs_buf_append_cstr(b, "\\n") != 0)
				return -1;
		} else if (c == '\r') {
			if (jcs_buf_append_cstr(b, "\\r") != 0)
				return -1;
		} else if (c == '\t') {
			if (jcs_buf_append_cstr(b, "\\t") != 0)
				return -1;
		} else if (c < 0x20U) {
			char hex[7];
			snprintf(hex, sizeof(hex), "\\u%04x", (unsigned)c);
			if (jcs_buf_append_cstr(b, hex) != 0)
				return -1;
		} else {
			if (jcs_buf_append_byte(b, (char)c) != 0)
				return -1;
		}
	}
	return jcs_buf_append_byte(b, '"');
}

static int jcs_is_safe_integer(double v, long long *out)
{
	long long n;
	if (!isfinite(v))
		return 0;
	if (v < -9007199254740991.0 || v > 9007199254740991.0)
		return 0;
	n = (long long)v;
	if ((double)n != v)
		return 0;
	*out = n;
	return 1;
}

static int jcs_append_number(jcs_buf_t *b, double v)
{
	long long n;
	char tmp[64];
	if (!isfinite(v))
		return -1;
	if (jcs_is_safe_integer(v, &n)) {
		snprintf(tmp, sizeof(tmp), "%lld", n);
		return jcs_buf_append_cstr(b, tmp);
	}
	snprintf(tmp, sizeof(tmp), "%.17g", v);
	return jcs_buf_append_cstr(b, tmp);
}

static int jcs_serialize(const cJSON *node, jcs_buf_t *b);

static int jcs_serialize_object(const cJSON *obj, jcs_buf_t *b)
{
	const cJSON *child;
	const char **keys;
	int count;
	int i;
	int first;

	if (!obj || !b)
		return -1;
	count = 0;
	for (child = obj->child; child != NULL; child = child->next)
		count++;
	if (count == 0)
		return jcs_buf_append_cstr(b, "{}");
	keys = (const char **)calloc((size_t)count, sizeof(const char *));
	if (!keys)
		return -1;
	i = 0;
	for (child = obj->child; child != NULL; child = child->next) {
		if (!child->string) {
			free(keys);
			return -1;
		}
		keys[i++] = child->string;
	}
	qsort(keys, (size_t)count, sizeof(const char *), jcs_key_cmp);
	if (jcs_buf_append_byte(b, '{') != 0) {
		free(keys);
		return -1;
	}
	first = 1;
	for (i = 0; i < count; i++) {
		child = cJSON_GetObjectItemCaseSensitive(obj, keys[i]);
		if (!child) {
			free(keys);
			return -1;
		}
		if (!first) {
			if (jcs_buf_append_byte(b, ',') != 0) {
				free(keys);
				return -1;
			}
		}
		first = 0;
		if (jcs_append_escaped_string(b, keys[i]) != 0) {
			free(keys);
			return -1;
		}
		if (jcs_buf_append_byte(b, ':') != 0) {
			free(keys);
			return -1;
		}
		if (jcs_serialize(child, b) != 0) {
			free(keys);
			return -1;
		}
	}
	free(keys);
	return jcs_buf_append_byte(b, '}');
}

static int jcs_serialize_array(const cJSON *arr, jcs_buf_t *b)
{
	const cJSON *child;
	int first;

	if (!arr || !b)
		return -1;
	if (jcs_buf_append_byte(b, '[') != 0)
		return -1;
	first = 1;
	for (child = arr->child; child != NULL; child = child->next) {
		if (!first) {
			if (jcs_buf_append_byte(b, ',') != 0)
				return -1;
		}
		first = 0;
		if (jcs_serialize(child, b) != 0)
			return -1;
	}
	return jcs_buf_append_byte(b, ']');
}

static int jcs_serialize(const cJSON *node, jcs_buf_t *b)
{
	if (!node || !b)
		return -1;
	if (cJSON_IsNull(node))
		return jcs_buf_append_cstr(b, "null");
	if (cJSON_IsFalse(node))
		return jcs_buf_append_cstr(b, "false");
	if (cJSON_IsTrue(node))
		return jcs_buf_append_cstr(b, "true");
	if (cJSON_IsNumber(node))
		return jcs_append_number(b, node->valuedouble);
	if (cJSON_IsString(node))
		return jcs_append_escaped_string(b, node->valuestring);
	if (cJSON_IsArray(node))
		return jcs_serialize_array(node, b);
	if (cJSON_IsObject(node))
		return jcs_serialize_object(node, b);
	return -1;
}

int jcs_canonicalize(const cJSON *root, unsigned char **out, size_t *out_len)
{
	jcs_buf_t b;

	if (!root || !out || !out_len)
		return -1;
	memset(&b, 0, sizeof(b));
	if (jcs_serialize(root, &b) != 0) {
		free(b.buf);
		return -1;
	}
	*out = b.buf;
	*out_len = b.len;
	return 0;
}
