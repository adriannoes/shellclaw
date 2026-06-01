/**
 * @file manifest_build.c
 * @brief ASAP Manifest JSON tree builders (unsigned manifest document).
 */
#define _POSIX_C_SOURCE 200809L

#include "asap/manifest_build.h"
#include "asap/manifest_profiles.h"
#include "core/config.h"
#include "core/skill.h"
#include "core/version.h"
#include "hardware/board_detect.h"
#include "cJSON.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SKILL_NAMES 64
#define MAX_SKILL_DESC 512
#define MAX_ASAP_ENDPOINT 512
#define MANIFEST_ASAP_VERSION "2.1.0"

static int build_asap_endpoint(char *out, size_t out_size, const config_t *cfg)
{
	const char *base;
	size_t len;
	int n;

	if (!out || out_size == 0) return -1;
	base = config_asap_public_base_url(cfg);
	if (!base || base[0] == '\0') return -1;
	len = strlen(base);
	while (len > 0 && base[len - 1] == '/') len--;
	n = snprintf(out, out_size, "%.*s/asap", (int)len, base);
	return (n < 0 || (size_t)n >= out_size) ? -1 : 0;
}

static const char *skill_description_for(const config_t *cfg, const char *skill_id)
{
	static const struct {
		const char *id;
		const char *desc;
	} defaults[] = {
		{ "assistant", "General assistant on edge hardware" },
		{ "edge_briefing", "Local briefing with optional cloud fallback" },
		{ "server_admin", "Host administration tasks" },
		{ "gpio_control", "GPIO pin control" },
		{ NULL, NULL }
	};
	static char buf[MAX_SKILL_DESC];
	size_t i;
	const char *override;

	if (!skill_id || !skill_id[0]) return NULL;
	override = config_asap_skill_description(cfg, skill_id);
	if (override && override[0] != '\0') return override;
	if (cfg && skill_get_description(cfg, skill_id, buf, sizeof(buf)) == 0 && buf[0] != '\0')
		return buf;
	for (i = 0; defaults[i].id != NULL; i++) {
		if (strcmp(defaults[i].id, skill_id) == 0)
			return defaults[i].desc;
	}
	return skill_id;
}

static cJSON *cjson_add_string_to_object_checked(cJSON *parent, const char *key,
						 const char *value)
{
	cJSON *item = cJSON_CreateString(value);
	if (!item) {
		cJSON_Delete(parent);
		return NULL;
	}
	cJSON_AddItemToObject(parent, key, item);
	return parent;
}

static int count_skills_on_disk(const config_t *cfg)
{
	const char *dir_path;
	DIR *dir;
	struct dirent *ent;
	int total = 0;
	size_t len;

	if (!cfg)
		return 0;
	dir_path = config_skills_dir(cfg);
	if (!dir_path || dir_path[0] == '\0')
		return 0;
	dir = opendir(dir_path);
	if (!dir)
		return 0;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;
		len = strlen(ent->d_name);
		if (len <= 3 || strcmp(ent->d_name + len - 3, ".md") != 0)
			continue;
		total++;
	}
	closedir(dir);
	return total;
}

static int add_capabilities_skills(cJSON *capabilities, const config_t *cfg)
{
	cJSON *skills;
	char *names[MAX_SKILL_NAMES];
	int n;
	int total;
	int i;

	if (!capabilities) return -1;
	skills = cJSON_CreateArray();
	if (!skills) return -1;
	cJSON_AddItemToObject(capabilities, "skills", skills);
	if (!cfg) return 0;
	total = count_skills_on_disk(cfg);
	n = skill_list_names(cfg, names, MAX_SKILL_NAMES);
	if (n < 0) return -1;
	if (total > MAX_SKILL_NAMES)
		fprintf(stderr, "manifest: skills truncated (%d > %d)\n", total,
			MAX_SKILL_NAMES);
	for (i = 0; i < n && i < MAX_SKILL_NAMES; i++) {
		cJSON *item;
		cJSON *id_str;
		cJSON *desc_str;
		const char *desc;
		item = cJSON_CreateObject();
		if (!item) {
			for (int j = i; j < n; j++)
				free(names[j]);
			return -1;
		}
		cJSON_AddItemToArray(skills, item);
		id_str = cJSON_CreateString(names[i]);
		if (!id_str) {
			cJSON_DeleteItemFromArray(skills, cJSON_GetArraySize(skills) - 1);
			for (int j = i; j < n; j++)
				free(names[j]);
			return -1;
		}
		cJSON_AddItemToObject(item, "id", id_str);
		desc = skill_description_for(cfg, names[i]);
		desc_str = cJSON_CreateString(desc ? desc : names[i]);
		if (!desc_str) {
			cJSON_DeleteItemFromArray(skills, cJSON_GetArraySize(skills) - 1);
			for (int j = i; j < n; j++)
				free(names[j]);
			return -1;
		}
		cJSON_AddItemToObject(item, "description", desc_str);
		free(names[i]);
	}
	return 0;
}

static int add_capabilities_hardware(cJSON *capabilities, const config_t *cfg, board_id_t board)
{
	cJSON *hardware;
	cJSON *io_arr;
	const manifest_board_profile_t *profile;
	const char *class_name;
	const char *model_name;
	int io_count;
	int i;

	profile = manifest_board_profile(board);
	class_name = config_hardware_class(cfg);
	if (!class_name || class_name[0] == '\0') class_name = profile->class_name;
	model_name = config_hardware_model(cfg);
	if (!model_name || model_name[0] == '\0') model_name = profile->model_name;
	hardware = cJSON_CreateObject();
	if (!hardware) return -1;
	cJSON_AddItemToObject(capabilities, "hardware", hardware);
	if (!cjson_add_string_to_object_checked(hardware, "class", class_name))
		return -1;
	if (!cjson_add_string_to_object_checked(hardware, "model", model_name))
		return -1;
	io_arr = cJSON_CreateArray();
	if (!io_arr) {
		cJSON_DeleteItemFromObject(capabilities, "hardware");
		return -1;
	}
	cJSON_AddItemToObject(hardware, "io", io_arr);
	io_count = config_hardware_io_count(cfg);
	if (io_count > 0) {
		for (i = 0; i < io_count; i++) {
			cJSON *io_str;
			const char *entry = config_hardware_io_entry(cfg, i);
			if (!entry || entry[0] == '\0')
				continue;
			io_str = cJSON_CreateString(entry);
			if (!io_str) {
				cJSON_DeleteItemFromObject(capabilities, "hardware");
				return -1;
			}
			cJSON_AddItemToArray(io_arr, io_str);
		}
	} else {
		for (i = 0; i < profile->io_count; i++) {
			cJSON *io_str = cJSON_CreateString(profile->io[i]);
			if (!io_str) {
				cJSON_DeleteItemFromObject(capabilities, "hardware");
				return -1;
			}
			cJSON_AddItemToArray(io_arr, io_str);
		}
	}
	return 0;
}

static int add_capabilities_inference(cJSON *capabilities, board_id_t board)
{
	cJSON *inference;
	cJSON *modes;
	cJSON *local_models;
	cJSON *model_entry;
	const manifest_board_profile_t *profile;
	int i;

	profile = manifest_board_profile(board);
	inference = cJSON_CreateObject();
	if (!inference) return -1;
	cJSON_AddItemToObject(capabilities, "inference", inference);
	modes = cJSON_CreateArray();
	if (!modes) {
		cJSON_DeleteItemFromObject(capabilities, "inference");
		return -1;
	}
	cJSON_AddItemToObject(inference, "modes", modes);
	for (i = 0; i < profile->mode_count; i++) {
		cJSON *mode_str = cJSON_CreateString(profile->modes[i]);
		if (!mode_str) {
			cJSON_DeleteItemFromObject(capabilities, "inference");
			return -1;
		}
		cJSON_AddItemToArray(modes, mode_str);
	}
	local_models = cJSON_CreateArray();
	if (!local_models) {
		cJSON_DeleteItemFromObject(capabilities, "inference");
		return -1;
	}
	cJSON_AddItemToObject(inference, "local_models", local_models);
	model_entry = cJSON_CreateObject();
	if (!model_entry) {
		cJSON_DeleteItemFromObject(capabilities, "inference");
		return -1;
	}
	cJSON_AddItemToArray(local_models, model_entry);
	if (!cjson_add_string_to_object_checked(model_entry, "id", profile->local_model_id))
		return -1;
	if (!cjson_add_string_to_object_checked(model_entry, "quantization",
						profile->local_quantization))
		return -1;
	return 0;
}

cJSON *manifest_build_tree(const config_t *cfg)
{
	cJSON *root;
	cJSON *capabilities;
	cJSON *endpoints;
	cJSON *item;
	char asap_url[MAX_ASAP_ENDPOINT];
	board_id_t board;
	const char *urn;
	const char *name;

	root = cJSON_CreateObject();
	if (!root) return NULL;
	urn = config_asap_agent_urn(cfg);
	name = config_asap_agent_name(cfg);
	board = manifest_resolve_board(cfg);
	item = cJSON_CreateString(urn);
	if (!item) {
		cJSON_Delete(root);
		return NULL;
	}
	cJSON_AddItemToObject(root, "id", item);
	item = cJSON_CreateString(name);
	if (!item) {
		cJSON_Delete(root);
		return NULL;
	}
	cJSON_AddItemToObject(root, "name", item);
	item = cJSON_CreateString(SHELLCLAW_RELEASE_VERSION);
	if (!item) {
		cJSON_Delete(root);
		return NULL;
	}
	cJSON_AddItemToObject(root, "version", item);
	item = cJSON_CreateString(config_asap_description(cfg));
	if (!item) {
		cJSON_Delete(root);
		return NULL;
	}
	cJSON_AddItemToObject(root, "description", item);
	capabilities = cJSON_CreateObject();
	if (!capabilities) {
		cJSON_Delete(root);
		return NULL;
	}
	cJSON_AddItemToObject(root, "capabilities", capabilities);
	item = cJSON_CreateString(MANIFEST_ASAP_VERSION);
	if (!item) {
		cJSON_Delete(root);
		return NULL;
	}
	cJSON_AddItemToObject(capabilities, "asap_version", item);
	if (add_capabilities_skills(capabilities, cfg) != 0) {
		cJSON_Delete(root);
		return NULL;
	}
	item = cJSON_CreateFalse();
	if (!item) {
		cJSON_Delete(root);
		return NULL;
	}
	cJSON_AddItemToObject(capabilities, "state_persistence", item);
	item = cJSON_CreateFalse();
	if (!item) {
		cJSON_Delete(root);
		return NULL;
	}
	cJSON_AddItemToObject(capabilities, "streaming", item);
	item = cJSON_CreateArray();
	if (!item) {
		cJSON_Delete(root);
		return NULL;
	}
	cJSON_AddItemToObject(capabilities, "mcp_tools", item);
	if (add_capabilities_hardware(capabilities, cfg, board) != 0) {
		cJSON_Delete(root);
		return NULL;
	}
	if (add_capabilities_inference(capabilities, board) != 0) {
		cJSON_Delete(root);
		return NULL;
	}
	endpoints = cJSON_CreateObject();
	if (!endpoints) {
		cJSON_Delete(root);
		return NULL;
	}
	cJSON_AddItemToObject(root, "endpoints", endpoints);
	if (build_asap_endpoint(asap_url, sizeof(asap_url), cfg) != 0) {
		cJSON_Delete(root);
		return NULL;
	}
	item = cJSON_CreateString(asap_url);
	if (!item) {
		cJSON_Delete(root);
		return NULL;
	}
	cJSON_AddItemToObject(endpoints, "asap", item);
	return root;
}

char *manifest_build_json(const config_t *cfg)
{
	cJSON *root;
	char *out;
	root = manifest_build_tree(cfg);
	if (!root)
		return NULL;
	out = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	return out;
}
