/**
 * @file config.c
 * @brief Configuration loader: TOML parse + environment overrides.
 */
#define _POSIX_C_SOURCE 200809L

#include "config.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "toml.h"

#define ERRBUF_COPY(buf, sz, msg) do { \
	if ((buf) && (sz) > 0) { \
		size_t n = strlen(msg); \
		if (n >= (sz)) n = (sz) - 1; \
		memcpy((buf), (msg), n); \
		(buf)[n] = '\0'; \
	} \
} while (0)

#define DEFAULT_MAX_TOOL_ITERATIONS 20
#define DEFAULT_MAX_CONTEXT_MESSAGES 40
#define DEFAULT_MAX_TOKENS 4096
#define DEFAULT_TEMPERATURE 0.7
#define DEFAULT_SHELL_TIMEOUT_SEC 60

#define ENV_AGENT_MODEL          "SHELLCLAW_AGENT_MODEL"
#define ENV_AGENT_MAX_TOKENS     "SHELLCLAW_AGENT_MAX_TOKENS"
#define ENV_AGENT_TEMPERATURE    "SHELLCLAW_AGENT_TEMPERATURE"
#define ENV_AGENT_MAX_TOOL_ITER  "SHELLCLAW_AGENT_MAX_TOOL_ITERATIONS"
#define ENV_AGENT_MAX_CTX_MSG    "SHELLCLAW_AGENT_MAX_CONTEXT_MESSAGES"
#define ENV_MEMORY_DB_PATH       "SHELLCLAW_MEMORY_DB_PATH"
#define ENV_SKILLS_DIR            "SHELLCLAW_SKILLS_DIR"
#define ENV_OPENAI_ENDPOINT      "SHELLCLAW_OPENAI_ENDPOINT"
#define ENV_DEFAULT_PROVIDER     "SHELLCLAW_DEFAULT_PROVIDER"

struct config {
	char *agent_model;
	int agent_max_tokens;
	double agent_temperature;
	int agent_max_tool_iterations;
	int agent_max_context_messages;
	char *agent_soul_path;
	char *agent_identity_path;
	char *agent_user_path;
	char *provider_default;
	char *provider_anthropic_api_key_env;
	char *provider_openai_api_key_env;
	char *provider_openai_endpoint;
	int telegram_enabled;
	char *telegram_token_env;
	char **telegram_allowed_users;
	int telegram_allowed_users_count;
	char *memory_db_path;
	char *skills_dir;
	int workspace_only;
	char *workspace_path;
	int shell_timeout_sec;
};

static void set_string(char **dst, const char *src)
{
	if (*dst) free(*dst);
	*dst = src ? strdup(src) : NULL;
}

static char *expand_tilde(const char *path)
{
	if (!path || path[0] != '~') return path ? strdup(path) : NULL;
	const char *home = getenv("HOME");
	if (!home) home = "";
	if (path[1] == '\0' || path[1] == '/') {
		size_t hlen = strlen(home);
		size_t tail = strlen(path + 1);  /* skip '~' */
		char *out = malloc(hlen + tail + 1);
		if (!out) return NULL;
		memcpy(out, home, hlen);
		memcpy(out + hlen, path + 1, tail + 1);  /* includes NUL */
		return out;
	}
	return strdup(path);
}

static int parse_agent(const toml_table_t *root, config_t *cfg, char *errbuf, size_t errbufsz)
{
	const toml_table_t *agent = toml_table_in(root, "agent");
	if (!agent) {
		ERRBUF_COPY(errbuf, errbufsz, "missing [agent] section");
		return -1;
	}
	toml_datum_t d = toml_string_in(agent, "model");
	if (d.ok) {
		set_string(&cfg->agent_model, d.u.s);
		free(d.u.s);
	}
	d = toml_int_in(agent, "max_tokens");
	if (d.ok) cfg->agent_max_tokens = (int)d.u.i;
	d = toml_double_in(agent, "temperature");
	if (d.ok) cfg->agent_temperature = d.u.d;
	d = toml_int_in(agent, "max_tool_iterations");
	if (d.ok) cfg->agent_max_tool_iterations = (int)d.u.i;
	d = toml_int_in(agent, "max_context_messages");
	if (d.ok) cfg->agent_max_context_messages = (int)d.u.i;
	const toml_table_t *identity = toml_table_in(agent, "identity");
	if (identity) {
		d = toml_string_in(identity, "soul");
		if (d.ok) { set_string(&cfg->agent_soul_path, d.u.s); free(d.u.s); }
		d = toml_string_in(identity, "identity");
		if (d.ok) { set_string(&cfg->agent_identity_path, d.u.s); free(d.u.s); }
		d = toml_string_in(identity, "user");
		if (d.ok) { set_string(&cfg->agent_user_path, d.u.s); free(d.u.s); }
	}
	return 0;
}

static int parse_providers(const toml_table_t *root, config_t *cfg)
{
	const toml_table_t *providers = toml_table_in(root, "providers");
	if (!providers) return 0;
	toml_datum_t d_def = toml_string_in(providers, "default");
	if (d_def.ok) { set_string(&cfg->provider_default, d_def.u.s); free(d_def.u.s); }
	const toml_table_t *anth = toml_table_in(providers, "anthropic");
	if (anth) {
		toml_datum_t d_anth = toml_string_in(anth, "api_key_env");
		if (d_anth.ok) { set_string(&cfg->provider_anthropic_api_key_env, d_anth.u.s); free(d_anth.u.s); }
	}
	const toml_table_t *openai = toml_table_in(providers, "openai");
	if (openai) {
		toml_datum_t d_oe = toml_string_in(openai, "api_key_env");
		if (d_oe.ok) { set_string(&cfg->provider_openai_api_key_env, d_oe.u.s); free(d_oe.u.s); }
		toml_datum_t d_ep = toml_string_in(openai, "endpoint");
		if (d_ep.ok) { set_string(&cfg->provider_openai_endpoint, d_ep.u.s); free(d_ep.u.s); }
	}
	return 0;
}

static int parse_telegram(const toml_table_t *root, config_t *cfg, char *errbuf, size_t errbufsz)
{
	const toml_table_t *ch = toml_table_in(root, "channels");
	if (!ch) return 0;
	const toml_table_t *tg = toml_table_in(ch, "telegram");
	if (!tg) return 0;
	toml_datum_t d = toml_bool_in(tg, "enabled");
	if (d.ok) cfg->telegram_enabled = d.u.b;
	d = toml_string_in(tg, "token_env");
	if (d.ok) { set_string(&cfg->telegram_token_env, d.u.s); free(d.u.s); }
	const toml_array_t *arr = toml_array_in(tg, "allowed_users");
	if (arr) {
		int n = toml_array_nelem(arr);
		char **users = n > 0 ? malloc((size_t)n * sizeof(char *)) : NULL;
		if (n > 0 && !users) {
			ERRBUF_COPY(errbuf, errbufsz, "out of memory allocating telegram allowed_users");
			return -1;
		}
		if (users) {
			for (int i = 0; i < n; i++) {
				toml_datum_t s = toml_string_at(arr, i);
				users[i] = s.ok ? s.u.s : NULL;
			}
			cfg->telegram_allowed_users = users;
			cfg->telegram_allowed_users_count = n;
		}
	}
	return 0;
}

static int parse_memory_skills_sandbox(const toml_table_t *root, config_t *cfg)
{
	const toml_table_t *mem = toml_table_in(root, "memory");
	if (mem) {
		toml_datum_t d = toml_string_in(mem, "db_path");
		if (d.ok) { set_string(&cfg->memory_db_path, d.u.s); free(d.u.s); }
	}
	const toml_table_t *skills = toml_table_in(root, "skills");
	if (skills) {
		toml_datum_t d = toml_string_in(skills, "dir");
		if (d.ok) { set_string(&cfg->skills_dir, d.u.s); free(d.u.s); }
	}
	const toml_table_t *sandbox = toml_table_in(root, "sandbox");
	if (sandbox) {
		toml_datum_t d = toml_bool_in(sandbox, "workspace_only");
		if (d.ok) cfg->workspace_only = d.u.b;
		d = toml_string_in(sandbox, "workspace_path");
		if (d.ok) { set_string(&cfg->workspace_path, d.u.s); free(d.u.s); }
		d = toml_int_in(sandbox, "shell_timeout_sec");
		if (d.ok) cfg->shell_timeout_sec = (int)d.u.i;
	}
	return 0;
}

static int parse_int_env(const char *v, int *out, int min_val, int max_val)
{
	if (!v || !out) return 0;
	char *end = NULL;
	long val = strtol(v, &end, 10);
	if (*end != '\0' || val < min_val || val > max_val) return 0;
	*out = (int)val;
	return 1;
}

static int parse_double_env(const char *v, double *out, double min_val, double max_val)
{
	if (!v || !out) return 0;
	char *end = NULL;
	double val = strtod(v, &end);
	if (*end != '\0' || val < min_val || val > max_val) return 0;
	*out = val;
	return 1;
}

static void apply_env_overrides(config_t *cfg)
{
	const char *v;
	v = getenv(ENV_AGENT_MODEL);
	if (v) set_string(&cfg->agent_model, v);
	v = getenv(ENV_AGENT_MAX_TOKENS);
	if (v) parse_int_env(v, &cfg->agent_max_tokens, 1, INT_MAX);
	v = getenv(ENV_AGENT_TEMPERATURE);
	if (v) parse_double_env(v, &cfg->agent_temperature, 0.0, 2.0);
	v = getenv(ENV_AGENT_MAX_TOOL_ITER);
	if (v) parse_int_env(v, &cfg->agent_max_tool_iterations, 1, 1000);
	v = getenv(ENV_AGENT_MAX_CTX_MSG);
	if (v) parse_int_env(v, &cfg->agent_max_context_messages, 1, 1000);
	v = getenv(ENV_MEMORY_DB_PATH);
	if (v) set_string(&cfg->memory_db_path, v);
	v = getenv(ENV_SKILLS_DIR);
	if (v) set_string(&cfg->skills_dir, v);
	v = getenv(ENV_OPENAI_ENDPOINT);
	if (v) set_string(&cfg->provider_openai_endpoint, v);
	v = getenv(ENV_DEFAULT_PROVIDER);
	if (v) set_string(&cfg->provider_default, v);
}

static void expand_paths(config_t *cfg)
{
	char *s;
	if (cfg->agent_soul_path && cfg->agent_soul_path[0] == '~') {
		s = expand_tilde(cfg->agent_soul_path);
		if (s) { set_string(&cfg->agent_soul_path, s); free(s); }
	}
	if (cfg->agent_identity_path && cfg->agent_identity_path[0] == '~') {
		s = expand_tilde(cfg->agent_identity_path);
		if (s) { set_string(&cfg->agent_identity_path, s); free(s); }
	}
	if (cfg->agent_user_path && cfg->agent_user_path[0] == '~') {
		s = expand_tilde(cfg->agent_user_path);
		if (s) { set_string(&cfg->agent_user_path, s); free(s); }
	}
	if (cfg->memory_db_path && cfg->memory_db_path[0] == '~') {
		s = expand_tilde(cfg->memory_db_path);
		if (s) { set_string(&cfg->memory_db_path, s); free(s); }
	}
	if (cfg->skills_dir && cfg->skills_dir[0] == '~') {
		s = expand_tilde(cfg->skills_dir);
		if (s) { set_string(&cfg->skills_dir, s); free(s); }
	}
	if (cfg->workspace_path && cfg->workspace_path[0] == '~') {
		s = expand_tilde(cfg->workspace_path);
		if (s) { set_string(&cfg->workspace_path, s); free(s); }
	}
}

static int validate_required(const config_t *cfg, char *errbuf, size_t errbufsz)
{
	if (!cfg->agent_model || !cfg->agent_model[0]) {
		ERRBUF_COPY(errbuf, errbufsz, "agent.model is required");
		return -1;
	}
	return 0;
}

int config_load(const char *path, config_t **out, char *errbuf, size_t errbufsz)
{
	if (!path || !out) {
		ERRBUF_COPY(errbuf, errbufsz, "invalid arguments");
		return -1;
	}
	char *resolved = expand_tilde(path);
	if (!resolved) {
		ERRBUF_COPY(errbuf, errbufsz, "failed to expand path");
		return -1;
	}
	FILE *fp = fopen(resolved, "r");
	free(resolved);
	if (!fp) {
		if (errbuf && errbufsz > 0) {
			int n = snprintf(errbuf, errbufsz, "cannot open config file: %s", path);
			if (n >= (int)errbufsz && errbufsz > 4) {
				memcpy(errbuf + errbufsz - 4, "...", 3);
				errbuf[errbufsz - 1] = '\0';
			}
		}
		return -1;
	}
	char errbuf_toml[256];
	toml_table_t *tab = toml_parse_file(fp, errbuf_toml, sizeof(errbuf_toml));
	fclose(fp);
	if (!tab) {
		if (errbuf && errbufsz > 0) snprintf(errbuf, errbufsz, "config parse error: %s", errbuf_toml);
		return -1;
	}
	config_t *cfg = calloc(1, sizeof(*cfg));
	if (!cfg) {
		toml_free(tab);
		ERRBUF_COPY(errbuf, errbufsz, "out of memory");
		return -1;
	}
	cfg->agent_max_tokens = DEFAULT_MAX_TOKENS;
	cfg->agent_temperature = DEFAULT_TEMPERATURE;
	cfg->agent_max_tool_iterations = DEFAULT_MAX_TOOL_ITERATIONS;
	cfg->agent_max_context_messages = DEFAULT_MAX_CONTEXT_MESSAGES;
	set_string(&cfg->provider_default, "anthropic");
	set_string(&cfg->provider_anthropic_api_key_env, "ANTHROPIC_API_KEY");
	set_string(&cfg->provider_openai_api_key_env, "OPENAI_API_KEY");
	set_string(&cfg->provider_openai_endpoint, "https://api.openai.com/v1/chat/completions");
	set_string(&cfg->memory_db_path, "~/.shellclaw/memory.db");
	set_string(&cfg->skills_dir, "~/.shellclaw/skills");
	cfg->workspace_only = 1;
	set_string(&cfg->workspace_path, "~/.shellclaw");
	cfg->shell_timeout_sec = DEFAULT_SHELL_TIMEOUT_SEC;
	int err = parse_agent(tab, cfg, errbuf, errbufsz);
	if (err) goto fail;
	parse_providers(tab, cfg);
	err = parse_telegram(tab, cfg, errbuf, errbufsz);
	if (err) goto fail;
	parse_memory_skills_sandbox(tab, cfg);
	toml_free(tab);
	tab = NULL;
	apply_env_overrides(cfg);
	expand_paths(cfg);
	err = validate_required(cfg, errbuf, errbufsz);
	if (err) goto fail;
	*out = cfg;
	return 0;
fail:
	if (tab) toml_free(tab);
	config_free(cfg);
	return -1;
}

void config_free(config_t *cfg)
{
	if (!cfg) return;
	set_string(&cfg->agent_model, NULL);
	set_string(&cfg->agent_soul_path, NULL);
	set_string(&cfg->agent_identity_path, NULL);
	set_string(&cfg->agent_user_path, NULL);
	set_string(&cfg->provider_default, NULL);
	set_string(&cfg->provider_anthropic_api_key_env, NULL);
	set_string(&cfg->provider_openai_api_key_env, NULL);
	set_string(&cfg->provider_openai_endpoint, NULL);
	set_string(&cfg->telegram_token_env, NULL);
	if (cfg->telegram_allowed_users) {
		for (int i = 0; i < cfg->telegram_allowed_users_count; i++)
			free(cfg->telegram_allowed_users[i]);
		free(cfg->telegram_allowed_users);
		cfg->telegram_allowed_users = NULL;
		cfg->telegram_allowed_users_count = 0;
	}
	set_string(&cfg->memory_db_path, NULL);
	set_string(&cfg->skills_dir, NULL);
	set_string(&cfg->workspace_path, NULL);
	free(cfg);
}

const char *config_agent_model(const config_t *c) { return c ? c->agent_model : NULL; }
int config_agent_max_tokens(const config_t *c) { return c ? c->agent_max_tokens : 0; }
double config_agent_temperature(const config_t *c) { return c ? c->agent_temperature : 0.0; }
int config_agent_max_tool_iterations(const config_t *c) { return c ? c->agent_max_tool_iterations : 0; }
int config_agent_max_context_messages(const config_t *c) { return c ? c->agent_max_context_messages : 0; }
const char *config_agent_soul_path(const config_t *c) { return c ? c->agent_soul_path : NULL; }
const char *config_agent_identity_path(const config_t *c) { return c ? c->agent_identity_path : NULL; }
const char *config_agent_user_path(const config_t *c) { return c ? c->agent_user_path : NULL; }
const char *config_default_provider(const config_t *c) { return c ? c->provider_default : NULL; }
const char *config_provider_anthropic_api_key_env(const config_t *c) { return c ? c->provider_anthropic_api_key_env : NULL; }
const char *config_provider_openai_api_key_env(const config_t *c) { return c ? c->provider_openai_api_key_env : NULL; }
const char *config_provider_openai_endpoint(const config_t *c) { return c ? c->provider_openai_endpoint : NULL; }
int config_telegram_enabled(const config_t *c) { return c ? c->telegram_enabled : 0; }
const char *config_telegram_token_env(const config_t *c) { return c ? c->telegram_token_env : NULL; }
int config_telegram_allowed_users_count(const config_t *c) { return c ? c->telegram_allowed_users_count : 0; }
const char *config_telegram_allowed_user(const config_t *c, int index) {
	if (!c || !c->telegram_allowed_users || index < 0 || index >= c->telegram_allowed_users_count) return NULL;
	return c->telegram_allowed_users[index];
}
const char *config_memory_db_path(const config_t *c) { return c ? c->memory_db_path : NULL; }
const char *config_skills_dir(const config_t *c) { return c ? c->skills_dir : NULL; }
int config_workspace_only(const config_t *c) { return c ? c->workspace_only : 0; }
const char *config_workspace_path(const config_t *c) { return c ? c->workspace_path : NULL; }
int config_shell_timeout_sec(const config_t *c) { return c && c->shell_timeout_sec > 0 ? c->shell_timeout_sec : DEFAULT_SHELL_TIMEOUT_SEC; }
