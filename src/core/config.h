/** @file config.h. TOML path from CLI or default; env overrides file. Read-only after load. */

#ifndef SHELLCLAW_CONFIG_H
#define SHELLCLAW_CONFIG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque configuration handle. Allocated by config_load(), freed by config_free(). */
typedef struct config config_t;

/**
 * Load configuration from a TOML file.
 * Expands ~ in path. Environment variables override file values. Validates required
 * fields; on failure returns non-zero and optionally writes message to errbuf.
 *
 * @param path    Path to config file (e.g. ~/.shellclaw/config.toml).
 * @param out     On success, *out is set to allocated config_t; caller must config_free().
 * @param errbuf  Optional buffer for error message (may be NULL).
 * @param errbufsz Size of errbuf (ignored if errbuf is NULL).
 * @return 0 on success, non-zero on error.
 */
int config_load(const char *path, config_t **out, char *errbuf, size_t errbufsz);

/**
 * Free configuration. Safe to call with NULL.
 *
 * @param cfg Config to free (may be NULL).
 */
void config_free(config_t *cfg);

/* --- Getters (read-only; all return internal pointers or values) --- */

const char *config_agent_model(const config_t *c);
int config_agent_max_tokens(const config_t *c);
double config_agent_temperature(const config_t *c);
int config_agent_max_tool_iterations(const config_t *c);
int config_agent_max_context_messages(const config_t *c);
const char *config_agent_soul_path(const config_t *c);
const char *config_agent_identity_path(const config_t *c);
const char *config_agent_user_path(const config_t *c);

const char *config_default_provider(const config_t *c);
const char *config_provider_anthropic_api_key_env(const config_t *c);
const char *config_provider_openai_api_key_env(const config_t *c);
const char *config_provider_openai_endpoint(const config_t *c);

int config_telegram_enabled(const config_t *c);
const char *config_telegram_token_env(const config_t *c);
int config_telegram_allowed_users_count(const config_t *c);
const char *config_telegram_allowed_user(const config_t *c, int index);

const char *config_memory_db_path(const config_t *c);
const char *config_skills_dir(const config_t *c);
int config_workspace_only(const config_t *c);
const char *config_workspace_path(const config_t *c);
int config_shell_timeout_sec(const config_t *c);

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_CONFIG_H */
