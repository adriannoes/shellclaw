/**
 * @file cron.h
 * @brief Cron scheduler: parse schedules, next_run, trigger injection via cron channel.
 */

#ifndef SHELLCLAW_CRON_H
#define SHELLCLAW_CRON_H

#include "channels/channel.h"
#include "tools/tool.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Parse schedule and compute next_run from current time.
 * Formats: "cron:min hour dom month dow", "interval:N", "at:unix_ts".
 * Cron: 5 fields, * or N or N-M. dow 0-6 (Sun-Sat).
 *
 * @param schedule Schedule string.
 * @param now      Current Unix timestamp.
 * @param next_out Output: next run time.
 * @return 0 on success, -1 on parse error.
 */
int cron_parse_next_run(const char *schedule, long long now, long long *next_out);

/**
 * Check if schedule is one-shot (at:ts). One-shot jobs are deleted after run.
 *
 * @param schedule Schedule string.
 * @return 1 if one-shot, 0 otherwise.
 */
int cron_is_one_shot(const char *schedule);

/**
 * Get the cron channel (poll returns due jobs, send routes to target channel).
 */
const channel_t *channel_cron_get(void);

/** Get the cron tool for agent (list, create, delete, toggle jobs). */
const tool_t *tool_cron_get(void);

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_CRON_H */
