/**
 * @file reload_propagation_shim.c
 * @brief Test doubles for try_config_reload() subsystem propagation hooks.
 */
#include "core/bootstrap.h"
#include "core/config.h"
#include "providers/provider.h"
#include "tools/tool.h"
#include "channels/channel.h"
#include "channels/heartbeat.h"

static const char *g_config_path;
static config_t *g_bootstrap_cfg;
static config_t *g_router_live_cfg;

void bootstrap_set_config_path(const char *path)
{
	g_config_path = path;
}

const char *bootstrap_get_config_path(void)
{
	return g_config_path;
}

void bootstrap_set_cfg(config_t *cfg)
{
	g_bootstrap_cfg = cfg;
}

config_t *bootstrap_get_cfg(void)
{
	return g_bootstrap_cfg;
}

void provider_router_set_live_config(const config_t *cfg)
{
	g_router_live_cfg = (config_t *)cfg;
}

config_t *reload_test_router_live_cfg(void)
{
	return g_router_live_cfg;
}

void provider_openai_set_live_config(const config_t *cfg)
{
	(void)cfg;
}

void provider_local_set_live_config(const config_t *cfg)
{
	(void)cfg;
}

void tool_set_config(const config_t *cfg)
{
	(void)cfg;
}

void shellclaw_telegram_set_live_cfg(const config_t *cfg)
{
	(void)cfg;
}

void shellclaw_discord_set_live_cfg(const config_t *cfg)
{
	(void)cfg;
}

void heartbeat_set_live_config(const config_t *cfg)
{
	(void)cfg;
}
