/**
 * @file web_search.h
 * @brief Web search tool: Brave Search (when key set) or DuckDuckGo fallback.
 */

#ifndef SHELLCLAW_TOOL_WEB_SEARCH_H
#define SHELLCLAW_TOOL_WEB_SEARCH_H

#include "tools/tool.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Set config for provider selection (Brave if key env set, else DuckDuckGo). */
void tool_web_search_set_config(const config_t *cfg);

const tool_t *tool_web_search_get(void);

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_TOOL_WEB_SEARCH_H */
