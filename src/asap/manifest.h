/**
 * @file manifest.h
 * @brief ASAP manifest and health JSON builders for well-known discovery.
 */
#ifndef SHELLCLAW_ASAP_MANIFEST_H
#define SHELLCLAW_ASAP_MANIFEST_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct config;
typedef struct config config_t;

/**
 * Build upstream ASAP Manifest JSON (v2.4 capabilities.hardware + inference).
 * Caller must free the returned string.
 *
 * @param cfg  Configuration (URN, description, public_base_url, skills; NULL uses defaults).
 * @return     Allocated JSON string, or NULL on error.
 */
char *manifest_build_json(const config_t *cfg);

/**
 * Build upstream SignedManifest JSON (manifest + Ed25519 signature block + public_key).
 * Canonicalizes the inner manifest with JCS (RFC 8785) before signing.
 * Returns NULL if keys cannot be loaded or on allocation/signing failure.
 *
 * @param cfg  Configuration (same as @ref manifest_build_json).
 * @return     Allocated JSON string, or NULL on error.
 */
char *manifest_build_signed_json(const config_t *cfg);

/**
 * Return ASAP health JSON string.
 * Static string; do not free.
 *
 * @return  JSON string {"status":"ok"}
 */
const char *manifest_health_json(void);

/**
 * Load or create Ed25519 keypair under $SHELLCLAW_HOME/keys/ (default ~/.shellclaw/keys/).
 * Creates ed25519.priv (64 bytes) and ed25519.pub (32 bytes) with mode 0600 on first use.
 * Refuses if ed25519.priv exists with group/other permission bits set.
 *
 * @return 0 on success, -1 on I/O, permission, or crypto error.
 */
int manifest_keys_load(void);

/**
 * Return loaded public key (32 bytes) after @ref manifest_keys_load succeeds.
 */
const uint8_t *manifest_keys_public(void);

/**
 * Return loaded secret key (64 bytes) after @ref manifest_keys_load succeeds.
 */
const uint8_t *manifest_keys_private(void);

/** Tests only: use @p keys_dir for ed25519.{priv,pub} (NULL restores default layout). */
void manifest_keys_set_dir_for_test(const char *keys_dir);

/**
 * Rotate Ed25519 keypair on disk: backup existing keys as
 * ed25519.priv.bak.<unix_ts> and ed25519.pub.bak.<unix_ts> when present,
 * then write a new pair with mode 0600. Clears the in-memory cache.
 *
 * @param err     Optional error buffer (may be NULL).
 * @param err_len Size of @p err.
 * @return 0 on success, -1 on failure.
 */
int manifest_keys_rotate(char *err, size_t err_len);

/** Clear in-memory keys so @ref manifest_keys_load runs again. */
void manifest_keys_reset(void);

/** Tests only: alias for @ref manifest_keys_reset. */
void manifest_keys_reset_for_test(void);

/** Tests only: next atomic write to ed25519.pub fails once (cleared after use). */
void manifest_keys_test_set_fail_pub_write(int enabled);

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_ASAP_MANIFEST_H */
