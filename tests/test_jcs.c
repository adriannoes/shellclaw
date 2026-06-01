/**
 * @file test_jcs.c
 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "test_runner.h"
#include "crypto/jcs.h"
#include "cJSON.h"
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int test_jcs_canonicalize_sorted_keys(void)
{
	cJSON *root;
	unsigned char *out;
	size_t out_len;
	const char *expect = "{\"a\":1,\"z\":2}";

	root = cJSON_Parse("{\"z\":2,\"a\":1}");
	ASSERT(root != NULL);
	ASSERT(jcs_canonicalize(root, &out, &out_len) == 0);
	ASSERT(out_len == strlen(expect));
	ASSERT(memcmp(out, expect, out_len) == 0);
	free(out);
	cJSON_Delete(root);
	return 0;
}

static int test_jcs_escapes_primitives_and_arrays(void)
{
	cJSON *root;
	unsigned char *out;
	size_t out_len;
	const char *expect =
	    "{\"arr\":[null,true,false,42,1.5,\"a\\tb\\nc\"],\"empty\":{}}";

	root = cJSON_Parse(
	    "{\"empty\":{},\"arr\":[null,true,false,42,1.5,\"a\\tb\\nc\"]}");
	ASSERT(root != NULL);
	ASSERT(jcs_canonicalize(root, &out, &out_len) == 0);
	ASSERT(out_len == strlen(expect));
	ASSERT(memcmp(out, expect, out_len) == 0);
	free(out);
	cJSON_Delete(root);
	return 0;
}

static int test_jcs_rejects_invalid_inputs(void)
{
	cJSON *nan_node;
	unsigned char *out = NULL;
	size_t out_len = 0;

	ASSERT(jcs_canonicalize(NULL, &out, &out_len) == -1);
	nan_node = cJSON_CreateNumber(NAN);
	ASSERT(nan_node != NULL);
	ASSERT(jcs_canonicalize(nan_node, &out, &out_len) == -1);
	cJSON_Delete(nan_node);
	return 0;
}

static int test_jcs_rejects_non_finite_number(void)
{
	cJSON *root;
	unsigned char *out = NULL;
	size_t out_len = 0;

	root = cJSON_CreateNumber(INFINITY);
	ASSERT(root != NULL);
	ASSERT(jcs_canonicalize(root, &out, &out_len) == -1);
	cJSON_Delete(root);
	return 0;
}

static int test_jcs_string_escape_coverage(void)
{
	cJSON *root;
	unsigned char *out;
	size_t out_len;
	const char *expect =
	    "{\"ctl\":\"\\u0001\",\"q\":\"\\\"\\\\\\b\\f\\r\\t\"}";

	root = cJSON_Parse("{\"q\":\"\\\"\\\\\\b\\f\\r\\t\",\"ctl\":\"\\u0001\"}");
	ASSERT(root != NULL);
	ASSERT(jcs_canonicalize(root, &out, &out_len) == 0);
	ASSERT(out_len == strlen(expect));
	ASSERT(memcmp(out, expect, out_len) == 0);
	free(out);
	cJSON_Delete(root);
	return 0;
}

int main(int argc, char **argv)
{
	int failed = 0;
	(void)argc;
	(void)argv;
	if (test_jcs_canonicalize_sorted_keys() != 0) {
		fprintf(stderr, "test_jcs_canonicalize_sorted_keys failed\n");
		failed++;
	}
	if (test_jcs_escapes_primitives_and_arrays() != 0) {
		fprintf(stderr, "test_jcs_escapes_primitives_and_arrays failed\n");
		failed++;
	}
	if (test_jcs_rejects_invalid_inputs() != 0) {
		fprintf(stderr, "test_jcs_rejects_invalid_inputs failed\n");
		failed++;
	}
	if (test_jcs_rejects_non_finite_number() != 0) {
		fprintf(stderr, "test_jcs_rejects_non_finite_number failed\n");
		failed++;
	}
	if (test_jcs_string_escape_coverage() != 0) {
		fprintf(stderr, "test_jcs_string_escape_coverage failed\n");
		failed++;
	}
	if (failed == 0)
		printf("test_jcs: all tests passed\n");
	return failed;
}
