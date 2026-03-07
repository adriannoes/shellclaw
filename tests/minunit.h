/**
 * @file minunit.h
 * @brief Minimal unit testing macros for C. Header-only, no external deps.
 *
 * Usage:
 *   static int tests_run = 0, tests_failed = 0;
 *   #define MU_ASSERT(cond, msg) ...
 *   #define MU_RUN(test) ...
 *   Or include this header after defining MU_TEST_SCOPE.
 *
 * Compatible with existing test_*.c pattern (tests_run, tests_failed, MU_ASSERT, MU_RUN).
 */
#ifndef SHELLCLAW_MINUNIT_H
#define SHELLCLAW_MINUNIT_H

#include <stdio.h>

#ifndef MU_ASSERT
#define MU_ASSERT(cond, msg) do { \
	tests_run++; \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", (msg)); \
		tests_failed++; \
		return; \
	} \
} while (0)
#endif

#ifndef MU_RUN
#define MU_RUN(test) do { test(); } while (0)
#endif

#endif /* SHELLCLAW_MINUNIT_H */
