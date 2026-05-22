/**
 * @file openai_compat.h
 * @brief Shared OpenAI Chat Completions JSON body build and HTTP POST.
 */

#ifndef SHELLCLAW_OPENAI_COMPAT_H
#define SHELLCLAW_OPENAI_COMPAT_H

#include "providers/provider.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Build unformatted JSON body for chat/completions. Caller frees with cJSON_free().
 *
 * @return Heap string or NULL on error (sets response error when @p response non-NULL).
 */
char *openai_compat_build_chat_body(const char *model, int max_tokens, double temperature,
                                    const provider_message_t *messages, size_t message_count,
                                    const provider_tool_def_t *tools, size_t tool_count,
                                    provider_response_t *response);

/**
 * POST JSON to chat/completions URL; parse response with provider_parse_chat_completions_json.
 *
 * @param bearer_token Optional Bearer token (NULL omits Authorization header).
 * @param http_error_label Prefix for HTTP error messages (e.g. "OpenAI API").
 * @param nosignal When non-zero, sets CURLOPT_NOSIGNAL (local provider).
 */
int openai_compat_post_chat(const char *url, const char *body, const char *bearer_token,
                            const char *http_error_label, int nosignal, long request_timeout_sec,
                            long connect_timeout_sec, provider_response_t *response);

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_OPENAI_COMPAT_H */
