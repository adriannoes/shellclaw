/**
 * @file provider_common.c
 * @brief Shared provider helpers (e.g. response cleanup).
 */

#include "providers/provider.h"
#include <stdlib.h>
#include <string.h>

void provider_response_clear(provider_response_t *r)
{
	if (!r) return;
	free(r->content);
	r->content = NULL;
	if (r->tool_calls) {
		for (size_t i = 0; i < r->tool_calls_count; i++) {
			free(r->tool_calls[i].id);
			free(r->tool_calls[i].name);
			free(r->tool_calls[i].arguments);
		}
		free(r->tool_calls);
		r->tool_calls = NULL;
		r->tool_calls_count = 0;
	}
	r->error = 0;
}
