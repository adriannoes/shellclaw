/**
 * @file provider.h
 * @brief LLM provider vtable and shared types (message, response, tool_def).
 *
 * All providers (Anthropic, OpenAI) implement the same interface so the agent
 * is provider-agnostic. API keys are read from environment via config; never logged.
 */

#ifndef SHELLCLAW_PROVIDER_H
#define SHELLCLAW_PROVIDER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct config;
typedef struct config config_t;

/** Tool definition passed to the LLM (JSON Schema for parameters). */
typedef struct provider_tool_def {
	const char *name;             /**< Tool name (e.g. "shell") */
	const char *description;      /**< Human-readable description */
	const char *parameters_json;  /**< JSON Schema string for arguments */
} provider_tool_def_t;

/** One tool call returned by the LLM (provider allocates; caller frees via response_clear). */
typedef struct provider_tool_call {
	char *id;         /**< Call id from API */
	char *name;       /**< Tool name to invoke */
	char *arguments;  /**< JSON string of arguments */
} provider_tool_call_t;

/** Single chat message (caller-owned; provider does not take ownership). */
typedef struct provider_message {
	const char *role;   /**< "system", "user", "assistant", or "tool" (OpenAI) */
	const char *content; /**< Message body (text) */
	/** For assistant messages: tool calls made in this turn (provider builds tool_use blocks). */
	const provider_tool_call_t *tool_calls;
	size_t tool_calls_count;
	/** For tool result messages: id of the tool_use this result answers (content = result). */
	const char *tool_use_id;
} provider_message_t;

/** Chat response (provider fills; caller must call provider_response_clear after use). */
typedef struct provider_response {
	int error;                    /**< 0 on success, non-zero on failure */
	char *content;                /**< Assistant text (allocated by provider) */
	provider_tool_call_t *tool_calls; /**< Array of tool calls (allocated by provider) */
	size_t tool_calls_count;      /**< Number of tool_calls */
} provider_response_t;

/** Clear and free response fields. Caller-allocated struct; safe to call repeatedly or on zeroed. */
void provider_response_clear(provider_response_t *r);

/**
 * Provider vtable: init, chat, cleanup. All providers implement this interface.
 */
typedef struct provider {
	const char *name;
	/** Initialize with config (e.g. read API key env). Return 0 on success. */
	int (*init)(const config_t *cfg);
	/**
	 * Send messages and optional tools; fill response. Return 0 on success.
	 * On failure, set response->error and optionally response->content to error message.
	 */
	int (*chat)(const provider_message_t *messages, size_t message_count,
	            const provider_tool_def_t *tools, size_t tool_count,
	            provider_response_t *response);
	/** Release provider resources. */
	void (*cleanup)(void);
} provider_t;

/** Stub provider for tests and vtable verification. Returns static vtable. */
const provider_t *provider_stub_get(void);

/** Anthropic (Claude) provider. Messages API, tool_use. */
const provider_t *provider_anthropic_get(void);

/** OpenAI provider. Chat Completions, tool_calls (function calling). */
const provider_t *provider_openai_get(void);

/** Provider selected by config (default_provider). No fallback; failure propagates to caller. */
const provider_t *provider_router_get(const config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_PROVIDER_H */
