/**
 * @file channel.h
 * @brief Channel vtable and message types. CLI and Telegram implement this interface.
 */

#ifndef SHELLCLAW_CHANNEL_H
#define SHELLCLAW_CHANNEL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct config;
typedef struct config config_t;

/** Attachment for incoming (photo) or outgoing (optional) messages. */
typedef struct channel_attachment {
	char *path_or_base64;  /**< File path or base64-encoded image data */
	size_t size;           /**< Length of path_or_base64 string */
	int is_base64;         /**< 1 if base64, 0 if file path */
} channel_attachment_t;

/**
 * Incoming message from a channel. Channel allocates; caller must call
 * channel_incoming_msg_clear() when done.
 */
typedef struct channel_incoming_msg {
	char *session_id;           /**< e.g. "cli:default", "telegram:123456" */
	char *user_id;              /**< Channel-specific user ID (e.g. Telegram user id) */
	char *text;                 /**< Message text (may be empty for photo-only) */
	channel_attachment_t *attachments;
	size_t attachments_count;
} channel_incoming_msg_t;

/** Clear and free incoming message fields. Safe to call repeatedly or on zeroed. */
void channel_incoming_msg_clear(channel_incoming_msg_t *msg);

/**
 * Channel vtable: init, poll, send, cleanup. CLI and Telegram implement this.
 */
typedef struct channel {
	const char *name;
	/** Initialize with config. Return 0 on success. */
	int (*init)(const config_t *cfg);
	/**
	 * Poll for incoming message. Block up to timeout_ms.
	 * @param out       Filled on success; caller must channel_incoming_msg_clear().
	 * @param timeout_ms Max wait in milliseconds.
	 * @return 1 if message received, 0 if timeout/no message, -1 on error.
	 */
	int (*poll)(channel_incoming_msg_t *out, int timeout_ms);
	/**
	 * Send response to recipient.
	 * @param recipient  Session ID or user ID (channel-specific).
	 * @param text       Response text (must not be NULL).
	 * @param attachments Optional; may be NULL if count is 0.
	 * @param att_count  Number of attachments.
	 * @return 0 on success, non-zero on error.
	 */
	int (*send)(const char *recipient, const char *text,
	            const channel_attachment_t *attachments, size_t att_count);
	/** Release channel resources. */
	void (*cleanup)(void);
} channel_t;

#define MAX_REGISTERED_CHANNELS 8

/** Register a channel by name for routing (e.g. cron send). */
void channel_register(const char *name, const channel_t *ch);

/** Get channel by name. Returns NULL if not found. */
const channel_t *channel_get_by_name(const char *name);

/** Stub channel for tests. poll() always returns 0 (no message). */
const channel_t *channel_stub_get(void);

/** CLI channel: stdin/stdout, -m one-shot, interactive prompt. */
const channel_t *channel_cli_get(void);

/** Set one-shot message for CLI (call before init when using -m). */
void channel_cli_set_one_shot(const char *msg);

/** Set verbose mode for CLI output (ANSI colors). */
void channel_cli_set_verbose(int v);

/** Telegram channel: long-poll getUpdates, allowlist, /reset, /status. */
const channel_t *channel_telegram_get(void);

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_CHANNEL_H */
