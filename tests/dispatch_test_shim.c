/**
 * @file dispatch_test_shim.c
 * @brief Minimal bootstrap stubs for dispatch unit tests (avoids full subsystem link).
 */
#define _POSIX_C_SOURCE 200809L

#include "core/bootstrap.h"
#include "core/config.h"
#include "providers/provider.h"
#include "tools/tool.h"

static config_t *g_cfg;
static const provider_t *g_provider;
static const tool_t *g_tools[8];
static size_t g_tool_count;

void bootstrap_set_test_state(config_t *cfg, const provider_t *provider)
{
	g_cfg = cfg;
	g_provider = provider;
}

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

void bootstrap_set_verbose(int verbose)
{
	(void)verbose;
}

void bootstrap_set_cli_one_shot(const char *one_shot)
{
	(void)one_shot;
}

void bootstrap_set_config_path(const char *path)
{
	(void)path;
}

const char *bootstrap_get_config_path(void)
{
	return NULL;
}

int bootstrap_channel_count(void)
{
	return 0;
}

const channel_t *bootstrap_channel_at(int index)
{
	(void)index;
	return NULL;
}
