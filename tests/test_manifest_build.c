/**
 * @file test_manifest_build.c
 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "manifest_test_common.h"
#include "test_runner.h"
#include "core/config.h"
#include "core/version.h"
#include "crypto/crypto.h"
#include "crypto/jcs.h"
#include "hardware/board_detect.h"
#include "asap/manifest_keys.h"
#include "cJSON.h"
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

	manifest_keys_reset();
	manifest_keys_set_dir_for_test(NULL);
	signed_json = manifest_build_signed_json(NULL);
	ASSERT(signed_json == NULL);
	return 0;
}

static int test_manifest_board_from_config(void)
{
	FILE *f = fopen(TMP_CONFIG, "w");
	config_t *cfg = NULL;
	char errbuf[256];
	char *json;
	cJSON *parsed;

	ASSERT(f);
	fprintf(f, "[agent]\nmodel = \"test\"\n");
	fprintf(f, "[hardware]\nboard = \"jetson\"\n");
	fclose(f);
	ASSERT(config_load(TMP_CONFIG, &cfg, errbuf, sizeof(errbuf)) == 0);
	json = manifest_build_json(cfg);
	ASSERT(json != NULL);
	ASSERT(strstr(json, "local_cuda") != NULL);
	ASSERT(strstr(json, "jetson_orin_nano_super_8gb") != NULL);
	parsed = cJSON_Parse(json);
	ASSERT(parsed != NULL);
	ASSERT(assert_manifest_shape(parsed, "urn:asap:agent:shellclaw", SHELLCLAW_RELEASE_VERSION,
		"edge_accelerator", "jetson_orin_nano_super_8gb", "cloud",
		"Phi-3-mini-4k-instruct-Q4_K_M") == 0);
	cJSON_Delete(parsed);
	free(json);
	config_free(cfg);
	unlink(TMP_CONFIG);
	return 0;
}

static int test_manifest_builtin_and_unknown_skill_descriptions(void)
{
	const char *dir = "/tmp/shellclaw_test_manifest_skills_builtin";
	char cmd[256];
	FILE *sf;
	config_t *cfg = NULL;
	char errbuf[256];
	char *json;
	cJSON *parsed;
	cJSON *skills;
	int i;

	snprintf(cmd, sizeof(cmd), "rm -rf \"%s\" && mkdir -p \"%s\"", dir, dir);
	ASSERT(system(cmd) == 0);
	sf = fopen("/tmp/shellclaw_test_manifest_skills_builtin/assistant.md", "w");
	ASSERT(sf);
	fprintf(sf, "\n");
	fclose(sf);
	sf = fopen("/tmp/shellclaw_test_manifest_skills_builtin/gpio_control.md", "w");
	ASSERT(sf);
	fprintf(sf, "\n");
	fclose(sf);
	sf = fopen("/tmp/shellclaw_test_manifest_skills_builtin/edge_only_skill.md", "w");
	ASSERT(sf);
	fprintf(sf, "\n");
	fclose(sf);
	sf = fopen(TMP_CONFIG, "w");
	ASSERT(sf);
	fprintf(sf, "[agent]\nmodel = \"test\"\n[skills]\ndir = \"%s\"\n", dir);
	fclose(sf);
	ASSERT(config_load(TMP_CONFIG, &cfg, errbuf, sizeof(errbuf)) == 0);
	json = manifest_build_json(cfg);
	ASSERT(json != NULL);
	parsed = cJSON_Parse(json);
	ASSERT(parsed != NULL);
	skills = cJSON_GetObjectItem(cJSON_GetObjectItem(parsed, "capabilities"), "skills");
	ASSERT(skills != NULL);
	for (i = 0; i < cJSON_GetArraySize(skills); i++) {
		cJSON *s = cJSON_GetArrayItem(skills, i);
		const char *sid = cJSON_GetObjectItem(s, "id")->valuestring;
		const char *desc = cJSON_GetObjectItem(s, "description")->valuestring;
		if (strcmp(sid, "assistant") == 0)
			ASSERT(strcmp(desc, "General assistant on edge hardware") == 0);
		if (strcmp(sid, "gpio_control") == 0)
			ASSERT(strcmp(desc, "GPIO pin control") == 0);
		if (strcmp(sid, "edge_only_skill") == 0)
			ASSERT(strcmp(desc, "edge_only_skill") == 0);
	}
	cJSON_Delete(parsed);
	free(json);
	config_free(cfg);
	unlink(TMP_CONFIG);
	snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", dir);
	(void)system(cmd);
	return 0;
}

static int test_manifest_hardware_skips_empty_io_entries(void)
{
	FILE *f = fopen(TMP_CONFIG, "w");
	config_t *cfg = NULL;
	char errbuf[256];
	char *json;
	cJSON *parsed;
	cJSON *io;

	ASSERT(f);
	fprintf(f, "[agent]\nmodel = \"test\"\n");
	fprintf(f, "[hardware]\nio = [\"\", \"spi\"]\n");
	fclose(f);
	ASSERT(config_load(TMP_CONFIG, &cfg, errbuf, sizeof(errbuf)) == 0);
	json = manifest_build_json(cfg);
	ASSERT(json != NULL);
	parsed = cJSON_Parse(json);
	ASSERT(parsed != NULL);
	io = cJSON_GetObjectItem(
	    cJSON_GetObjectItem(cJSON_GetObjectItem(parsed, "capabilities"), "hardware"),
	    "io");
	ASSERT(io != NULL && cJSON_GetArraySize(io) == 1);
	ASSERT(strcmp(cJSON_GetArrayItem(io, 0)->valuestring, "spi") == 0);
	cJSON_Delete(parsed);
	free(json);
	config_free(cfg);
	unlink(TMP_CONFIG);
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
	manifest_keys_reset();
	crypto_test_set_randombytes_seed(MANIFEST_KEYS_TEST_SEED);
	ASSERT(manifest_keys_ensure_loaded(NULL, 0) == 0);
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
	manifest_keys_reset();
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
	if (test_health_json() != 0) {
		fprintf(stderr, "test_health_json failed\n");
		failed++;
	}
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
	if (test_manifest_board_from_config() != 0) {
		fprintf(stderr, "test_manifest_board_from_config failed\n");
		failed++;
	}
	if (test_manifest_builtin_and_unknown_skill_descriptions() != 0) {
		fprintf(stderr, "test_manifest_builtin_and_unknown_skill_descriptions failed\n");
		failed++;
	}
	if (test_manifest_hardware_skips_empty_io_entries() != 0) {
		fprintf(stderr, "test_manifest_hardware_skips_empty_io_entries failed\n");
		failed++;
	}
	if (failed == 0)
		printf("test_manifest_build: all tests passed\n");
	return failed;
}
