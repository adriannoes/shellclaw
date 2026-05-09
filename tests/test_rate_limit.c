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

int main(void)
{
	int r = 0;
	r |= test_allow_up_to_limit();
	r |= test_window_reset_allows_again();
	r |= test_different_ips_independent();
	r |= test_null_ip_uses_unknown();
	r |= test_partial_window_not_reset();
	if (r == 0) printf("test_rate_limit: all tests passed\n");
	return r;
}
