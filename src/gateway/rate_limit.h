/**
 * @file rate_limit.h
 * @brief Per-IP sliding-window rate limiter for the ASAP endpoint.
 *
 * The table is fixed-size (RATE_LIMIT_TABLE_SIZE entries) and uses linear
 * probing.  All functions accept an explicit @p now parameter so tests can
 * inject a fake clock without relying on wall-clock delays.
 *
 * Thread-safety: all public functions are protected by an internal
 * pthread mutex.  Callers do NOT need to hold any lock.
 */
#ifndef SHELLCLAW_GATEWAY_RATE_LIMIT_H
#define SHELLCLAW_GATEWAY_RATE_LIMIT_H

#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Number of requests allowed per window for /asap. */
#define ASAP_RATE_LIMIT_RPM 10

/** Window size in seconds (60 = per-minute). */
#define ASAP_RATE_WINDOW_SECS 60

/**
 * Check whether @p ip has exceeded the /asap rate limit and record the request.
 *
 * If the IP has fewer than ASAP_RATE_LIMIT_RPM requests in the last
 * ASAP_RATE_WINDOW_SECS seconds, the counter is incremented and 0 is
 * returned.  Otherwise the counter is NOT incremented and 1 is returned.
 *
 * @param ip   Client IP string (NULL or empty is treated as "unknown").
 * @param now  Current time (use time(NULL) in production; inject for tests).
 * @return     0 if the request is allowed, 1 if the limit is exceeded.
 */
int rate_limit_asap(const char *ip, time_t now);

/**
 * Reset all rate-limit counters (for use between unit tests).
 */
void rate_limit_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_GATEWAY_RATE_LIMIT_H */
