/**
 * @file static.c
 * @brief Embedded static asset lookup and serving.
 */
#define _POSIX_C_SOURCE 200809L

#include "gateway/static.h"
#include "gateway/ui_assets.h"
#include <string.h>

int static_lookup(const char *path, const unsigned char **data_out, size_t *len_out,
                  const char **content_type_out)
{
	if (!path || !data_out || !len_out || !content_type_out) return -1;
	*data_out = NULL;
	*len_out = 0;
	*content_type_out = NULL;
	/* Normalize: strip query string if present */
	const char *q = strchr(path, '?');
	size_t path_len = q ? (size_t)(q - path) : strlen(path);
	if (path_len == 0) return -1;
	for (size_t i = 0; ui_asset_table[i].path != NULL; i++) {
		size_t plen = strlen(ui_asset_table[i].path);
		if (path_len == plen && strncmp(path, ui_asset_table[i].path, plen) == 0) {
			*data_out = ui_asset_table[i].ptr;
			*len_out = ui_asset_table[i].len;
			*content_type_out = ui_asset_table[i].content_type;
			return 0;
		}
	}
	/* SPA fallback: unknown paths serve index.html (first entry is / -> index.html) */
	*data_out = ui_asset_table[0].ptr;
	*len_out = ui_asset_table[0].len;
	*content_type_out = "text/html";
	return 0;
}
