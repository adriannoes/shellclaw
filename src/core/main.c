/**
 * @file main.c
 * @brief Entry point: CLI args, config load, init order, signal handlers, main loop.
 */

#include "core/config.h"
#include "core/memory.h"
#include "core/skill.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define VERSION "0.1.0"
#define DEFAULT_CONFIG_PATH "~/.shellclaw/config.toml"

static int g_verbose;
static volatile sig_atomic_t g_shutdown;

/* Stubs until channels/tools (Task 6–7). */
static int memory_init_from_config(const config_t *cfg)
{
	const char *path = config_memory_db_path(cfg);
	return path ? memory_init(path) : -1;
}
#define SKILLS_BUF_SIZE (256 * 1024)
#define SYSTEM_PROMPT_BUF_SIZE (256 * 1024)
static char g_skills_buf[SKILLS_BUF_SIZE];
static char g_system_prompt_base[SYSTEM_PROMPT_BUF_SIZE];

static int skills_init(const config_t *cfg)
{
	if (!cfg) return -1;
	if (skill_load_all(cfg, g_skills_buf, sizeof(g_skills_buf)) != 0) return -1;
	return skill_build_system_prompt_base(cfg, g_skills_buf, g_system_prompt_base, sizeof(g_system_prompt_base));
}
static void skills_cleanup(void) { (void)0; }
static int providers_init(const config_t *cfg) { (void)cfg; return 0; }
static void providers_cleanup(void) {}
static int channels_init(const config_t *cfg) { (void)cfg; return 0; }
static void channels_cleanup(void) {}
static int tools_init(const config_t *cfg) { (void)cfg; return 0; }
static void tools_cleanup(void) {}

static void on_signal(int sig)
{
	(void)sig;
	g_shutdown = 1;
}

static void setup_signals(void)
{
	if (signal(SIGINT, on_signal) == SIG_ERR)
		fprintf(stderr, "warning: signal(SIGINT) failed\n");
	if (signal(SIGTERM, on_signal) == SIG_ERR)
		fprintf(stderr, "warning: signal(SIGTERM) failed\n");
}

static int init_subsystems(const config_t *cfg)
{
	if (memory_init_from_config(cfg) != 0) return -1;
	if (skills_init(cfg) != 0) { memory_cleanup(); return -1; }
	if (providers_init(cfg) != 0) { skills_cleanup(); memory_cleanup(); return -1; }
	if (channels_init(cfg) != 0) { providers_cleanup(); skills_cleanup(); memory_cleanup(); return -1; }
	if (tools_init(cfg) != 0) {
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

static void main_loop(void)
{
	while (!g_shutdown) {
		/* Placeholder until channel poll (Task 6). */
		sleep(1);
	}
}

static void print_usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [--config <path>] [--verbose] [--version]\n", prog);
}

static int parse_args(int argc, char **argv, const char **config_path_out)
{
	*config_path_out = DEFAULT_CONFIG_PATH;
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
	char errbuf[256];
	if (config_load(config_path, &cfg, errbuf, sizeof(errbuf)) != 0) {
		fprintf(stderr, "Error: %s\n", errbuf[0] ? errbuf : "failed to load config");
		return 1;
	}
	(void)g_verbose;
	g_shutdown = 0;
	setup_signals();
	if (init_subsystems(cfg) != 0) {
		fprintf(stderr, "Error: subsystem init failed\n");
		config_free(cfg);
		return 1;
	}
	main_loop();
	cleanup_subsystems();
	config_free(cfg);
	return 0;
}
