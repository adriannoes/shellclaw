/**
 * @file main.c
 * @brief Entry point: CLI, config, init order, signals, main loop.
 */
#define _POSIX_C_SOURCE 200809L

#include "core/agent.h"
#include "core/config.h"
#include "core/memory.h"
#include "core/skill.h"
#include "channels/channel.h"
#include "providers/provider.h"
#include "tools/tool.h"
#include <curl/curl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define VERSION "0.1.0"
#define DEFAULT_CONFIG_PATH "~/.shellclaw/config.toml"
#define SKILLS_BUF_SIZE (256 * 1024)
#define SYSTEM_PROMPT_BUF_SIZE (256 * 1024)
#define RESPONSE_BUF_SIZE (32 * 1024)
#define POLL_TIMEOUT_MS 1000

static int g_verbose;
static volatile sig_atomic_t g_shutdown;

static const char *g_cli_one_shot;
static const config_t *g_cfg;
static const provider_t *g_provider;
#define MAX_TOOLS 8
static const tool_t *g_tools[MAX_TOOLS];
static size_t g_tool_count;

static int memory_init_from_config(const config_t *cfg)
{
	const char *path = config_memory_db_path(cfg);
	return path ? memory_init(path) : -1;
}

static int skills_init(const config_t *cfg)
{
	if (!cfg) return -1;
	char *skills_buf = malloc(SKILLS_BUF_SIZE);
	char *system_buf = malloc(SYSTEM_PROMPT_BUF_SIZE);
	if (!skills_buf || !system_buf) {
		free(skills_buf);
		free(system_buf);
		return -1;
	}
	int ret = skill_load_all(cfg, skills_buf, SKILLS_BUF_SIZE);
	if (ret == 0)
		ret = skill_build_system_prompt_base(cfg, skills_buf, system_buf, SYSTEM_PROMPT_BUF_SIZE);
	free(skills_buf);
	free(system_buf);
	return ret;
}

static void skills_cleanup(void) { (void)0; }

static int providers_init(const config_t *cfg)
{
	g_provider = provider_router_get(cfg);
	if (!g_provider) return -1;
	return g_provider->init(cfg);
}

static void providers_cleanup(void)
{
	if (g_provider && g_provider->cleanup)
		g_provider->cleanup();
	g_provider = NULL;
}

#define MAX_CHANNELS 4
static const channel_t *g_channels[MAX_CHANNELS];
static int g_channel_count;

static int channels_init(const config_t *cfg)
{
	g_cfg = cfg;
	g_channel_count = 0;
	channel_cli_set_one_shot(g_cli_one_shot);
	channel_cli_set_verbose(g_verbose);
	const channel_t *cli = channel_cli_get();
	if (cli->init(cfg) != 0) return -1;
	g_channels[g_channel_count++] = cli;
	if (config_telegram_enabled(cfg)) {
		const channel_t *tg = channel_telegram_get();
		if (tg->init(cfg) == 0)
			g_channels[g_channel_count++] = tg;
	}
	return 0;
}

static void channels_cleanup(void)
{
	for (int i = 0; i < g_channel_count; i++) {
		if (g_channels[i] && g_channels[i]->cleanup)
			g_channels[i]->cleanup();
	}
	g_channel_count = 0;
	g_cfg = NULL;
}

static int tools_init(const config_t *cfg)
{
	tool_set_config(cfg);
	g_tool_count = tool_get_all(g_tools, MAX_TOOLS);
	return 0;
}

static void tools_cleanup(void)
{
	g_tool_count = 0;
}

static void on_signal(int sig)
{
	(void)sig;
	g_shutdown = 1;
}

static void setup_signals(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGINT, &sa, NULL) != 0)
		fprintf(stderr, "warning: sigaction(SIGINT) failed\n");
	if (sigaction(SIGTERM, &sa, NULL) != 0)
		fprintf(stderr, "warning: sigaction(SIGTERM) failed\n");
}

static int init_subsystems(const config_t *cfg)
{
	if (memory_init_from_config(cfg) != 0) {
		fprintf(stderr, "Error: memory init failed\n");
		return -1;
	}
	if (skills_init(cfg) != 0) {
		fprintf(stderr, "Error: skills init failed\n");
		memory_cleanup();
		return -1;
	}
	if (providers_init(cfg) != 0) {
		fprintf(stderr, "Error: provider init failed (check default_provider and API keys)\n");
		skills_cleanup();
		memory_cleanup();
		return -1;
	}
	if (channels_init(cfg) != 0) {
		fprintf(stderr, "Error: channels init failed\n");
		providers_cleanup();
		skills_cleanup();
		memory_cleanup();
		return -1;
	}
	/* cppcheck-suppress knownConditionTrueFalse */
	if (tools_init(cfg) != 0) {
		fprintf(stderr, "Error: tools init failed\n");
		channels_cleanup();
		providers_cleanup();
		skills_cleanup();
		memory_cleanup();
		return -1;
	}
	return 0;
}

static void cleanup_subsystems(void)
{
	tools_cleanup();
	channels_cleanup();
	providers_cleanup();
	skills_cleanup();
	memory_cleanup();
}

static int handle_message(const channel_t *ch, const channel_incoming_msg_t *msg)
{
	const char *text = msg->text ? msg->text : "";
	if (strcmp(text, "/reset") == 0) {
		session_delete(msg->session_id);
		return ch->send(msg->session_id, "Session cleared.", NULL, 0);
	}
	if (strcmp(text, "/status") == 0) {
		char buf[128];
		snprintf(buf, sizeof(buf), "ShellClaw %s — agent ready.", VERSION);
		return ch->send(msg->session_id, buf, NULL, 0);
	}
	char resp_buf[RESPONSE_BUF_SIZE];
	agent_tool_t flat_tools[MAX_TOOLS];
	for (size_t i = 0; i < g_tool_count; i++) {
		flat_tools[i].name = g_tools[i]->name;
		flat_tools[i].description = g_tools[i]->description;
		flat_tools[i].parameters_json = g_tools[i]->parameters_json;
		flat_tools[i].execute = g_tools[i]->execute;
	}
	int err = agent_run(g_cfg, msg->session_id, text, g_provider,
	                    flat_tools, g_tool_count,
	                    resp_buf, sizeof(resp_buf));
	if (err != 0 && resp_buf[0] == '\0')
		snprintf(resp_buf, sizeof(resp_buf), "Error: agent failed (code %d)", err);
	return ch->send(msg->session_id, resp_buf, NULL, 0);
}

static void main_loop(int one_shot)
{
	while (!g_shutdown) {
		channel_incoming_msg_t msg;
		memset(&msg, 0, sizeof(msg));
		int got = 0;
		const channel_t *which = NULL;
		for (int i = 0; i < g_channel_count && !got; i++) {
			int r = g_channels[i]->poll(&msg, POLL_TIMEOUT_MS);
			if (r == 1) {
				got = 1;
				which = g_channels[i];
				break;
			}
			if (r < 0)
				channel_incoming_msg_clear(&msg);
		}
		if (got && which) {
			handle_message(which, &msg);
			channel_incoming_msg_clear(&msg);
			if (one_shot)
				g_shutdown = 1;
		}
	}
}

static void print_usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [--config <path>] [--verbose] [--version] [-m \"message\"]\n", prog);
}

static int parse_args(int argc, char **argv, const char **config_path_out)
{
	*config_path_out = DEFAULT_CONFIG_PATH;
	g_cli_one_shot = NULL;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--version") == 0) {
			printf("%s\n", VERSION);
			exit(0);
		}
		if (strcmp(argv[i], "--verbose") == 0) {
			g_verbose = 1;
			continue;
		}
		if (strcmp(argv[i], "--config") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "Error: --config requires a path argument\n");
				return -1;
			}
			*config_path_out = argv[++i];
			continue;
		}
		if (strcmp(argv[i], "-m") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "Error: -m requires a message argument\n");
				return -1;
			}
			g_cli_one_shot = argv[++i];
			continue;
		}
		fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
		print_usage(argv[0]);
		return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	const char *config_path;
	if (parse_args(argc, argv, &config_path) != 0) return 1;
	config_t *cfg = NULL;
	char errbuf[256] = {0};
	if (config_load(config_path, &cfg, errbuf, sizeof(errbuf)) != 0) {
		fprintf(stderr, "Error: %s\n", errbuf[0] ? errbuf : "failed to load config");
		return 1;
	}
	g_shutdown = 0;
	setup_signals();
	if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
		fprintf(stderr, "Error: curl_global_init failed\n");
		config_free(cfg);
		return 1;
	}
	if (init_subsystems(cfg) != 0) {
		fprintf(stderr, "Error: subsystem init failed (check config, API keys, memory path)\n");
		curl_global_cleanup();
		config_free(cfg);
		return 1;
	}
	main_loop(g_cli_one_shot != NULL);
	cleanup_subsystems();
	curl_global_cleanup();
	config_free(cfg);
	return 0;
}
