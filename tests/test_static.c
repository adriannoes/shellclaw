/**
 * @file test_static.c
 * @brief Unit tests for static asset lookup.
 */
#define _POSIX_C_SOURCE 200809L

#include "gateway/static.h"
#include <stdio.h>
#include <string.h>

static int test_static_lookup_exact(void)
{
	const unsigned char *data = NULL;
	size_t len = 0;
	const char *ct = NULL;
	if (static_lookup("/", &data, &len, &ct) != 0) return 1;
	if (!data || len == 0 || !ct) return 2;
	if (strcmp(ct, "text/html") != 0) return 3;
	if (data[0] != 0x1f || data[1] != 0x8b) return 4; /* gzip magic */
	return 0;
}

static int test_static_lookup_css(void)
{
	const unsigned char *data = NULL;
	size_t len = 0;
	const char *ct = NULL;
	if (static_lookup("/css/style.css", &data, &len, &ct) != 0) return 1;
	if (!data || len == 0 || !ct) return 2;
	if (strcmp(ct, "text/css") != 0) return 3;
	return 0;
}

static int test_static_lookup_js(void)
{
	const unsigned char *data = NULL;
	size_t len = 0;
	const char *ct = NULL;
	if (static_lookup("/js/app.js", &data, &len, &ct) != 0) return 1;
	if (!data || len == 0 || !ct) return 2;
	if (strcmp(ct, "application/javascript") != 0) return 3;
	return 0;
}

static int test_static_lookup_spa_fallback(void)
{
	const unsigned char *data = NULL;
	size_t len = 0;
	const char *ct = NULL;
	if (static_lookup("/unknown/path", &data, &len, &ct) != 0) return 1;
	if (!data || len == 0 || !ct) return 2;
	if (strcmp(ct, "text/html") != 0) return 3;
	return 0;
}

static int test_static_lookup_null(void)
{
	const unsigned char *data = NULL;
	size_t len = 0;
	const char *ct = NULL;
	if (static_lookup(NULL, &data, &len, &ct) == 0) return 1;
	return 0;
}

int main(void)
{
	int failed = 0;
	if (test_static_lookup_exact() != 0) {
		fprintf(stderr, "FAIL: test_static_lookup_exact\n");
		failed++;
	}
	if (test_static_lookup_css() != 0) {
		fprintf(stderr, "FAIL: test_static_lookup_css\n");
		failed++;
	}
	if (test_static_lookup_js() != 0) {
		fprintf(stderr, "FAIL: test_static_lookup_js\n");
		failed++;
	}
	if (test_static_lookup_spa_fallback() != 0) {
		fprintf(stderr, "FAIL: test_static_lookup_spa_fallback\n");
		failed++;
	}
	if (test_static_lookup_null() != 0) {
		fprintf(stderr, "FAIL: test_static_lookup_null\n");
		failed++;
	}
	if (failed > 0) return 1;
	printf("test_static: all passed\n");
	return 0;
}
