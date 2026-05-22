/**
 * @file context.h
 * @brief get_context enrichment tool (geo, weather, holidays) plus dashboard snapshot getter.
 */

#ifndef SHELLCLAW_TOOLS_CONTEXT_H
#define SHELLCLAW_TOOLS_CONTEXT_H

#include "tools/tool.h"
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

struct config;
typedef struct config config_t;

void tool_context_set_config(const config_t *cfg);

/** Returns static tool descriptor for registry (name `get_context`). */
const tool_t *tool_context_get(void);

/**
 * Last merged JSON from get_context suitable for dashboards (subset + dashboard lines).
 * Non-blocking read of cached snapshot. Caller must free the returned pointer.
 *
 * @return Heap copy of JSON body, or NULL on allocation failure or empty fallback.
 */
char *tool_context_snapshot_json(void);

#ifdef SHELLCLAW_CONTEXT_TEST
void tool_context_test_reset(void);
void tool_context_test_set_unix_time(time_t t);
/** Use fixed responses instead of the network when non-NULL at perform time (prefix match on URL). */
void tool_context_test_set_http_bodies(const char *geo_json, const char *weather_json, const char *holidays_json);
/** Returns the geo lookup URL (for HTTPS assertions in tests). */
const char *tool_context_test_geo_url(void);
#endif /* SHELLCLAW_CONTEXT_TEST */

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_TOOLS_CONTEXT_H */
