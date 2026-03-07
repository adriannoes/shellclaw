/**
 * @file shell.h
 * @brief Shell tool declaration.
 */

#ifndef SHELLCLAW_TOOL_SHELL_H
#define SHELLCLAW_TOOL_SHELL_H

#include "tools/tool.h"

#ifdef __cplusplus
extern "C" {
#endif

struct config;
typedef struct config config_t;

const tool_t *tool_shell_get(void);
void tool_shell_set_config(const config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_TOOL_SHELL_H */
