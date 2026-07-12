/**
 * @file test_rate_limit.c
 * @brief Unit tests for rate_limit_asap with an injected fake clock.
 */
#define _POSIX_C_SOURCE 200809L

#include "gateway/rate_limit.h"
#include <stdio.h>
#include <string.h>

#define ASSERT(c) do { \
	if (!(c)) { \
		fprintf(stderr, "FAIL: %s:%d %s\n", __FILE__, __LINE__, #c); \
		return 1; \
	} \
} while (0)

static int test_allow_up_to_limit(void)
{
	int i;
	time_t now = 1000;
	rate_limit_reset();
	for (i = 0; i < ASAP_RATE_LIMIT_RPM; i++)
		ASSERT(rate_limit_asap("10.0.0.1", now) == 0);
	ASSERT(rate_limit_asap("10.0.0.1", now) == 1);
	return 0;
}

static int test_window_reset_allows_again(void)
{
	int i;
	time_t t0 = 2000;
	rate_limit_reset();
	for (i = 0; i < ASAP_RATE_LIMIT_RPM; i++)
		rate_limit_asap("10.0.0.2", t0);
	ASSERT(rate_limit_asap("10.0.0.2", t0) == 1);
	ASSERT(rate_limit_asap("10.0.0.2", t0 + ASAP_RATE_WINDOW_SECS) == 0);
	return 0;
}

static int test_different_ips_independent(void)
{
	int i;
	time_t now = 3000;
	rate_limit_reset();
	for (i = 0; i < ASAP_RATE_LIMIT_RPM; i++)
		rate_limit_asap("10.0.0.3", now);
	ASSERT(rate_limit_asap("10.0.0.3", now) == 1);
	ASSERT(rate_limit_asap("10.0.0.4", now) == 0);
	return 0;
}

static int test_null_ip_uses_unknown(void)
{
	int i;
	time_t now = 4000;
	rate_limit_reset();
	for (i = 0; i < ASAP_RATE_LIMIT_RPM; i++)
		ASSERT(rate_limit_asap(NULL, now) == 0);
	ASSERT(rate_limit_asap(NULL, now) == 1);
	ASSERT(rate_limit_asap("", now) == 1);
	return 0;
}

static int test_partial_window_not_reset(void)
{
	time_t t0 = 5000;
	rate_limit_reset();
	ASSERT(rate_limit_asap("10.0.0.5", t0) == 0);
	ASSERT(rate_limit_asap("10.0.0.5", t0 + ASAP_RATE_WINDOW_SECS - 1) == 0);
	return 0;
}

/* Must match RATE_LIMIT_TABLE_SIZE in src/gateway/rate_limit.c. */
#define TEST_RATE_LIMIT_TABLE_SIZE 64

/**
 * When the fixed table is full, find_or_create evicts the hash-index slot so
 * a new attacker IP is still tracked (and can be limited). Without this path,
 * a 65th IP would either fail open or leave rate limiting stuck.
 */
static int test_table_full_still_tracks_overflow_ip(void)
{
	int i;
	int j;
	time_t now = 7000;
	char ip[32];
	const char *overflow = "10.99.1.1";

	rate_limit_reset();
	for (i = 0; i < TEST_RATE_LIMIT_TABLE_SIZE; i++) {
		snprintf(ip, sizeof(ip), "10.70.%d.%d", i / 256, i % 256);
		for (j = 0; j < ASAP_RATE_LIMIT_RPM; j++)
			ASSERT(rate_limit_asap(ip, now) == 0);
		ASSERT(rate_limit_asap(ip, now) == 1);
	}

	/* 65th distinct IP must be admitted on a fresh post-eviction counter. */
	ASSERT(rate_limit_asap(overflow, now) == 0);
	for (j = 1; j < ASAP_RATE_LIMIT_RPM; j++)
		ASSERT(rate_limit_asap(overflow, now) == 0);
	ASSERT(rate_limit_asap(overflow, now) == 1);
	return 0;
}

int main(void)
{
	int r = 0;
	r |= test_allow_up_to_limit();
	r |= test_window_reset_allows_again();
	r |= test_different_ips_independent();
	r |= test_null_ip_uses_unknown();
	r |= test_partial_window_not_reset();
	r |= test_table_full_still_tracks_overflow_ip();
	if (r == 0) printf("test_rate_limit: all tests passed\n");
	return r;
}
