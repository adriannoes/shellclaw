/**
 * @file bootstrap_dispatch_stub.c
 * @brief Minimal bootstrap surface for dispatch unit tests (avoids full subsystem init).
 */
#define _POSIX_C_SOURCE 200809L

#include "core/bootstrap.h"
#include "providers/provider.h"
#include "tools/tool.h"

#define MAX_TOOLS 8

static config_t *g_cfg;
static const provider_t *g_provider;
static const tool_t *g_tools[MAX_TOOLS];
static size_t g_tool_count;

config_t *bootstrap_get_cfg(void)
{
	return g_cfg;
}

void bootstrap_set_cfg(config_t *cfg)
{
	g_cfg = cfg;
}

const provider_t *bootstrap_get_provider(void)
{
	return g_provider;
}

void bootstrap_set_provider_for_test(const provider_t *provider)
{
	g_provider = provider;
}

size_t bootstrap_tool_count(void)
{
	return g_tool_count;
}

const tool_t *bootstrap_tool_at(size_t index)
{
	if (index >= g_tool_count)
		return NULL;
	return g_tools[index];
}

void bootstrap_reset_tools_for_test(void)
{
	g_tool_count = 0;
}

void bootstrap_add_tool_for_test(const tool_t *tool)
{
	if (tool != NULL && g_tool_count < MAX_TOOLS)
		g_tools[g_tool_count++] = tool;
}
