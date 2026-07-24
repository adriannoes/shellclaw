/**
 * @file config_patch.c
 * @brief Patch on-disk TOML from dashboard JSON updates.
 */
#define _POSIX_C_SOURCE 200809L

#include "core/config_patch.h"
#include "core/config.h"
#include "cJSON.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PATCH_ERR(errbuf, errbufsz, msg)                                       \
	do {                                                                       \
		if ((errbuf) && (errbufsz) > 0)                                        \
			snprintf((errbuf), (errbufsz), "%s", (msg));                       \
	} while (0)

static char *read_file(const char *path, size_t *out_len, char *errbuf, size_t errbufsz)
{
	FILE *f;
	char *buf;
	long n;
	size_t got;

	if (!path || !out_len) {
		PATCH_ERR(errbuf, errbufsz, "invalid arguments");
		return NULL;
	}
	f = fopen(path, "r");
	if (!f) {
		PATCH_ERR(errbuf, errbufsz, "cannot open config file");
		return NULL;
	}
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		PATCH_ERR(errbuf, errbufsz, "cannot read config file");
		return NULL;
	}
	n = ftell(f);
	if (n < 0) {
		fclose(f);
		PATCH_ERR(errbuf, errbufsz, "cannot read config file");
		return NULL;
	}
	if (fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		PATCH_ERR(errbuf, errbufsz, "cannot read config file");
		return NULL;
	}
	buf = malloc((size_t)n + 1);
	if (!buf) {
		fclose(f);
		PATCH_ERR(errbuf, errbufsz, "out of memory");
		return NULL;
	}
	got = fread(buf, 1, (size_t)n, f);
	fclose(f);
	if (got != (size_t)n) {
		free(buf);
		PATCH_ERR(errbuf, errbufsz, "cannot read config file");
		return NULL;
	}
	buf[got] = '\0';
	*out_len = got;
	return buf;
}

static int append_fmt(char **buf, size_t *len, size_t *cap, const char *fmt, ...)
{
	va_list ap;
	int need;
	char stack[256];
	char *heap = NULL;
	char *target;

	if (!buf || !*buf || !len || !cap)
		return -1;
	va_start(ap, fmt);
	need = vsnprintf(stack, sizeof(stack), fmt, ap);
	va_end(ap);
	if (need < 0)
		return -1;
	if ((size_t)need < sizeof(stack)) {
		target = stack;
	} else {
		heap = malloc((size_t)need + 1);
		if (!heap)
			return -1;
		va_start(ap, fmt);
		(void)vsnprintf(heap, (size_t)need + 1, fmt, ap);
		va_end(ap);
		target = heap;
	}
	if (*len + (size_t)need + 1 > *cap) {
		size_t new_cap = (*cap == 0) ? (size_t)need + 64 : *cap;
		while (new_cap < *len + (size_t)need + 1)
			new_cap *= 2;
		char *grown = realloc(*buf, new_cap);
		if (!grown) {
			free(heap);
			return -1;
		}
		*buf = grown;
		*cap = new_cap;
	}
	memcpy(*buf + *len, target, (size_t)need);
	*len += (size_t)need;
	(*buf)[*len] = '\0';
	free(heap);
	return 0;
}

static int escape_toml_string(const char *in, char **out)
{
	size_t cap;
	size_t len;
	size_t i;

	if (!in || !out)
		return -1;
	cap = strlen(in) * 2 + 3;
	*out = malloc(cap);
	if (!*out)
		return -1;
	(*out)[0] = '"';
	len = 1;
	for (i = 0; in[i]; i++) {
		if (in[i] == '"' || in[i] == '\\') {
			if (len + 2 >= cap) {
				cap *= 2;
				char *grown = realloc(*out, cap);
				if (!grown) {
					free(*out);
					*out = NULL;
					return -1;
				}
				*out = grown;
			}
			(*out)[len++] = '\\';
		}
		if (len + 1 >= cap) {
			cap *= 2;
			char *grown = realloc(*out, cap);
			if (!grown) {
				free(*out);
				*out = NULL;
				return -1;
			}
			*out = grown;
		}
		(*out)[len++] = in[i];
	}
	(*out)[len++] = '"';
	(*out)[len] = '\0';
	return 0;
}

static const char *find_section(const char *content, const char *section)
{
	char marker[128];
	size_t marker_len;
	const char *p;

	if (!content || !section)
		return NULL;
	snprintf(marker, sizeof(marker), "[%s]", section);
	marker_len = strlen(marker);
	for (p = content; *p; p++) {
		if (strncmp(p, marker, marker_len) != 0)
			continue;
		if (p != content && p[-1] != '\n')
			continue;
		if (p[marker_len] != '\0' && p[marker_len] != '\r' && p[marker_len] != '\n')
			continue;
		return p;
	}
	return NULL;
}

static const char *section_end(const char *section_start)
{
	const char *p;

	if (!section_start)
		return NULL;
	p = strchr(section_start + 1, '\n');
	if (!p)
		return section_start + strlen(section_start);
	for (; *p; p++) {
		if (*p == '[' && (p == section_start || p[-1] == '\n'))
			return p;
	}
	return section_start + strlen(section_start);
}

static const char *find_key_line(const char *section_start, const char *section_end,
                                 const char *key, size_t *line_len)
{
	size_t key_len;
	const char *p;

	if (!section_start || !section_end || !key || !line_len)
		return NULL;
	key_len = strlen(key);
	for (p = section_start; p < section_end; p++) {
		const char *line_end = strchr(p, '\n');
		size_t span;

		if (!line_end || line_end > section_end)
			line_end = section_end;
		span = (size_t)(line_end - p);
		while (span > 0 && isspace((unsigned char)p[span - 1]))
			span--;
		if (span > key_len) {
			const char *after_key = p + key_len;
			while (after_key < line_end &&
			       (*after_key == ' ' || *after_key == '\t'))
				after_key++;
			if (strncmp(p, key, key_len) == 0 && after_key < line_end &&
			    *after_key == '=') {
				*line_len = (size_t)(line_end - p);
				if (*line_end == '\n')
					(*line_len)++;
				return p;
			}
		}
		if (!*line_end)
			break;
		p = line_end;
	}
	return NULL;
}

static int patch_key_line(char **content, size_t *len, size_t *cap, const char *section,
                          const char *key, const char *line_value)
{
	const char *sec;
	const char *sec_end;
	const char *line;
	size_t line_len;
	size_t prefix_len;
	size_t suffix_len;
	char *replacement;
	size_t replacement_len;
	char *next;

	if (!content || !*content || !len || !cap || !section || !key || !line_value)
		return -1;
	sec = find_section(*content, section);
	if (!sec) {
		return append_fmt(content, len, cap, "\n[%s]\n%s = %s\n", section, key, line_value);
	}
	sec_end = section_end(sec);
	line = find_key_line(sec, sec_end, key, &line_len);
	if (!line) {
		const char *insert_at = sec_end;
		size_t insert_off = (size_t)(insert_at - *content);
		char insert_line[512];

		snprintf(insert_line, sizeof(insert_line), "%s = %s\n", key, line_value);
		replacement_len = strlen(insert_line);
		next = malloc(*len + replacement_len + 1);
		if (!next)
			return -1;
		memcpy(next, *content, insert_off);
		memcpy(next + insert_off, insert_line, replacement_len);
		memcpy(next + insert_off + replacement_len, *content + insert_off,
		       *len - insert_off + 1);
		free(*content);
		*content = next;
		*len += replacement_len;
		if (*len + 1 > *cap)
			*cap = *len + 1;
		return 0;
	}
	prefix_len = (size_t)(line - *content);
	suffix_len = *len - prefix_len - line_len;
	replacement_len = strlen(key) + strlen(line_value) + 8;
	replacement = malloc(replacement_len);
	if (!replacement)
		return -1;
	snprintf(replacement, replacement_len, "%s = %s\n", key, line_value);
	replacement_len = strlen(replacement);
	next = malloc(prefix_len + replacement_len + suffix_len + 1);
	if (!next) {
		free(replacement);
		return -1;
	}
	memcpy(next, *content, prefix_len);
	memcpy(next + prefix_len, replacement, replacement_len);
	memcpy(next + prefix_len + replacement_len, *content + prefix_len + line_len, suffix_len + 1);
	free(replacement);
	free(*content);
	*content = next;
	*len = prefix_len + replacement_len + suffix_len;
	if (*len + 1 > *cap)
		*cap = *len + 1;
	return 0;
}

static int patch_string_field(char **content, size_t *len, size_t *cap, const char *section,
                              const char *key, const char *value)
{
	char *escaped;

	if (!value)
		return 0;
	if (escape_toml_string(value, &escaped) != 0)
		return -1;
	if (patch_key_line(content, len, cap, section, key, escaped) != 0) {
		free(escaped);
		return -1;
	}
	free(escaped);
	return 0;
}

static int patch_int_field(char **content, size_t *len, size_t *cap, const char *section,
                           const char *key, int value, int has_value)
{
	char buf[32];

	if (!has_value)
		return 0;
	snprintf(buf, sizeof(buf), "%d", value);
	return patch_key_line(content, len, cap, section, key, buf);
}

static int patch_double_field(char **content, size_t *len, size_t *cap, const char *section,
                              const char *key, double value, int has_value)
{
	char buf[32];

	if (!has_value)
		return 0;
	snprintf(buf, sizeof(buf), "%g", value);
	return patch_key_line(content, len, cap, section, key, buf);
}

int config_patch_dashboard_json(const char *config_path, const char *json_body, char **out_toml,
                                size_t *out_len, char *errbuf, size_t errbufsz)
{
	cJSON *root;
	size_t cap;
	char *content;
	size_t len;
	config_t *cfg = NULL;
	char tmp_path[512];
	FILE *f;

	if (!config_path || !json_body || !out_toml || !out_len) {
		PATCH_ERR(errbuf, errbufsz, "invalid arguments");
		return -1;
	}
	*out_toml = NULL;
	*out_len = 0;
	root = cJSON_Parse(json_body);
	if (!root || !cJSON_IsObject(root)) {
		cJSON_Delete(root);
		PATCH_ERR(errbuf, errbufsz, "invalid JSON body");
		return -1;
	}
	content = read_file(config_path, &len, errbuf, errbufsz);
	if (!content) {
		cJSON_Delete(root);
		return -1;
	}
	cap = len + 1;
	{
		cJSON *model = cJSON_GetObjectItem(root, "model");
		cJSON *max_tokens = cJSON_GetObjectItem(root, "max_tokens");
		cJSON *temperature = cJSON_GetObjectItem(root, "temperature");
		cJSON *gateway_host = cJSON_GetObjectItem(root, "gateway_host");
		cJSON *gateway_port = cJSON_GetObjectItem(root, "gateway_port");

		if (model && cJSON_IsString(model) &&
		    patch_string_field(&content, &len, &cap, "agent", "model", model->valuestring) != 0)
			goto fail;
		if (max_tokens && cJSON_IsNumber(max_tokens) &&
		    patch_int_field(&content, &len, &cap, "agent", "max_tokens", max_tokens->valueint,
		                    1) != 0)
			goto fail;
		if (temperature && cJSON_IsNumber(temperature) &&
		    patch_double_field(&content, &len, &cap, "agent", "temperature",
		                        temperature->valuedouble, 1) != 0)
			goto fail;
		if (gateway_host && cJSON_IsString(gateway_host) &&
		    patch_string_field(&content, &len, &cap, "gateway", "host",
		                       gateway_host->valuestring) != 0)
			goto fail;
		if (gateway_port && cJSON_IsNumber(gateway_port) &&
		    patch_int_field(&content, &len, &cap, "gateway", "port", gateway_port->valueint, 1) !=
		        0)
			goto fail;
	}
	snprintf(tmp_path, sizeof(tmp_path), "%s.patch-test", config_path);
	f = fopen(tmp_path, "w");
	if (!f) {
		PATCH_ERR(errbuf, errbufsz, "failed to validate patched config");
		goto fail;
	}
	if (fwrite(content, 1, len, f) != len) {
		fclose(f);
		unlink(tmp_path);
		PATCH_ERR(errbuf, errbufsz, "failed to validate patched config");
		goto fail;
	}
	fclose(f);
	if (config_load(tmp_path, &cfg, errbuf, errbufsz) != 0) {
		unlink(tmp_path);
		goto fail;
	}
	config_free(cfg);
	unlink(tmp_path);
	*out_toml = content;
	*out_len = len;
	cJSON_Delete(root);
	return 0;
fail:
	config_free(cfg);
	free(content);
	cJSON_Delete(root);
	return -1;
}
