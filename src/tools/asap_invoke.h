/**
 * @file asap_invoke.h
 * @brief asap_invoke tool: delegate tasks to remote ASAP agents via registry lookup.
 *
 * Parameters accepted by the tool execute function (JSON object):
 *   - "urn"      (string, required)  — target agent URN.
 *   - "skill_id" (string, optional)  — specific skill to invoke on that agent.
 *   - "input"    (object, required)  — free-form task input passed as payload.
 *
 * Ownership: the module holds a borrowed const config_t* set via
 * tool_asap_invoke_set_config(); the caller must ensure the config outlives
 * all tool calls.
 */
#ifndef SHELLCLAW_TOOLS_ASAP_INVOKE_H
#define SHELLCLAW_TOOLS_ASAP_INVOKE_H

#include "tools/tool.h"

#ifdef __cplusplus
extern "C" {
#endif

struct config;
typedef struct config config_t;

/** Return the asap_invoke tool_t (statically allocated; never NULL). */
const tool_t *tool_asap_invoke_get(void);

/**
 * Bind a config handle to the asap_invoke module. Must be called (at least
 * once) before any agent invocations so the module knows the registry URL,
 * revocation URL, client timeout, and sender URN.
 *
 * @param cfg  Borrowed config handle; must outlive all tool calls.
 */
void tool_asap_invoke_set_config(const config_t *cfg);

#ifdef SHELLCLAW_ASAP_INVOKE_TEST
#include "asap/envelope.h"
#include "asap/client.h"

/**
 * Signature of the send function used internally by asap_invoke.
 * Matches asap_client_send_task exactly so the real implementation is the default.
 */
typedef int (*asap_invoke_send_fn)(const char *url, const char *bearer,
        const char *method, const asap_envelope_t *env,
        const asap_client_config_t *client, const config_t *cfg,
        asap_envelope_t *response, char *errbuf, size_t errlen);

/**
 * Override the HTTP send function used by asap_invoke (unit tests only).
 * Pass NULL to restore the real asap_client_send_task.
 *
 * @param fn  Replacement function, or NULL to use the real implementation.
 */
void asap_invoke_test_set_send_fn(asap_invoke_send_fn fn);

/** Reset the internal registry cache so tests start clean (unit tests only). */
void asap_invoke_test_reset_cache(void);

/**
 * Pre-load a registry JSON body into the internal cache for @a url (unit tests only).
 * Use this together with #asap_invoke_test_set_send_fn to exercise the full
 * execute path without any real HTTP.
 *
 * @param url   URL string to associate with the loaded cache entry.
 * @param json  Registry JSON body (parsed via registry_cache_test_load_json).
 * @return 0 on success, -1 on parse error.
 */
int asap_invoke_test_load_registry(const char *url, const char *json);

/**
 * Override the registry URL used by asap_invoke (unit tests only).
 * Takes priority over the URL from config when non-NULL.
 * Pass NULL to clear the override and revert to the config-derived URL.
 *
 * @param url  URL string (borrowed; must outlive the tool calls in the test).
 */
void asap_invoke_test_set_registry_url(const char *url);
#endif /* SHELLCLAW_ASAP_INVOKE_TEST */

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_TOOLS_ASAP_INVOKE_H */
