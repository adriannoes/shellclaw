/**
 * @file test_asap_ulid.c
 * @brief Unit tests for ULID generator.
 */
#define _POSIX_C_SOURCE 200809L

#include "asap/ulid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

static int charset_ok(const char *s, size_t len)
{
	const char *set = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
	for (size_t i = 0; i < len; i++) {
		if (!strchr(set, s[i])) return 0;
	}
	return 1;
}

static int test_len_and_charset(void)
{
	char b[32];
	char shortbuf[4];
	memset(b, 'X', sizeof b);
	ASSERT(ulid_generate(shortbuf, 2) == -1);
	ASSERT(ulid_generate(b, sizeof b) == 0);
	ASSERT(strlen(b) == (size_t)ULID_STRING_LEN);
	ASSERT(b[ULID_STRING_LEN] == '\0');
	ASSERT(charset_ok(b, ULID_STRING_LEN));
	return 0;
}

static int test_same_ms_monotonic(void)
{
	char a[32], b[32];
	int found = 0;
	/* Tight pair loop: with luck both fall in the same 10-char time prefix. */
	for (int n = 0; n < 500000 && !found; n++) {
		ASSERT(ulid_generate(a, sizeof a) == 0);
		ASSERT(ulid_generate(b, sizeof b) == 0);
		if (strncmp(a, b, 10) == 0) {
			ASSERT(strcmp(b, a) > 0);
			found = 1;
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	int failed = 0;
	if (test_len_and_charset() != 0) { fprintf(stderr, "test_len_and_charset failed\n"); failed++; }
	if (test_same_ms_monotonic() != 0) { fprintf(stderr, "test_same_ms_monotonic failed\n"); failed++; }
	if (failed == 0)
		printf("test_asap_ulid: all tests passed\n");
	return failed;
}
