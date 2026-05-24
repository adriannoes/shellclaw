/**
 * @file test_manifest.c
 * @brief Unit tests for ASAP manifest and health JSON (upstream Manifest v2.4 shape).
 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "asap/manifest.h"
#include "test_runner.h"
#include "core/config.h"
#include "core/version.h"
#include "crypto/crypto.h"
#include "crypto/jcs.h"
#include "hardware/board_detect.h"
#include "cJSON.h"
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
#define TMP_CONFIG "/tmp/shellclaw_test_manifest_config.toml"
#define MANIFEST_ASAP_VERSION "2.1.0"

static int assert_manifest_shape(cJSON *parsed, const char *expect_id,
	const char *expect_version, const char *expect_hw_class,
	const char *expect_hw_model, const char *expect_mode0,
	const char *expect_local_model)
{
	cJSON *id;
	cJSON *version;
	cJSON *description;
	cJSON *capabilities;
	cJSON *skills;
	cJSON *hardware;
	cJSON *inference;
	cJSON *modes;
	cJSON *local_models;
	cJSON *endpoints;
	cJSON *asap_ep;
	cJSON *state_persistence;
	cJSON *streaming;
	cJSON *mcp_tools;

	id = cJSON_GetObjectItem(parsed, "id");
	ASSERT(id != NULL && cJSON_IsString(id));
	ASSERT(strcmp(id->valuestring, expect_id) == 0);
	version = cJSON_GetObjectItem(parsed, "version");
	ASSERT(version != NULL && cJSON_IsString(version));
	ASSERT(strcmp(version->valuestring, expect_version) == 0);
	description = cJSON_GetObjectItem(parsed, "description");
	ASSERT(description != NULL && cJSON_IsString(description));
	ASSERT(description->valuestring[0] != '\0');
	ASSERT(cJSON_GetObjectItem(parsed, "skills") == NULL);
	capabilities = cJSON_GetObjectItem(parsed, "capabilities");
	ASSERT(capabilities != NULL && cJSON_IsObject(capabilities));
	ASSERT(strcmp(cJSON_GetObjectItem(capabilities, "asap_version")->valuestring,
		MANIFEST_ASAP_VERSION) == 0);
	skills = cJSON_GetObjectItem(capabilities, "skills");
	ASSERT(skills != NULL && cJSON_IsArray(skills));
	if (cJSON_GetArraySize(skills) > 0) {
		cJSON *first = cJSON_GetArrayItem(skills, 0);
		ASSERT(cJSON_GetObjectItem(first, "id") != NULL);
		ASSERT(cJSON_GetObjectItem(first, "description") != NULL);
	}
	state_persistence = cJSON_GetObjectItem(capabilities, "state_persistence");
	ASSERT(state_persistence != NULL && cJSON_IsFalse(state_persistence));
	streaming = cJSON_GetObjectItem(capabilities, "streaming");
	ASSERT(streaming != NULL && cJSON_IsFalse(streaming));
	mcp_tools = cJSON_GetObjectItem(capabilities, "mcp_tools");
	ASSERT(mcp_tools != NULL && cJSON_IsArray(mcp_tools));
	ASSERT(cJSON_GetArraySize(mcp_tools) == 0);
	hardware = cJSON_GetObjectItem(capabilities, "hardware");
	ASSERT(hardware != NULL && cJSON_IsObject(hardware));
	ASSERT(strcmp(cJSON_GetObjectItem(hardware, "class")->valuestring, expect_hw_class) == 0);
	ASSERT(strcmp(cJSON_GetObjectItem(hardware, "model")->valuestring, expect_hw_model) == 0);
	ASSERT(cJSON_GetObjectItem(hardware, "io") != NULL);
	inference = cJSON_GetObjectItem(capabilities, "inference");
	ASSERT(inference != NULL && cJSON_IsObject(inference));
	modes = cJSON_GetObjectItem(inference, "modes");
	ASSERT(modes != NULL && cJSON_IsArray(modes));
	ASSERT(cJSON_GetArraySize(modes) >= 1);
	ASSERT(strcmp(cJSON_GetArrayItem(modes, 0)->valuestring, expect_mode0) == 0);
	local_models = cJSON_GetObjectItem(inference, "local_models");
	ASSERT(local_models != NULL && cJSON_GetArraySize(local_models) == 1);
	ASSERT(strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(local_models, 0), "id")->valuestring,
		expect_local_model) == 0);
	endpoints = cJSON_GetObjectItem(parsed, "endpoints");
	ASSERT(endpoints != NULL && cJSON_IsObject(endpoints));
	asap_ep = cJSON_GetObjectItem(endpoints, "asap");
	ASSERT(asap_ep != NULL && cJSON_IsString(asap_ep));
	ASSERT(strstr(asap_ep->valuestring, "://") != NULL);
	ASSERT(strstr(asap_ep->valuestring, "/asap") != NULL);
	ASSERT(cJSON_GetObjectItem(endpoints, "health") == NULL);
	ASSERT(cJSON_GetObjectItem(endpoints, "manifest") == NULL);
	return 0;
}

static int test_health_json(void)
{
	const char *s = manifest_health_json();
	ASSERT(s != NULL);
	ASSERT(strstr(s, "status") != NULL);
	ASSERT(strstr(s, "ok") != NULL);
	cJSON *parsed = cJSON_Parse(s);
	ASSERT(parsed != NULL);
	cJSON *status = cJSON_GetObjectItem(parsed, "status");
	ASSERT(status != NULL);
	ASSERT(cJSON_IsString(status));
	ASSERT(strcmp(status->valuestring, "ok") == 0);
	cJSON_Delete(parsed);
	return 0;
}

static int test_manifest_json_null_config_stub(void)
{
	char *json = manifest_build_json(NULL);
	cJSON *parsed;
	ASSERT(json != NULL);
	parsed = cJSON_Parse(json);
	ASSERT(parsed != NULL);
	ASSERT(assert_manifest_shape(parsed, "urn:asap:agent:shellclaw", SHELLCLAW_RELEASE_VERSION,
		"sbc", "stub", "cloud", "tinyllama-1.1b-chat-Q4_K_M") == 0);
	ASSERT(strstr(json, "https://shellclaw.example.com/asap") != NULL);
	cJSON_Delete(parsed);
	free(json);
	return 0;
}

static int test_manifest_jetson_board(void)
{
	char *json;
	cJSON *parsed;
	const char *path = "/tmp/shellclaw_test_manifest_jetson_compat";
	FILE *f;

	f = fopen(path, "w");
	ASSERT(f);
	fprintf(f, "nvidia,p3768-0005-0000-a0\n");
	fclose(f);
	board_detect_set_path_for_test(path);
	setenv("SHELLCLAW_BOARD", "jetson", 1);
	json = manifest_build_json(NULL);
	board_detect_set_path_for_test(NULL);
	unsetenv("SHELLCLAW_BOARD");
	unlink(path);
	ASSERT(json != NULL);
	parsed = cJSON_Parse(json);
	ASSERT(parsed != NULL);
	ASSERT(assert_manifest_shape(parsed, "urn:asap:agent:shellclaw", SHELLCLAW_RELEASE_VERSION,
		"edge_accelerator", "jetson_orin_nano_super_8gb", "cloud",
		"Phi-3-mini-4k-instruct-Q4_K_M") == 0);
	ASSERT(strstr(json, "local_cuda") != NULL);
	cJSON_Delete(parsed);
	free(json);
	return 0;
}

static int test_manifest_rpi_board(void)
{
	char *json;
	cJSON *parsed;

	setenv("SHELLCLAW_BOARD", "rpi", 1);
	json = manifest_build_json(NULL);
	unsetenv("SHELLCLAW_BOARD");
	ASSERT(json != NULL);
	parsed = cJSON_Parse(json);
	ASSERT(parsed != NULL);
	ASSERT(assert_manifest_shape(parsed, "urn:asap:agent:shellclaw", SHELLCLAW_RELEASE_VERSION,
		"sbc", "raspberry_pi_zero_2_w", "cloud", "tinyllama-1.1b-chat-Q4_K_M") == 0);
	ASSERT(strstr(json, "local_cpu") != NULL);
	cJSON_Delete(parsed);
	free(json);
	return 0;
}

static int test_manifest_json_with_config(void)
{
	FILE *f = fopen(TMP_CONFIG, "w");
	ASSERT(f);
	fprintf(f, "[agent]\nmodel = \"test\"\n");
	fprintf(f, "[asap]\nagent_urn = \"urn:asap:agent:my-custom\"\n");
	fprintf(f, "agent_name = \"My Agent\"\n");
	fprintf(f, "description = \"Custom manifest description\"\n");
	fprintf(f, "public_base_url = \"https://agent.example\"\n");
	fprintf(f, "[skills]\ndir = \"/tmp/shellclaw_test_manifest_skills\"\n");
	fclose(f);
	{
		char errbuf[256];
		config_t *cfg = NULL;
		char *json;
		cJSON *parsed;
		int r = config_load(TMP_CONFIG, &cfg, errbuf, sizeof(errbuf));
		ASSERT(r == 0);
		json = manifest_build_json(cfg);
		ASSERT(json != NULL);
		ASSERT(strstr(json, "urn:asap:agent:my-custom") != NULL);
		ASSERT(strstr(json, "My Agent") != NULL);
		ASSERT(strstr(json, "Custom manifest description") != NULL);
		ASSERT(strstr(json, "https://agent.example/asap") != NULL);
		parsed = cJSON_Parse(json);
		ASSERT(parsed != NULL);
		ASSERT(strcmp(cJSON_GetObjectItem(parsed, "id")->valuestring,
			"urn:asap:agent:my-custom") == 0);
		cJSON_Delete(parsed);
		free(json);
		config_free(cfg);
	}
	unlink(TMP_CONFIG);
	return 0;
}

static int test_manifest_skill_objects(void)
{
	const char *dir = "/tmp/shellclaw_test_manifest_skills_obj";
	char cmd[256];
	FILE *f;
	config_t *cfg = NULL;
	char errbuf[256];
	char *json;
	cJSON *parsed;
	cJSON *skills;
	cJSON *item;

	snprintf(cmd, sizeof(cmd), "rm -rf \"%s\" && mkdir -p \"%s\"", dir, dir);
	ASSERT(system(cmd) == 0);
	f = fopen("/tmp/shellclaw_test_manifest_skills_obj/demo.md", "w");
	ASSERT(f);
	fprintf(f, "# Demo skill headline\nBody ignored for manifest.\n");
	fclose(f);
	f = fopen(TMP_CONFIG, "w");
	ASSERT(f);
	fprintf(f, "[agent]\nmodel = \"test\"\n");
	fprintf(f, "[asap]\n");
	fprintf(f, "[asap.skill_descriptions]\n");
	fprintf(f, "override_skill = \"From config table\"\n");
	fprintf(f, "[skills]\ndir = \"%s\"\n", dir);
	fclose(f);
	ASSERT(config_load(TMP_CONFIG, &cfg, errbuf, sizeof(errbuf)) == 0);
	json = manifest_build_json(cfg);
	ASSERT(json != NULL);
	parsed = cJSON_Parse(json);
	ASSERT(parsed != NULL);
	skills = cJSON_GetObjectItem(cJSON_GetObjectItem(parsed, "capabilities"), "skills");
	ASSERT(skills != NULL);
	item = NULL;
	for (int i = 0; i < cJSON_GetArraySize(skills); i++) {
		cJSON *s = cJSON_GetArrayItem(skills, i);
		const char *sid = cJSON_GetObjectItem(s, "id")->valuestring;
		if (strcmp(sid, "demo") == 0)
			item = s;
		if (strcmp(sid, "override_skill") == 0)
			ASSERT(strcmp(cJSON_GetObjectItem(s, "description")->valuestring,
				"From config table") == 0);
	}
	ASSERT(item != NULL);
	ASSERT(strcmp(cJSON_GetObjectItem(item, "description")->valuestring,
		"Demo skill headline") == 0);
	cJSON_Delete(parsed);
	free(json);
	config_free(cfg);
	unlink(TMP_CONFIG);
	snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", dir);
	(void)system(cmd);
	return 0;
}

static const uint8_t MANIFEST_KEYS_TEST_SEED[32] = {
	0x5aU, 0x11U, 0x51U, 0xa4U, 0x59U, 0xfaU, 0xeaU, 0xdeU,
	0x3dU, 0x24U, 0x71U, 0x15U, 0xf9U, 0x4aU, 0xedU, 0xaeU,
	0x42U, 0x31U, 0x81U, 0x24U, 0x09U, 0x5aU, 0xfaU, 0xbeU,
	0x4dU, 0x14U, 0x51U, 0xa5U, 0x59U, 0xfaU, 0xedU, 0xeeU
};

static const uint8_t MANIFEST_KEYS_ROTATE_SEED[32] = {
	0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U,
	0x09U, 0x0aU, 0x0bU, 0x0cU, 0x0dU, 0x0eU, 0x0fU, 0x10U,
	0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U, 0x17U, 0x18U,
	0x19U, 0x1aU, 0x1bU, 0x1cU, 0x1dU, 0x1eU, 0x1fU, 0x20U
};

static int file_mode_is_0600(const char *path)
{
	struct stat st;
	if (stat(path, &st) != 0)
		return 0;
	return (st.st_mode & 0777U) == 0600U;
}

static int test_manifest_keys_first_run_creates(void)
{
	char dir[128];
	char priv_path[512];
	char pub_path[512];

	ASSERT(test_runner_mkdtemp_path("shellclaw_manifest_keys", dir, sizeof(dir)) == 0);
	snprintf(priv_path, sizeof(priv_path), "%s/ed25519.priv", dir);
	snprintf(pub_path, sizeof(pub_path), "%s/ed25519.pub", dir);
	manifest_keys_set_dir_for_test(dir);
	crypto_test_set_randombytes_seed(MANIFEST_KEYS_TEST_SEED);
	ASSERT(manifest_keys_load() == 0);
	crypto_test_clear_randombytes_seed();
	ASSERT(access(priv_path, F_OK) == 0);
	ASSERT(access(pub_path, F_OK) == 0);
	ASSERT(file_mode_is_0600(priv_path));
	ASSERT(file_mode_is_0600(pub_path));
	{
		FILE *f = fopen(priv_path, "rb");
		long sz;
		ASSERT(f != NULL);
		fseek(f, 0, SEEK_END);
		sz = ftell(f);
		fclose(f);
		ASSERT(sz == (long)CRYPTO_ED25519_PRIVATE_KEY_SIZE);
	}
	{
		FILE *f = fopen(pub_path, "rb");
		long sz;
		ASSERT(f != NULL);
		fseek(f, 0, SEEK_END);
		sz = ftell(f);
		fclose(f);
		ASSERT(sz == (long)CRYPTO_ED25519_PUBLIC_KEY_SIZE);
	}
	manifest_keys_reset_for_test();
	manifest_keys_set_dir_for_test(NULL);
	snprintf(priv_path, sizeof(priv_path), "rm -rf \"%s\"", dir);
	(void)system(priv_path);
	return 0;
}

static int test_manifest_keys_second_run_reuses(void)
{
	char dir[128];
	uint8_t pub_disk[CRYPTO_ED25519_PUBLIC_KEY_SIZE];
	const uint8_t *pub_mem;
	FILE *f;

	ASSERT(test_runner_mkdtemp_path("shellclaw_manifest_keys2", dir, sizeof(dir)) == 0);
	manifest_keys_set_dir_for_test(dir);
	crypto_test_set_randombytes_seed(MANIFEST_KEYS_TEST_SEED);
	ASSERT(manifest_keys_load() == 0);
	pub_mem = manifest_keys_public();
	ASSERT(pub_mem != NULL);
	{
		char pub_path[512];
		snprintf(pub_path, sizeof(pub_path), "%s/ed25519.pub", dir);
		f = fopen(pub_path, "rb");
		ASSERT(f != NULL);
		ASSERT(fread(pub_disk, 1, sizeof(pub_disk), f) == sizeof(pub_disk));
		fclose(f);
		ASSERT(memcmp(pub_disk, pub_mem, sizeof(pub_disk)) == 0);
	}
	manifest_keys_reset_for_test();
	ASSERT(manifest_keys_load() == 0);
	ASSERT(memcmp(manifest_keys_public(), pub_disk, sizeof(pub_disk)) == 0);
	crypto_test_clear_randombytes_seed();
	manifest_keys_reset_for_test();
	manifest_keys_set_dir_for_test(NULL);
	{
		char cmd[512];
		snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", dir);
		(void)system(cmd);
	}
	return 0;
}

static int is_valid_base64(const char *s)
{
	size_t len;
	size_t i;
	if (!s || s[0] == '\0')
		return 0;
	len = strlen(s);
	if (len % 4U != 0U)
		return 0;
	for (i = 0; i < len; i++) {
		char c = s[i];
		if (c == '=') {
			if (i < len - 2U)
				return 0;
			continue;
		}
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		    (c >= '0' && c <= '9') || c == '+' || c == '/')
			continue;
		return 0;
	}
	return 1;
}

static int test_jcs_canonicalize_sorted_keys(void)
{
	cJSON *root;
	unsigned char *out;
	size_t out_len;
	const char *expect = "{\"a\":1,\"z\":2}";

	root = cJSON_Parse("{\"z\":2,\"a\":1}");
	ASSERT(root != NULL);
	ASSERT(jcs_canonicalize(root, &out, &out_len) == 0);
	ASSERT(out_len == strlen(expect));
	ASSERT(memcmp(out, expect, out_len) == 0);
	free(out);
	cJSON_Delete(root);
	return 0;
}

static int test_jcs_rejects_invalid_inputs(void)
{
	cJSON *nan_node;
	unsigned char *out = NULL;
	size_t out_len = 0;

	ASSERT(jcs_canonicalize(NULL, &out, &out_len) == -1);
	nan_node = cJSON_CreateNumber(NAN);
	ASSERT(nan_node != NULL);
	ASSERT(jcs_canonicalize(nan_node, &out, &out_len) == -1);
	cJSON_Delete(nan_node);
	return 0;
}

static int test_jcs_escapes_primitives_and_arrays(void)
{
	cJSON *root;
	unsigned char *out;
	size_t out_len;
	const char *expect =
	    "{\"arr\":[null,true,false,42,1.5,\"a\\tb\\nc\"],\"empty\":{}}";

	root = cJSON_Parse(
	    "{\"empty\":{},\"arr\":[null,true,false,42,1.5,\"a\\tb\\nc\"]}");
	ASSERT(root != NULL);
	ASSERT(jcs_canonicalize(root, &out, &out_len) == 0);
	ASSERT(out_len == strlen(expect));
	ASSERT(memcmp(out, expect, out_len) == 0);
	free(out);
	cJSON_Delete(root);
	return 0;
}

static int test_manifest_hardware_io_from_config(void)
{
	FILE *f = fopen(TMP_CONFIG, "w");
	config_t *cfg = NULL;
	char errbuf[256];
	char *json;
	cJSON *parsed;
	cJSON *io;
	cJSON *entry;

	ASSERT(f);
	fprintf(f, "[agent]\nmodel = \"test\"\n");
	fprintf(f, "[asap]\npublic_base_url = \"https://edge.example/\"\n");
	fprintf(f, "[hardware]\nclass = \"custom_class\"\nmodel = \"custom_model\"\n");
	fprintf(f, "io = [\"spi\", \"i2c\"]\n");
	fclose(f);
	ASSERT(config_load(TMP_CONFIG, &cfg, errbuf, sizeof(errbuf)) == 0);
	json = manifest_build_json(cfg);
	ASSERT(json != NULL);
	ASSERT(strstr(json, "https://edge.example/asap") != NULL);
	ASSERT(strstr(json, "custom_class") != NULL);
	ASSERT(strstr(json, "spi") != NULL);
	parsed = cJSON_Parse(json);
	ASSERT(parsed != NULL);
	io = cJSON_GetObjectItem(
	    cJSON_GetObjectItem(cJSON_GetObjectItem(parsed, "capabilities"), "hardware"),
	    "io");
	ASSERT(io != NULL && cJSON_GetArraySize(io) == 2);
	entry = cJSON_GetArrayItem(io, 0);
	ASSERT(entry != NULL && strcmp(entry->valuestring, "spi") == 0);
	cJSON_Delete(parsed);
	free(json);
	config_free(cfg);
	unlink(TMP_CONFIG);
	return 0;
}

static int test_manifest_build_signed_without_keys(void)
{
	char *signed_json;

	manifest_keys_reset_for_test();
	manifest_keys_set_dir_for_test(NULL);
	signed_json = manifest_build_signed_json(NULL);
	ASSERT(signed_json == NULL);
	return 0;
}

static int test_signed_manifest_structure_and_verify(void)
{
	char dir[128];
	char *signed_json;
	cJSON *root;
	cJSON *manifest;
	cJSON *signature;
	cJSON *alg;
	cJSON *sig_b64;
	cJSON *trust;
	cJSON *pub_b64;
	unsigned char *canonical;
	size_t canon_len;
	uint8_t sig_raw[CRYPTO_ED25519_SIGNATURE_SIZE];
	uint8_t pub_raw[CRYPTO_ED25519_PUBLIC_KEY_SIZE];
	int sig_len;
	int pub_len;
	int ok;

	ASSERT(test_runner_mkdtemp_path("shellclaw_manifest_signed", dir, sizeof(dir)) == 0);
	manifest_keys_set_dir_for_test(dir);
	manifest_keys_reset_for_test();
	crypto_test_set_randombytes_seed(MANIFEST_KEYS_TEST_SEED);
	signed_json = manifest_build_signed_json(NULL);
	ASSERT(signed_json != NULL);
	crypto_test_clear_randombytes_seed();
	root = cJSON_Parse(signed_json);
	ASSERT(root != NULL);
	manifest = cJSON_GetObjectItem(root, "manifest");
	ASSERT(manifest != NULL && cJSON_IsObject(manifest));
	signature = cJSON_GetObjectItem(root, "signature");
	ASSERT(signature != NULL && cJSON_IsObject(signature));
	alg = cJSON_GetObjectItem(signature, "alg");
	ASSERT(alg != NULL && strcmp(alg->valuestring, "ed25519") == 0);
	sig_b64 = cJSON_GetObjectItem(signature, "signature");
	ASSERT(sig_b64 != NULL && is_valid_base64(sig_b64->valuestring));
	ASSERT(strlen(sig_b64->valuestring) == 88U);
	trust = cJSON_GetObjectItem(signature, "trust_level");
	ASSERT(trust != NULL && strcmp(trust->valuestring, "self-signed") == 0);
	pub_b64 = cJSON_GetObjectItem(root, "public_key");
	ASSERT(pub_b64 != NULL && is_valid_base64(pub_b64->valuestring));
	ASSERT(strlen(pub_b64->valuestring) == 44U);
	ASSERT(jcs_canonicalize(manifest, &canonical, &canon_len) == 0);
	sig_len = crypto_base64_decode(sig_b64->valuestring, sig_raw, sizeof(sig_raw));
	ASSERT(sig_len == (int)CRYPTO_ED25519_SIGNATURE_SIZE);
	pub_len = crypto_base64_decode(pub_b64->valuestring, pub_raw, sizeof(pub_raw));
	ASSERT(pub_len == (int)CRYPTO_ED25519_PUBLIC_KEY_SIZE);
	ok = crypto_ed25519_verify(pub_raw, (size_t)pub_len, canonical, canon_len,
		sig_raw, (size_t)sig_len);
	ASSERT(ok == 1);
	free(canonical);
	cJSON_Delete(root);
	free(signed_json);
	manifest_keys_reset_for_test();
	manifest_keys_set_dir_for_test(NULL);
	{
		char cmd[512];
		snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", dir);
		(void)system(cmd);
	}
	return 0;
}

static int find_one_backup(const char *keys_dir, const char *prefix, char *out, size_t out_sz)
{
	DIR *d;
	struct dirent *ent;
	int found;

	found = 0;
	d = opendir(keys_dir);
	if (!d)
		return -1;
	while ((ent = readdir(d)) != NULL) {
		size_t plen;
		if (strncmp(ent->d_name, prefix, strlen(prefix)) != 0)
			continue;
		plen = strlen(prefix);
		if (ent->d_name[plen] == '\0' || strncmp(ent->d_name + plen, ".bak.", 5) != 0)
			continue;
		if (snprintf(out, out_sz, "%s/%s", keys_dir, ent->d_name) >= (int)out_sz)
			continue;
		found = 1;
		break;
	}
	closedir(d);
	return found ? 0 : -1;
}

static int read_live_keypair(const char *priv_path, const char *pub_path,
	uint8_t *priv_out, uint8_t *pub_out)
{
	FILE *f;

	f = fopen(priv_path, "rb");
	if (!f)
		return -1;
	if (fread(priv_out, 1, CRYPTO_ED25519_PRIVATE_KEY_SIZE, f) !=
	    CRYPTO_ED25519_PRIVATE_KEY_SIZE) {
		fclose(f);
		return -1;
	}
	fclose(f);
	f = fopen(pub_path, "rb");
	if (!f)
		return -1;
	if (fread(pub_out, 1, CRYPTO_ED25519_PUBLIC_KEY_SIZE, f) !=
	    CRYPTO_ED25519_PUBLIC_KEY_SIZE) {
		fclose(f);
		return -1;
	}
	fclose(f);
	return 0;
}

static int test_manifest_keys_rotate_fails_preserves_keys(void)
{
	char dir[128];
	char priv_path[512];
	char pub_path[512];
	uint8_t priv_before[CRYPTO_ED25519_PRIVATE_KEY_SIZE];
	uint8_t pub_before[CRYPTO_ED25519_PUBLIC_KEY_SIZE];
	uint8_t priv_after[CRYPTO_ED25519_PRIVATE_KEY_SIZE];
	uint8_t pub_after[CRYPTO_ED25519_PUBLIC_KEY_SIZE];
	char err[256];
	char cmd[512];

	ASSERT(test_runner_mkdtemp_path("shellclaw_manifest_rotate_fail", dir,
			sizeof(dir)) == 0);
	snprintf(priv_path, sizeof(priv_path), "%s/ed25519.priv", dir);
	snprintf(pub_path, sizeof(pub_path), "%s/ed25519.pub", dir);
	manifest_keys_set_dir_for_test(dir);
	crypto_test_set_randombytes_seed(MANIFEST_KEYS_TEST_SEED);
	ASSERT(manifest_keys_load() == 0);
	ASSERT(read_live_keypair(priv_path, pub_path, priv_before, pub_before) == 0);
	crypto_test_clear_randombytes_seed();
	manifest_keys_reset_for_test();

	ASSERT(chmod(dir, 0555) == 0);
	ASSERT(manifest_keys_rotate(err, sizeof(err)) != 0);
	ASSERT(chmod(dir, 0755) == 0);
	ASSERT(read_live_keypair(priv_path, pub_path, priv_after, pub_after) == 0);
	ASSERT(memcmp(priv_before, priv_after, sizeof(priv_before)) == 0);
	ASSERT(memcmp(pub_before, pub_after, sizeof(pub_before)) == 0);

	manifest_keys_reset_for_test();
	manifest_keys_test_set_fail_pub_write(1);
	crypto_test_set_randombytes_seed(MANIFEST_KEYS_ROTATE_SEED);
	ASSERT(manifest_keys_rotate(err, sizeof(err)) != 0);
	crypto_test_clear_randombytes_seed();
	ASSERT(read_live_keypair(priv_path, pub_path, priv_after, pub_after) == 0);
	ASSERT(memcmp(priv_before, priv_after, sizeof(priv_before)) == 0);
	ASSERT(memcmp(pub_before, pub_after, sizeof(pub_before)) == 0);

	manifest_keys_reset_for_test();
	manifest_keys_set_dir_for_test(NULL);
	snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", dir);
	(void)system(cmd);
	return 0;
}

static int test_manifest_keys_rotate_atomic(void)
{
	char dir[128];
	char priv_path[512];
	char pub_path[512];
	char bak_pub_path[576];
	uint8_t pub_before[CRYPTO_ED25519_PUBLIC_KEY_SIZE];
	uint8_t pub_after[CRYPTO_ED25519_PUBLIC_KEY_SIZE];
	uint8_t pub_bak[CRYPTO_ED25519_PUBLIC_KEY_SIZE];
	char err[256];
	FILE *f;
	char cmd[512];

	ASSERT(test_runner_mkdtemp_path("shellclaw_manifest_rotate", dir, sizeof(dir)) == 0);
	snprintf(priv_path, sizeof(priv_path), "%s/ed25519.priv", dir);
	snprintf(pub_path, sizeof(pub_path), "%s/ed25519.pub", dir);
	manifest_keys_set_dir_for_test(dir);
	crypto_test_set_randombytes_seed(MANIFEST_KEYS_TEST_SEED);
	ASSERT(manifest_keys_load() == 0);
	f = fopen(pub_path, "rb");
	ASSERT(f != NULL);
	ASSERT(fread(pub_before, 1, sizeof(pub_before), f) == sizeof(pub_before));
	fclose(f);
	crypto_test_set_randombytes_seed(MANIFEST_KEYS_ROTATE_SEED);
	manifest_keys_reset_for_test();
	ASSERT(manifest_keys_rotate(err, sizeof(err)) == 0);
	f = fopen(pub_path, "rb");
	ASSERT(f != NULL);
	ASSERT(fread(pub_after, 1, sizeof(pub_after), f) == sizeof(pub_after));
	fclose(f);
	ASSERT(memcmp(pub_before, pub_after, sizeof(pub_before)) != 0);
	ASSERT(find_one_backup(dir, "ed25519.pub", bak_pub_path, sizeof(bak_pub_path)) == 0);
	f = fopen(bak_pub_path, "rb");
	ASSERT(f != NULL);
	ASSERT(fread(pub_bak, 1, sizeof(pub_bak), f) == sizeof(pub_bak));
	fclose(f);
	ASSERT(memcmp(pub_bak, pub_before, sizeof(pub_before)) == 0);
	ASSERT(file_mode_is_0600(priv_path));
	ASSERT(file_mode_is_0600(pub_path));
	crypto_test_clear_randombytes_seed();
	manifest_keys_reset_for_test();
	manifest_keys_set_dir_for_test(NULL);
	snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", dir);
	(void)system(cmd);
	return 0;
}

static int test_manifest_keys_rejects_loose_priv_perms(void)
{
	char dir[128];
	char priv_path[512];
	FILE *f;
	unsigned char buf[CRYPTO_ED25519_PRIVATE_KEY_SIZE];

	ASSERT(test_runner_mkdtemp_path("shellclaw_manifest_keys3", dir, sizeof(dir)) == 0);
	snprintf(priv_path, sizeof(priv_path), "%s/ed25519.priv", dir);
	memset(buf, 0xab, sizeof(buf));
	f = fopen(priv_path, "wb");
	ASSERT(f != NULL);
	ASSERT(fwrite(buf, 1, sizeof(buf), f) == sizeof(buf));
	fclose(f);
	ASSERT(chmod(priv_path, 0644) == 0);
	manifest_keys_set_dir_for_test(dir);
	ASSERT(manifest_keys_load() != 0);
	manifest_keys_reset_for_test();
	manifest_keys_set_dir_for_test(NULL);
	{
		char cmd[512];
		snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", dir);
		(void)system(cmd);
	}
	return 0;
}

int main(int argc, char **argv)
{
	int failed = 0;
	(void)argc;
	(void)argv;
	if (test_health_json() != 0) { fprintf(stderr, "test_health_json failed\n"); failed++; }
	if (test_manifest_json_null_config_stub() != 0) {
		fprintf(stderr, "test_manifest_json_null_config_stub failed\n");
		failed++;
	}
	if (test_manifest_jetson_board() != 0) {
		fprintf(stderr, "test_manifest_jetson_board failed\n");
		failed++;
	}
	if (test_manifest_rpi_board() != 0) {
		fprintf(stderr, "test_manifest_rpi_board failed\n");
		failed++;
	}
	if (test_manifest_json_with_config() != 0) {
		fprintf(stderr, "test_manifest_json_with_config failed\n");
		failed++;
	}
	if (test_manifest_skill_objects() != 0) {
		fprintf(stderr, "test_manifest_skill_objects failed\n");
		failed++;
	}
	if (test_manifest_keys_first_run_creates() != 0) {
		fprintf(stderr, "test_manifest_keys_first_run_creates failed\n");
		failed++;
	}
	if (test_manifest_keys_second_run_reuses() != 0) {
		fprintf(stderr, "test_manifest_keys_second_run_reuses failed\n");
		failed++;
	}
	if (test_manifest_keys_rejects_loose_priv_perms() != 0) {
		fprintf(stderr, "test_manifest_keys_rejects_loose_priv_perms failed\n");
		failed++;
	}
	if (test_manifest_keys_rotate_atomic() != 0) {
		fprintf(stderr, "test_manifest_keys_rotate_atomic failed\n");
		failed++;
	}
	if (test_manifest_keys_rotate_fails_preserves_keys() != 0) {
		fprintf(stderr, "test_manifest_keys_rotate_fails_preserves_keys failed\n");
		failed++;
	}
	if (test_jcs_canonicalize_sorted_keys() != 0) {
		fprintf(stderr, "test_jcs_canonicalize_sorted_keys failed\n");
		failed++;
	}
	if (test_jcs_rejects_invalid_inputs() != 0) {
		fprintf(stderr, "test_jcs_rejects_invalid_inputs failed\n");
		failed++;
	}
	if (test_jcs_escapes_primitives_and_arrays() != 0) {
		fprintf(stderr, "test_jcs_escapes_primitives_and_arrays failed\n");
		failed++;
	}
	if (test_manifest_hardware_io_from_config() != 0) {
		fprintf(stderr, "test_manifest_hardware_io_from_config failed\n");
		failed++;
	}
	if (test_manifest_build_signed_without_keys() != 0) {
		fprintf(stderr, "test_manifest_build_signed_without_keys failed\n");
		failed++;
	}
	if (test_signed_manifest_structure_and_verify() != 0) {
		fprintf(stderr, "test_signed_manifest_structure_and_verify failed\n");
		failed++;
	}
	if (failed == 0)
		printf("test_manifest: all tests passed\n");
	return failed;
}
