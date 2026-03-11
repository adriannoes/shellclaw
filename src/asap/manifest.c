/**
 * @file manifest.c
 * @brief ASAP manifest and health JSON builders for well-known discovery.
 */
#define _POSIX_C_SOURCE 200809L

#include "asap/manifest.h"
#include "core/config.h"
#include "core/skill.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>

#define ASAP_VERSION "0.2.0"
#define MAX_SKILL_NAMES 64

char *manifest_build_json(const config_t *cfg)
{
	cJSON *root = cJSON_CreateObject();
	if (!root) return NULL;
	const char *urn = config_asap_agent_urn(cfg);
	const char *name = config_asap_agent_name(cfg);
	cJSON_AddItemToObject(root, "id", cJSON_CreateString(urn));
	cJSON_AddItemToObject(root, "name", cJSON_CreateString(name));
	cJSON_AddItemToObject(root, "version", cJSON_CreateString(ASAP_VERSION));
	cJSON *skills = cJSON_CreateArray();
	if (!skills) { cJSON_Delete(root); return NULL; }
	cJSON_AddItemToObject(root, "skills", skills);
	if (cfg) {
		char *names[MAX_SKILL_NAMES];
		int n = skill_list_names(cfg, names, MAX_SKILL_NAMES);
		for (int i = 0; i < n && i < MAX_SKILL_NAMES; i++) {
			cJSON_AddItemToArray(skills, cJSON_CreateString(names[i]));
			free(names[i]);
		}
	}
	cJSON *endpoints = cJSON_CreateObject();
	if (!endpoints) { cJSON_Delete(root); return NULL; }
	cJSON_AddItemToObject(root, "endpoints", endpoints);
	cJSON_AddItemToObject(endpoints, "asap", cJSON_CreateString("/asap"));
	cJSON_AddItemToObject(endpoints, "health", cJSON_CreateString("/.well-known/asap/health"));
	cJSON_AddItemToObject(endpoints, "manifest", cJSON_CreateString("/.well-known/asap/manifest.json"));
	char *out = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	return out;
}

static const char *HEALTH_JSON = "{\"status\":\"ok\"}";

const char *manifest_health_json(void)
{
	return HEALTH_JSON;
}
