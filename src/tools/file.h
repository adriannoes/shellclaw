/**
 * @file file.h
 * @brief File tool declaration.
 */

#ifndef SHELLCLAW_TOOL_FILE_H
#define SHELLCLAW_TOOL_FILE_H

#include "tools/tool.h"

#ifdef __cplusplus
extern "C" {
#endif

struct config;
typedef struct config config_t;

const tool_t *tool_file_get(void);
void tool_file_set_config(const config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_TOOL_FILE_H */
