/**
 * @file envelope.h
 * @brief ASAP v2.1 envelope types and JSON-RPC 2.0 error helpers.
 */
#ifndef SHELLCLAW_ASAP_ENVELOPE_H
#define SHELLCLAW_ASAP_ENVELOPE_H

#include "cJSON.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Single ASAP message as structured fields. String fields and @p payload
 * are owned by this struct when filled by API functions; use
 * asap_envelope_clear to release.
 */
typedef struct asap_envelope {
	char *id;             /**< Envelope or task id (ULID or string). */
	char *asap_version;  /**< Protocol version, e.g. "2.1". */
	char *sender;        /**< Sender URN. */
	char *recipient;     /**< Recipient URN. */
	char *payload_type;  /**< e.g. task.request, task.response. */
	cJSON *payload;      /**< JSON object or value; owned; may be NULL. */
	char *correlation_id; /**< Correlation for tracing. */
	char *trace_id;      /**< Distributed trace id. */
	char *timestamp;     /**< ISO 8601 timestamp string. */
} asap_envelope_t;

/**
 * Zero-initialize an envelope. Does not free previous contents;
 * use asap_envelope_clear first if the struct was already in use.
 *
 * @param env  Envelope to initialize (non-NULL).
 */
void asap_envelope_init(asap_envelope_t *env);

/**
 * Free all string fields and the payload cJSON. Safe on cleared or
 * init-only structures.
 *
 * @param env  Envelope to clear.
 */
void asap_envelope_clear(asap_envelope_t *env);

/**
 * Build a JSON-RPC 2.0 error object (top-level) with a standard error
 * body. The returned root must be freed with cJSON_Delete, or use
 * cJSON_Print and free the string.
 *
 * @param code     JSON-RPC error code (e.g. -32602).
 * @param message  Error message (non-NULL).
 * @param id       Request id to echo; may be NULL for null id.
 * @return         Root cJSON object, or NULL on allocation failure.
 */
cJSON *asap_jsonrpc_error(int code, const char *message, cJSON *id);

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_ASAP_ENVELOPE_H */
