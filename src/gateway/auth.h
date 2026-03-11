/**
 * @file auth.h
 * @brief Pairing code generation and bearer token validation for the gateway.
 */

#ifndef SHELLCLAW_GATEWAY_AUTH_H
#define SHELLCLAW_GATEWAY_AUTH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque auth context. */
typedef struct auth_ctx auth_ctx_t;

/**
 * Initialize auth context with tokens file path.
 * Default path ~/.shellclaw/auth_tokens.json if tokens_path is NULL or empty.
 *
 * @param tokens_path Path to JSON tokens file; NULL for default.
 * @return Allocated auth_ctx_t; caller must auth_cleanup(). NULL on error.
 */
auth_ctx_t *auth_init(const char *tokens_path);

/**
 * Release auth context. Safe to call with NULL.
 *
 * @param ctx Auth context (may be NULL).
 */
void auth_cleanup(auth_ctx_t *ctx);

/**
 * Get or create pairing code. If no tokens file or empty, generate random 6-digit
 * code, print to stdout, and return allocated code string. Otherwise return NULL.
 *
 * @param ctx Auth context.
 * @return Allocated 6-digit code string; caller must free. NULL if tokens exist.
 */
char *auth_get_or_create_pairing_code(auth_ctx_t *ctx);

/**
 * Pair with code: validate 6-digit code, add token to file, copy to token_out.
 *
 * @param ctx        Auth context.
 * @param code       6-digit pairing code.
 * @param token_out  Buffer to receive the new token.
 * @param token_size Size of token_out.
 * @return 0 on success, non-zero on invalid code or error.
 */
int auth_pair(auth_ctx_t *ctx, const char *code, char *token_out, size_t token_size);

/**
 * Validate bearer token against stored tokens.
 *
 * @param ctx   Auth context.
 * @param token Token to validate.
 * @return 1 if valid, 0 otherwise.
 */
int auth_validate_token(auth_ctx_t *ctx, const char *token);

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_GATEWAY_AUTH_H */
