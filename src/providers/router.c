/**
 * @file router.c
 * @brief Provider router: select Anthropic or OpenAI from config (no fallback).
 */

#include "core/config.h"
#include "providers/provider.h"
#include <string.h>
#include <ctype.h>

static int str_case_equal(const char *a, const char *b)
{
	if (!a || !b) return 0;
	for (; *a && *b; a++, b++)
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
			return 0;
	return *a == *b;
}

const provider_t *provider_router_get(const config_t *cfg)
{
	if (!cfg) return NULL;
	const char *name = config_default_provider(cfg);
	if (!name || !name[0]) return NULL;
	if (str_case_equal(name, "stub")) return provider_stub_get();
	if (str_case_equal(name, "anthropic")) return provider_anthropic_get();
	if (str_case_equal(name, "openai")) return provider_openai_get();
	return NULL;
}
