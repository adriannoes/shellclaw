/**
 * @file test_auth.c
 * @brief Unit tests for auth module: pairing code, token validation.
 */
#define _POSIX_C_SOURCE 200809L

#include "gateway/auth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
#define RUN(t) do { int r = (t); if (r) return r; } while (0)

static int test_auth_init_cleanup(void)
{
	auth_ctx_t *ctx = auth_init(NULL);
	ASSERT(ctx != NULL);
	auth_cleanup(ctx);
	auth_cleanup(NULL);
	return 0;
}

static int test_auth_init_custom_path(void)
{
	auth_ctx_t *ctx = auth_init("/tmp/shellclaw_test_tokens.json");
	ASSERT(ctx != NULL);
	auth_cleanup(ctx);
	return 0;
}

static int test_auth_get_pairing_code_when_empty(void)
{
	unlink("/tmp/shellclaw_test_tokens_empty.json");
	auth_ctx_t *ctx = auth_init("/tmp/shellclaw_test_tokens_empty.json");
	ASSERT(ctx != NULL);
	char *code = auth_get_or_create_pairing_code(ctx);
	ASSERT(code != NULL);
	ASSERT(strlen(code) == 6);
	for (int i = 0; i < 6; i++)
		ASSERT(code[i] >= '0' && code[i] <= '9');
	free(code);
	auth_cleanup(ctx);
	return 0;
}

static int test_auth_pair_valid_code(void)
{
	unlink("/tmp/shellclaw_test_tokens_pair.json");
	auth_ctx_t *ctx = auth_init("/tmp/shellclaw_test_tokens_pair.json");
	ASSERT(ctx != NULL);
	char *code = auth_get_or_create_pairing_code(ctx);
	ASSERT(code != NULL);
	char token[64] = {0};
	int ret = auth_pair(ctx, code, token, sizeof(token));
	ASSERT(ret == 0);
	ASSERT(strlen(token) > 0);
	ASSERT(auth_validate_token(ctx, token) == 1);
	free(code);
	auth_cleanup(ctx);
	unlink("/tmp/shellclaw_test_tokens_pair.json");
	return 0;
}

static int test_auth_pair_invalid_code(void)
{
	unlink("/tmp/shellclaw_test_tokens_invalid.json");
	auth_ctx_t *ctx = auth_init("/tmp/shellclaw_test_tokens_invalid.json");
	ASSERT(ctx != NULL);
	char *code = auth_get_or_create_pairing_code(ctx);
	ASSERT(code != NULL);
	char token[64] = {0};
	int ret = auth_pair(ctx, "000000", token, sizeof(token));
	ASSERT(ret != 0);
	ret = auth_pair(ctx, code, token, sizeof(token));
	ASSERT(ret == 0);
	free(code);
	auth_cleanup(ctx);
	unlink("/tmp/shellclaw_test_tokens_invalid.json");
	return 0;
}

static int test_auth_validate_token(void)
{
	unlink("/tmp/shellclaw_test_tokens_validate.json");
	auth_ctx_t *ctx = auth_init("/tmp/shellclaw_test_tokens_validate.json");
	ASSERT(ctx != NULL);
	ASSERT(auth_validate_token(ctx, NULL) == 0);
	ASSERT(auth_validate_token(ctx, "") == 0);
	ASSERT(auth_validate_token(ctx, "invalid") == 0);
	char *code = auth_get_or_create_pairing_code(ctx);
	ASSERT(code != NULL);
	char token[64] = {0};
	auth_pair(ctx, code, token, sizeof(token));
	ASSERT(auth_validate_token(ctx, token) == 1);
	free(code);
	auth_cleanup(ctx);
	unlink("/tmp/shellclaw_test_tokens_validate.json");
	return 0;
}

int main(void)
{
	int failed = 0;
	if (test_auth_init_cleanup() != 0) { fprintf(stderr, "test_auth_init_cleanup failed\n"); failed++; }
	if (test_auth_init_custom_path() != 0) { fprintf(stderr, "test_auth_init_custom_path failed\n"); failed++; }
	if (test_auth_get_pairing_code_when_empty() != 0) { fprintf(stderr, "test_auth_get_pairing_code_when_empty failed\n"); failed++; }
	if (test_auth_pair_valid_code() != 0) { fprintf(stderr, "test_auth_pair_valid_code failed\n"); failed++; }
	if (test_auth_pair_invalid_code() != 0) { fprintf(stderr, "test_auth_pair_invalid_code failed\n"); failed++; }
	if (test_auth_validate_token() != 0) { fprintf(stderr, "test_auth_validate_token failed\n"); failed++; }
	if (failed == 0)
		printf("test_auth: all tests passed\n");
	return failed;
}
