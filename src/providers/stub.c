/**
 * @file stub.c
 * @brief Stub provider for tests and verification of provider_t vtable.
 */
#define _POSIX_C_SOURCE 200809L

#include "providers/provider.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static int stub_init(const config_t *cfg)
{
	(void)cfg;
	return 0;
}

static int stub_chat(const provider_message_t *messages, size_t message_count,
                    const provider_tool_def_t *tools, size_t tool_count,
                    provider_response_t *response)
{
	(void)messages;
	(void)message_count;
	(void)tools;
	(void)tool_count;
	if (response) {
		response->error = 0;
		response->content = strdup("");
		response->tool_calls = NULL;
		response->tool_calls_count = 0;
	}
	return 0;
}

static void stub_cleanup(void) {}

static const provider_t stub_provider = {
	.name = "stub",
	.init = stub_init,
	.chat = stub_chat,
	.cleanup = stub_cleanup,
};

const provider_t *provider_stub_get(void)
{
	return &stub_provider;
}
