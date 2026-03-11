/**
 * @file manifest.h
 * @brief ASAP manifest and health JSON builders for well-known discovery.
 */
#ifndef SHELLCLAW_ASAP_MANIFEST_H
#define SHELLCLAW_ASAP_MANIFEST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct config;
typedef struct config config_t;

/**
 * Build ASAP manifest JSON from config and current skills.
 * Caller must free the returned string.
 *
 * @param cfg  Configuration (agent URN, name; may be NULL for defaults).
 * @return     Allocated JSON string, or NULL on error.
 */
char *manifest_build_json(const config_t *cfg);

/**
 * Return ASAP health JSON string.
 * Static string; do not free.
 *
 * @return  JSON string {"status":"ok"}
 */
const char *manifest_health_json(void);

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_ASAP_MANIFEST_H */
