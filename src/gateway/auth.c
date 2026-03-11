/**
 * @file auth.c
 * @brief Pairing code generation and bearer token store (JSON array).
 */
#define _POSIX_C_SOURCE 200809L

#include "gateway/auth.h"
#include "cJSON.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_TOKENS_PATH "~/.shellclaw/auth_tokens.json"
#define PAIRING_CODE_LEN 6
#define TOKEN_LEN 32

struct auth_ctx {
	char *tokens_path;
	char *pending_pairing_code;
};

static char *expand_tilde(const char *path)
{
	if (!path || path[0] != '~') return path ? strdup(path) : NULL;
	const char *home = getenv("HOME");
	if (!home) home = "";
	size_t hlen = strlen(home);
	size_t tail = strlen(path + 1);
	char *out = malloc(hlen + tail + 1);
	if (!out) return NULL;
	memcpy(out, home, hlen);
	memcpy(out + hlen, path + 1, tail + 1);
	return out;
}

static int is_file_empty_or_missing(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) return 1;
	int c = fgetc(f);
	fclose(f);
	return (c == EOF);
}

static int read_urandom(unsigned char *out, size_t n)
{
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0) return -1;
	ssize_t r = read(fd, out, n);
	close(fd);
	return (r == (ssize_t)n) ? 0 : -1;
}

static int generate_random_hex(char *out, size_t len)
{
	if (!out || len == 0) return -1;
	size_t raw_len = (len + 1) / 2;
	unsigned char *raw = malloc(raw_len);
	if (!raw) return -1;
	if (read_urandom(raw, raw_len) != 0) {
		free(raw);
		return -1;
	}
	for (size_t i = 0; i < len; i++) {
		int v = (i % 2 == 0) ? (raw[i / 2] >> 4) : (raw[i / 2] & 0x0F);
		out[i] = (char)(v < 10 ? '0' + v : 'a' + v - 10);
	}
	out[len] = '\0';
	free(raw);
	return 0;
}

static int generate_pairing_code(char *out, size_t out_size)
{
	if (!out || out_size < (size_t)(PAIRING_CODE_LEN + 1)) return -1;
	unsigned char raw[PAIRING_CODE_LEN];
	if (read_urandom(raw, sizeof(raw)) != 0) return -1;
	for (int i = 0; i < PAIRING_CODE_LEN; i++)
		out[i] = (char)('0' + (raw[i] % 10));
	out[PAIRING_CODE_LEN] = '\0';
	return 0;
}

static int constant_time_cmp(const char *a, const char *b, size_t len)
{
	volatile unsigned char result = 0;
	for (size_t i = 0; i < len; i++)
		result |= (unsigned char)a[i] ^ (unsigned char)b[i];
	return result == 0;
}

static int is_valid_6digit(const char *code)
{
	if (!code) return 0;
	for (int i = 0; i < PAIRING_CODE_LEN; i++) {
		if (code[i] < '0' || code[i] > '9') return 0;
	}
	return code[PAIRING_CODE_LEN] == '\0';
}

auth_ctx_t *auth_init(const char *tokens_path)
{
	auth_ctx_t *ctx = calloc(1, sizeof(*ctx));
	if (!ctx) return NULL;
	if (tokens_path && tokens_path[0] != '\0') {
		ctx->tokens_path = strdup(tokens_path);
	} else {
		ctx->tokens_path = expand_tilde(DEFAULT_TOKENS_PATH);
	}
	if (!ctx->tokens_path) {
		free(ctx);
		return NULL;
	}
	return ctx;
}

void auth_cleanup(auth_ctx_t *ctx)
{
	if (!ctx) return;
	free(ctx->tokens_path);
	free(ctx->pending_pairing_code);
	free(ctx);
}

char *auth_get_or_create_pairing_code(auth_ctx_t *ctx)
{
	if (!ctx || !ctx->tokens_path) return NULL;
	if (!is_file_empty_or_missing(ctx->tokens_path)) return NULL;
	free(ctx->pending_pairing_code);
	ctx->pending_pairing_code = NULL;
	char code[PAIRING_CODE_LEN + 1];
	if (generate_pairing_code(code, sizeof(code)) != 0) return NULL;
	ctx->pending_pairing_code = strdup(code);
	if (!ctx->pending_pairing_code) return NULL;
	printf("ShellClaw pairing code: %s\n", code);
	return strdup(code);
}

static int ensure_tokens_dir(const char *path)
{
	char *copy = strdup(path);
	if (!copy) return -1;
	char *slash = strrchr(copy, '/');
	if (slash) {
		*slash = '\0';
		if (slash != copy) {
			if (mkdir(copy, 0700) != 0 && errno != EEXIST) {
				free(copy);
				return -1;
			}
		}
	}
	free(copy);
	return 0;
}

int auth_pair(auth_ctx_t *ctx, const char *code, char *token_out, size_t token_size)
{
	if (!ctx || !ctx->tokens_path || !code || !token_out || token_size == 0) return -1;
	if (!is_valid_6digit(code)) return -1;
	if (!ctx->pending_pairing_code || strcmp(code, ctx->pending_pairing_code) != 0)
		return -1;
	char new_token[TOKEN_LEN + 1];
	generate_random_hex(new_token, TOKEN_LEN);
	cJSON *arr = cJSON_CreateArray();
	if (!arr) return -1;
	cJSON_AddItemToArray(arr, cJSON_CreateString(new_token));
	char *json = cJSON_PrintUnformatted(arr);
	cJSON_Delete(arr);
	if (!json) return -1;
	if (ensure_tokens_dir(ctx->tokens_path) != 0) {
		free(json);
		return -1;
	}
	int fd = open(ctx->tokens_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) {
		free(json);
		return -1;
	}
	FILE *out = fdopen(fd, "w");
	if (!out) {
		close(fd);
		free(json);
		return -1;
	}
	fprintf(out, "%s", json);
	fclose(out);
	free(json);
	size_t copy_len = (size_t)TOKEN_LEN < token_size - 1 ? (size_t)TOKEN_LEN : token_size - 1;
	memcpy(token_out, new_token, copy_len);
	token_out[copy_len] = '\0';
	free(ctx->pending_pairing_code);
	ctx->pending_pairing_code = NULL;
	return 0;
}

int auth_validate_token(auth_ctx_t *ctx, const char *token)
{
	if (!ctx || !ctx->tokens_path || !token || token[0] == '\0') return 0;
	FILE *f = fopen(ctx->tokens_path, "r");
	if (!f) return 0;
	char buf[8192];
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = '\0';
	cJSON *root = cJSON_Parse(buf);
	if (!root || !cJSON_IsArray(root)) {
		if (root) cJSON_Delete(root);
		return 0;
	}
	int found = 0;
	int count = cJSON_GetArraySize(root);
	size_t tok_len = strlen(token);
	for (int i = 0; i < count && !found; i++) {
		cJSON *item = cJSON_GetArrayItem(root, i);
		if (cJSON_IsString(item) && item->valuestring &&
		    strlen(item->valuestring) == tok_len &&
		    constant_time_cmp(item->valuestring, token, tok_len))
			found = 1;
	}
	cJSON_Delete(root);
	return found ? 1 : 0;
}
