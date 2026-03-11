/**
 * @file http.h
 * @brief HTTP server and REST API (libwebsockets for HTTP+WebSocket same port).
 */

#ifndef SHELLCLAW_GATEWAY_HTTP_H
#define SHELLCLAW_GATEWAY_HTTP_H

#ifdef __cplusplus
extern "C" {
#endif

struct config;
typedef struct config config_t;
struct auth_ctx;

/**
 * Start HTTP+WebSocket server on config host:port.
 * Rejects bind to 0.0.0.0 if allow_bind_all is false.
 *
 * @param cfg         Configuration (host, port, allow_bind_all).
 * @param auth_ctx    Auth context for token validation.
 * @param config_path Path to config.toml for PUT /api/config.
 * @return 0 on success, non-zero on error.
 */
int http_start(const config_t *cfg, struct auth_ctx *auth_ctx, const char *config_path);

/**
 * Stop HTTP server. Safe to call if not started.
 */
void http_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_GATEWAY_HTTP_H */
