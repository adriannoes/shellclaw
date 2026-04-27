/**
 * @file client.h
 * @brief ASAP HTTP client: JSON-RPC request over libcurl, JSON-RPC response into envelope.
 */
#ifndef SHELLCLAW_ASAP_CLIENT_H
#define SHELLCLAW_ASAP_CLIENT_H

#include "asap/envelope.h"
#include "core/config.h"
#include <stddef.h>

/** Defaults: 30s total and connect, SSL peer verification on. */
typedef struct asap_client_config {
	long timeout_sec;
	long connect_timeout_sec;
	/** 1 to verify TLS peer (recommended). 0 for testing only. */
	int ssl_verifypeer;
} asap_client_config_t;

void asap_client_config_init(asap_client_config_t *c);

/**
 * Set timeouts from @a cfg (asap @c client_timeout_sec) and default connect/SSL.
 * If @a c is pre-filled by caller, only missing/zero fields need override; this
 * overwrites with config-based timeout when @a cfg is non-NULL.
 */
void asap_client_config_from_config(const config_t *cfg, asap_client_config_t *c);

#define ASAP_DEFAULT_JSONRPC_METHOD "asap.send"

/**
 * POST a JSON-RPC request built from @a env to @a url. On HTTP 200, parses
 * the JSON body as a JSON-RPC success and fills @a response. On failure, writes
 * a short message to @a errbuf when non-NULL.
 *
 * If @a env->jsonrpc_request_id is NULL, generates a new ULID string as id.
 * @a bearer if non-NULL, sends @c Authorization: Bearer …
 * @a client if NULL, values come from @a cfg via #asap_client_config_from_config, or
 * pure defaults when @a cfg is also NULL.
 * @a response: caller should #asap_envelope_init or #asap_envelope_clear before/after.
 *
 * @return 0 on success, -1 on error (curl, non-200, or parse)
 */
int asap_client_send_task(const char *url, const char *bearer, const char *jsonrpc_method,
		const asap_envelope_t *env, const asap_client_config_t *client, const config_t *cfg,
		asap_envelope_t *response, char *errbuf, size_t errlen);

#endif /* SHELLCLAW_ASAP_CLIENT_H */
