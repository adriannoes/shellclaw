/**
 * @file log.h
 * @brief Lock-protected ring buffer of last ASAP_LOG_RING_SIZE envelope events for the Web UI.
 *
 * Thread-safe via an internal pthread mutex. Callers append inbound/outbound events;
 * the Web UI reads a snapshot via asap_log_get_snapshot().
 */
#ifndef SHELLCLAW_ASAP_LOG_H
#define SHELLCLAW_ASAP_LOG_H

#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum entries kept in the ring (oldest evicted when full). */
#define ASAP_LOG_RING_SIZE 50

/** Maximum bytes for the truncated JSON snippet per entry. */
#define ASAP_LOG_SNIPPET_MAX 256

/** Maximum bytes for the payload_type field. */
#define ASAP_LOG_TYPE_MAX 32

/** Maximum bytes for the envelope id field. */
#define ASAP_LOG_ID_MAX 64

/** Inbound message received on /asap. */
#define ASAP_LOG_DIR_IN 0

/** Outbound response produced by the server. */
#define ASAP_LOG_DIR_OUT 1

/** Single entry in the envelope log ring. */
typedef struct asap_log_entry {
	time_t ts;                               /**< Unix timestamp of the event. */
	int direction;                           /**< ASAP_LOG_DIR_IN or ASAP_LOG_DIR_OUT. */
	char payload_type[ASAP_LOG_TYPE_MAX];    /**< ASAP payload_type string (truncated). */
	char id[ASAP_LOG_ID_MAX];               /**< Envelope id (truncated). */
	char json_snippet[ASAP_LOG_SNIPPET_MAX]; /**< Truncated JSON representation. */
} asap_log_entry_t;

/**
 * Append an inbound envelope event to the ring.
 * If the ring is full, the oldest entry is evicted.
 * Thread-safe (internal pthread mutex).
 *
 * @param payload_type  Payload type string (may be NULL).
 * @param id            Envelope id string (may be NULL).
 * @param json_snippet  Truncated JSON string (may be NULL).
 */
void asap_log_append_in(const char *payload_type, const char *id, const char *json_snippet);

/**
 * Append an outbound envelope event to the ring.
 * Thread-safe (internal pthread mutex).
 *
 * @param payload_type  Payload type string (may be NULL).
 * @param id            Envelope id string (may be NULL).
 * @param json_snippet  Truncated JSON string (may be NULL).
 */
void asap_log_append_out(const char *payload_type, const char *id, const char *json_snippet);

/**
 * Copy up to @p max_entries entries from the ring (oldest first) into @p out.
 * Thread-safe (internal pthread mutex).
 *
 * @param out         Output array; caller must allocate at least
 *                    max_entries * sizeof(asap_log_entry_t) bytes.
 * @param max_entries Maximum entries to copy.
 * @return            Number of entries copied (0 when ring is empty).
 */
int asap_log_get_snapshot(asap_log_entry_t *out, int max_entries);

/**
 * Reset the ring to empty state.
 * Primarily useful in unit tests between test cases.
 */
void asap_log_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_ASAP_LOG_H */
