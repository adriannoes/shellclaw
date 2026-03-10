/**
 * @file static.h
 * @brief Embedded static asset lookup and serving.
 */

#ifndef SHELLCLAW_GATEWAY_STATIC_H
#define SHELLCLAW_GATEWAY_STATIC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Look up a static asset by path.
 *
 * @param path         Request path (e.g. "/", "/chat", "/css/style.css").
 * @param data_out     On success, set to pointer to gzipped data.
 * @param len_out      On success, set to data length.
 * @param content_type_out On success, set to Content-Type string (e.g. "text/html").
 * @return 0 if found, non-zero if not found.
 */
int static_lookup(const char *path, const unsigned char **data_out, size_t *len_out,
                 const char **content_type_out);

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_GATEWAY_STATIC_H */
