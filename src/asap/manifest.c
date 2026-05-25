/**
 * @file manifest.c
 * @brief ASAP manifest and health JSON builders for well-known discovery.
 */
#define _POSIX_C_SOURCE 200809L

#include "asap/manifest.h"
#include "core/config.h"
#include "core/skill.h"
#include "core/version.h"
#include "crypto/crypto.h"
#include "crypto/jcs.h"
#include "hardware/board_detect.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SKILL_NAMES 64
#define MAX_SKILL_DESC 512
#define MAX_ASAP_ENDPOINT 512
/** ASAP protocol version advertised in capabilities (upstream Manifest schema). */
#define MANIFEST_ASAP_VERSION "2.1.0"
#define MANIFEST_SIG_ALG "ed25519"
#define MANIFEST_TRUST_SELF_SIGNED "self-signed"
#define MANIFEST_B64_SIG_MAX 96
#define MANIFEST_B64_PUB_MAX 48

static const char *const DEFAULT_IO_GPIO_I2C[] = { "gpio", "i2c" };
static const char *const JETSON_MODES[] = { "cloud", "local_cuda" };
static const char *const RPI_MODES[] = { "cloud", "local_cpu" };
static const char *const STUB_MODES[] = { "cloud", "local_cpu" };

typedef struct {
	const char *class_name;
	const char *model_name;
	const char *const *io;
	int io_count;
	const char *const *modes;
	int mode_count;
	const char *local_model_id;
	const char *local_quantization;
} manifest_board_profile_t;

static board_id_t manifest_resolve_board(const config_t *cfg)
{
	board_id_t from_cfg;

	from_cfg = board_id_from_string(config_hardware_board(cfg));
	if (from_cfg != BOARD_UNKNOWN)
		return from_cfg;
	from_cfg = board_detect();
	if (from_cfg != BOARD_UNKNOWN)
		return from_cfg;
	return BOARD_STUB;
}

static const manifest_board_profile_t *manifest_board_profile(board_id_t board)
{
	static const manifest_board_profile_t jetson = {
		.class_name = "edge_accelerator",
		.model_name = "jetson_orin_nano_super_8gb",
		.io = DEFAULT_IO_GPIO_I2C,
		.io_count = 2,
		.modes = JETSON_MODES,
		.mode_count = 2,
		.local_model_id = "Phi-3-mini-4k-instruct-Q4_K_M",
		.local_quantization = "Q4_K_M",
	};
	static const manifest_board_profile_t rpi = {
		.class_name = "sbc",
		.model_name = "raspberry_pi_zero_2_w",
		.io = DEFAULT_IO_GPIO_I2C,
		.io_count = 2,
		.modes = RPI_MODES,
		.mode_count = 2,
		.local_model_id = "tinyllama-1.1b-chat-Q4_K_M",
		.local_quantization = "Q4_K_M",
	};
	static const manifest_board_profile_t stub = {
		.class_name = "sbc",
		.model_name = "stub",
		.io = NULL,
		.io_count = 0,
		.modes = STUB_MODES,
		.mode_count = 2,
		.local_model_id = "tinyllama-1.1b-chat-Q4_K_M",
		.local_quantization = "Q4_K_M",
	};

	switch (board) {
	case BOARD_JETSON_ORIN_NANO:
		return &jetson;
	case BOARD_RPI_ZERO2W:
		return &rpi;
	case BOARD_STUB:
	case BOARD_UNKNOWN:
	default:
		return &stub;
	}
}

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

static int add_capabilities_skills(cJSON *capabilities, const config_t *cfg)
{
	cJSON *skills;
	char *names[MAX_SKILL_NAMES];
	int n;
	int i;

	if (!capabilities) return -1;
	skills = cJSON_CreateArray();
	if (!skills) return -1;
	cJSON_AddItemToObject(capabilities, "skills", skills);
	if (!cfg) return 0;
	n = skill_list_names(cfg, names, MAX_SKILL_NAMES);
	/* n < 0: invalid args or OOM; skill_list_names frees partial entries before -1. */
	if (n < 0) return -1;
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
	cJSON *class_str;
	cJSON *model_str;
	const manifest_board_profile_t *profile;
	const char *class_name;
	const char *model_name;
	int io_count;
	int i;

	profile = manifest_board_profile(board);
	if (!profile) return -1;
	class_name = config_hardware_class(cfg);
	if (!class_name || class_name[0] == '\0') class_name = profile->class_name;
	model_name = config_hardware_model(cfg);
	if (!model_name || model_name[0] == '\0') model_name = profile->model_name;
	hardware = cJSON_CreateObject();
	if (!hardware) return -1;
	cJSON_AddItemToObject(capabilities, "hardware", hardware);
	class_str = cJSON_CreateString(class_name);
	if (!class_str) {
		cJSON_DeleteItemFromObject(capabilities, "hardware");
		return -1;
	}
	cJSON_AddItemToObject(hardware, "class", class_str);
	model_str = cJSON_CreateString(model_name);
	if (!model_str) {
		cJSON_DeleteItemFromObject(capabilities, "hardware");
		return -1;
	}
	cJSON_AddItemToObject(hardware, "model", model_str);
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
	cJSON *id_str;
	cJSON *quant_str;
	const manifest_board_profile_t *profile;
	int i;

	profile = manifest_board_profile(board);
	if (!profile) return -1;
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
	id_str = cJSON_CreateString(profile->local_model_id);
	if (!id_str) {
		cJSON_DeleteItemFromObject(capabilities, "inference");
		return -1;
	}
	cJSON_AddItemToObject(model_entry, "id", id_str);
	quant_str = cJSON_CreateString(profile->local_quantization);
	if (!quant_str) {
		cJSON_DeleteItemFromObject(capabilities, "inference");
		return -1;
	}
	cJSON_AddItemToObject(model_entry, "quantization", quant_str);
	return 0;
}

static cJSON *manifest_build_tree(const config_t *cfg)
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

char *manifest_build_signed_json(const config_t *cfg)
{
	cJSON *manifest;
	cJSON *wrapper;
	cJSON *sig_block;
	unsigned char *canonical;
	size_t canon_len;
	uint8_t sig_raw[CRYPTO_ED25519_SIGNATURE_SIZE];
	char sig_b64[MANIFEST_B64_SIG_MAX];
	char pub_b64[MANIFEST_B64_PUB_MAX];
	const uint8_t *priv;
	const uint8_t *pub;
	char *out;
	int b64_len;

	if (manifest_keys_load() != 0)
		return NULL;
	priv = manifest_keys_private();
	pub = manifest_keys_public();
	if (!priv || !pub)
		return NULL;
	manifest = manifest_build_tree(cfg);
	if (!manifest)
		return NULL;
	canonical = NULL;
	canon_len = 0;
	if (jcs_canonicalize(manifest, &canonical, &canon_len) != 0) {
		cJSON_Delete(manifest);
		return NULL;
	}
	if (crypto_ed25519_sign(priv, CRYPTO_ED25519_PRIVATE_KEY_SIZE,
			canonical, canon_len, sig_raw, sizeof(sig_raw)) != 0) {
		free(canonical);
		cJSON_Delete(manifest);
		return NULL;
	}
	free(canonical);
	b64_len = crypto_base64_encode(sig_raw, sizeof(sig_raw), sig_b64, sizeof(sig_b64));
	if (b64_len < 0) {
		cJSON_Delete(manifest);
		return NULL;
	}
	b64_len = crypto_base64_encode(pub, CRYPTO_ED25519_PUBLIC_KEY_SIZE,
			pub_b64, sizeof(pub_b64));
	if (b64_len < 0) {
		cJSON_Delete(manifest);
		return NULL;
	}
	wrapper = cJSON_CreateObject();
	if (!wrapper) {
		cJSON_Delete(manifest);
		return NULL;
	}
	cJSON_AddItemToObject(wrapper, "manifest", manifest);
	sig_block = cJSON_CreateObject();
	if (!sig_block) {
		cJSON_Delete(wrapper);
		return NULL;
	}
	cJSON_AddItemToObject(wrapper, "signature", sig_block);
	{
		cJSON *alg_item = cJSON_CreateString(MANIFEST_SIG_ALG);
		cJSON *sig_item = cJSON_CreateString(sig_b64);
		cJSON *trust_item = cJSON_CreateString(MANIFEST_TRUST_SELF_SIGNED);
		cJSON *pub_item = cJSON_CreateString(pub_b64);

		if (!alg_item || !sig_item || !trust_item || !pub_item) {
			if (alg_item) cJSON_Delete(alg_item);
			if (sig_item) cJSON_Delete(sig_item);
			if (trust_item) cJSON_Delete(trust_item);
			if (pub_item) cJSON_Delete(pub_item);
			cJSON_Delete(wrapper);
			return NULL;
		}
		cJSON_AddItemToObject(sig_block, "alg", alg_item);
		cJSON_AddItemToObject(sig_block, "signature", sig_item);
		cJSON_AddItemToObject(sig_block, "trust_level", trust_item);
		cJSON_AddItemToObject(wrapper, "public_key", pub_item);
	}
	out = cJSON_PrintUnformatted(wrapper);
	cJSON_Delete(wrapper);
	return out;
}

static const char *HEALTH_JSON = "{\"status\":\"ok\"}";

const char *manifest_health_json(void)
{
	return HEALTH_JSON;
}
