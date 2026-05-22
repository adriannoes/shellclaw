/**
 * @file test_runner.h
 * @brief Shared test helpers: temp paths, minunit macros.
 */
#ifndef SHELLCLAW_TEST_RUNNER_H
#define SHELLCLAW_TEST_RUNNER_H

#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "minunit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef ASSERT
#define ASSERT(c)                                                                                  \
	do {                                                                                       \
		if (!(c)) {                                                                        \
			fprintf(stderr, "FAIL: %s:%d %s\n", __FILE__, __LINE__, #c);                \
			return 1;                                                                \
		}                                                                                    \
	} while (0)
#endif

#ifndef RUN
#define RUN(t)                                                                                     \
	do {                                                                                       \
		int _r = (t);                                                                      \
		if (_r)                                                                            \
			return _r;                                                                 \
	} while (0)
#endif

/**
 * Create a unique temp file path under /tmp. Caller must unlink when done.
 * @param prefix  Filename prefix (e.g. "shellclaw_test_router").
 * @param out     Buffer for full path (must hold at least 128 bytes).
 * @param outsz   Size of out.
 * @return 0 on success, -1 on failure.
 */
static inline int test_runner_mkstemp_path(const char *prefix, char *out, size_t outsz)
{
	char tmpl[128];
	int fd;

	if (!prefix || !out || outsz < 32)
		return -1;
	snprintf(tmpl, sizeof(tmpl), "/tmp/%s_XXXXXX", prefix);
	fd = mkstemp(tmpl);
	if (fd < 0)
		return -1;
	close(fd);
	if (strlen(tmpl) + 1 > outsz) {
		unlink(tmpl);
		return -1;
	}
	memcpy(out, tmpl, strlen(tmpl) + 1);
	return 0;
}

/**
 * Create a unique temp directory under /tmp. Caller must rmdir contents and rmdir when done.
 * @param prefix  Directory name prefix.
 * @param out     Buffer for full path.
 * @param outsz   Size of out.
 * @return 0 on success, -1 on failure.
 */
static inline int test_runner_mkdtemp_path(const char *prefix, char *out, size_t outsz)
{
	char tmpl[128];

	if (!prefix || !out || outsz < 32)
		return -1;
	snprintf(tmpl, sizeof(tmpl), "/tmp/%s_XXXXXX", prefix);
	if (!mkdtemp(tmpl))
		return -1;
	if (strlen(tmpl) + 1 > outsz)
		return -1;
	memcpy(out, tmpl, strlen(tmpl) + 1);
	return 0;
}

#endif /* SHELLCLAW_TEST_RUNNER_H */
