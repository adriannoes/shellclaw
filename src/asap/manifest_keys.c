/**
 * @file manifest_keys.c
 * @brief Ed25519 key load, persist, and rotation for ASAP signed manifests.
 */
#define _POSIX_C_SOURCE 200809L

#include "asap/manifest_keys.h"
#include "core/config.h"
#include "crypto/crypto.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MANIFEST_KEYS_SUBDIR "keys"
#define MANIFEST_PRIV_FILENAME "ed25519.priv"
#define MANIFEST_PUB_FILENAME "ed25519.pub"
#define MANIFEST_KEY_FILE_MODE 0600

static uint8_t g_manifest_pubkey[CRYPTO_ED25519_PUBLIC_KEY_SIZE];
static uint8_t g_manifest_privkey[CRYPTO_ED25519_PRIVATE_KEY_SIZE];
static int g_manifest_keys_loaded;
static const char *g_manifest_keys_dir_test;
static int g_manifest_keys_test_fail_pub_write;
static int g_manifest_keys_test_fail_backup_write;

void manifest_keys_test_set_fail_pub_write(int enabled)
{
	g_manifest_keys_test_fail_pub_write = enabled ? 1 : 0;
}

void manifest_keys_test_set_fail_backup_write(int enabled)
{
	g_manifest_keys_test_fail_backup_write = enabled ? 1 : 0;
}

void manifest_keys_set_dir_for_test(const char *keys_dir)
{
	g_manifest_keys_dir_test = keys_dir;
}

static void manifest_keys_clear_loaded(void)
{
	g_manifest_keys_loaded = 0;
	memset(g_manifest_pubkey, 0, sizeof(g_manifest_pubkey));
	memset(g_manifest_privkey, 0, sizeof(g_manifest_privkey));
}

void manifest_keys_reset(void)
{
	manifest_keys_clear_loaded();
	g_manifest_keys_test_fail_pub_write = 0;
	g_manifest_keys_test_fail_backup_write = 0;
}

const uint8_t *manifest_keys_public(void)
{
	return g_manifest_keys_loaded ? g_manifest_pubkey : NULL;
}

const uint8_t *manifest_keys_private(void)
{
	return g_manifest_keys_loaded ? g_manifest_privkey : NULL;
}

static char *manifest_resolve_home_dir(void)
{
	const char *env = getenv("SHELLCLAW_HOME");

	if (env && env[0] != '\0')
		return strdup(env);
	return config_expand_tilde("~/.shellclaw");
}

static int manifest_keys_build_paths(char *priv_path, size_t priv_sz,
	char *pub_path, size_t pub_sz)
{
	const char *keys_dir;
	char *home = NULL;
	char keys_buf[512];

	if (g_manifest_keys_dir_test && g_manifest_keys_dir_test[0] != '\0') {
		keys_dir = g_manifest_keys_dir_test;
	} else {
		home = manifest_resolve_home_dir();
		if (!home)
			return -1;
		if (snprintf(keys_buf, sizeof(keys_buf), "%s/%s", home,
				MANIFEST_KEYS_SUBDIR) >= (int)sizeof(keys_buf)) {
			free(home);
			return -1;
		}
		keys_dir = keys_buf;
	}
	if (snprintf(priv_path, priv_sz, "%s/%s", keys_dir, MANIFEST_PRIV_FILENAME) >= (int)priv_sz ||
	    snprintf(pub_path, pub_sz, "%s/%s", keys_dir, MANIFEST_PUB_FILENAME) >= (int)pub_sz) {
		free(home);
		return -1;
	}
	free(home);
	return 0;
}

static int manifest_priv_permissions_ok(const char *priv_path)
{
	struct stat st;

	if (stat(priv_path, &st) != 0)
		return -1;
	if (!S_ISREG(st.st_mode))
		return -1;
	if ((st.st_mode & 0077U) != 0U)
		return -1;
	return 0;
}

static int manifest_build_tmp_path(const char *final_path, char *tmp_path, size_t tmp_sz)
{
	int n;
	if (!final_path || !tmp_path || tmp_sz == 0U)
		return -1;
	n = snprintf(tmp_path, tmp_sz, "%s.tmp", final_path);
	return (n < 0 || (size_t)n >= tmp_sz) ? -1 : 0;
}

static int manifest_write_key_file(const char *path, const uint8_t *data, size_t len)
{
	int fd;
	size_t off = 0;

	if (g_manifest_keys_test_fail_backup_write && path != NULL &&
	    strstr(path, ".bak.") != NULL) {
		g_manifest_keys_test_fail_backup_write = 0;
		return -1;
	}
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, MANIFEST_KEY_FILE_MODE);
	if (fd < 0)
		return -1;
	while (off < len) {
		ssize_t n = write(fd, data + off, len - off);
		if (n <= 0) {
			close(fd);
			unlink(path);
			return -1;
		}
		off += (size_t)n;
	}
	if (fchmod(fd, MANIFEST_KEY_FILE_MODE) != 0) {
		close(fd);
		unlink(path);
		return -1;
	}
	if (fsync(fd) != 0) {
		close(fd);
		unlink(path);
		return -1;
	}
	close(fd);
	return 0;
}

static int manifest_write_key_file_atomic(const char *final_path, const uint8_t *data, size_t len)
{
	char tmp_path[576];

	if (g_manifest_keys_test_fail_pub_write) {
		size_t flen = final_path ? strlen(final_path) : 0U;
		if (flen >= strlen(MANIFEST_PUB_FILENAME) &&
		    strcmp(final_path + flen - strlen(MANIFEST_PUB_FILENAME),
			MANIFEST_PUB_FILENAME) == 0) {
			g_manifest_keys_test_fail_pub_write = 0;
			return -1;
		}
	}
	if (manifest_build_tmp_path(final_path, tmp_path, sizeof(tmp_path)) != 0)
		return -1;
	if (manifest_write_key_file(tmp_path, data, len) != 0) {
		unlink(tmp_path);
		return -1;
	}
	if (rename(tmp_path, final_path) != 0) {
		unlink(tmp_path);
		return -1;
	}
	return 0;
}

static int manifest_read_key_file(const char *path, uint8_t *buf, size_t len)
{
	int fd;
	size_t off = 0;
	struct stat st;

	if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
		return -1;
	if ((size_t)st.st_size != len)
		return -1;
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	while (off < len) {
		ssize_t n = read(fd, buf + off, len - off);
		if (n <= 0) {
			close(fd);
			return -1;
		}
		off += (size_t)n;
	}
	close(fd);
	return 0;
}

static int manifest_keys_ensure_dir(const char *priv_path)
{
	char dir[512];
	const char *slash;
	size_t len;

	slash = strrchr(priv_path, '/');
	if (!slash || slash == priv_path)
		return -1;
	len = (size_t)(slash - priv_path);
	if (len >= sizeof(dir))
		return -1;
	memcpy(dir, priv_path, len);
	dir[len] = '\0';
	if (mkdir(dir, 0700) != 0 && errno != EEXIST)
		return -1;
	return 0;
}

static int manifest_keys_create_and_persist(const char *priv_path, const char *pub_path)
{
	if (manifest_keys_ensure_dir(priv_path) != 0)
		return -1;
	if (crypto_ed25519_keypair(g_manifest_pubkey, g_manifest_privkey) != 0)
		return -1;
	if (manifest_write_key_file_atomic(priv_path, g_manifest_privkey,
			CRYPTO_ED25519_PRIVATE_KEY_SIZE) != 0)
		return -1;
	if (manifest_write_key_file_atomic(pub_path, g_manifest_pubkey,
			CRYPTO_ED25519_PUBLIC_KEY_SIZE) != 0) {
		unlink(priv_path);
		return -1;
	}
	return 0;
}

static void manifest_keys_set_error(char *err, size_t err_len, const char *msg)
{
	if (!err || err_len == 0U)
		return;
	snprintf(err, err_len, "%s", msg);
}

static int manifest_keys_load_from_disk(const char *priv_path, const char *pub_path,
	char *err, size_t err_len)
{
	if (access(priv_path, F_OK) == 0 && manifest_priv_permissions_ok(priv_path) != 0) {
		manifest_keys_set_error(err, err_len,
			"ed25519.priv permissions too open (expected 0600)");
		return -1;
	}
	if (manifest_read_key_file(priv_path, g_manifest_privkey,
			CRYPTO_ED25519_PRIVATE_KEY_SIZE) != 0) {
		manifest_keys_set_error(err, err_len, "failed to read ed25519.priv");
		return -1;
	}
	if (manifest_read_key_file(pub_path, g_manifest_pubkey,
			CRYPTO_ED25519_PUBLIC_KEY_SIZE) != 0) {
		manifest_keys_set_error(err, err_len, "failed to read ed25519.pub");
		return -1;
	}
	return 0;
}

int manifest_keys_ensure_loaded(char *err, size_t err_len)
{
	if (g_manifest_keys_loaded) {
		char priv_path[512];
		char pub_path[512];

		if (manifest_keys_build_paths(priv_path, sizeof(priv_path),
				pub_path, sizeof(pub_path)) != 0) {
			manifest_keys_set_error(err, err_len, "failed to resolve keys directory");
			return -1;
		}
		if (access(priv_path, F_OK) == 0 && manifest_priv_permissions_ok(priv_path) != 0) {
			manifest_keys_clear_loaded();
			manifest_keys_set_error(err, err_len,
				"ed25519.priv permissions too open (expected 0600)");
			return -1;
		}
		return 0;
	}
	return manifest_keys_load(err, err_len);
}

int manifest_keys_load(char *err, size_t err_len)
{
	char priv_path[512];
	char pub_path[512];

	if (g_manifest_keys_loaded)
		return 0;
	if (manifest_keys_build_paths(priv_path, sizeof(priv_path),
			pub_path, sizeof(pub_path)) != 0) {
		manifest_keys_set_error(err, err_len, "failed to resolve keys directory");
		return -1;
	}
	if (access(priv_path, F_OK) == 0) {
		if (manifest_keys_load_from_disk(priv_path, pub_path, err, err_len) != 0)
			return -1;
	} else {
		if (manifest_keys_create_and_persist(priv_path, pub_path) != 0) {
			manifest_keys_set_error(err, err_len, "failed to create Ed25519 keypair");
			return -1;
		}
	}
	g_manifest_keys_loaded = 1;
	return 0;
}

int manifest_keys_rotate(char *err, size_t err_len)
{
	char priv_path[512];
	char pub_path[512];
	uint8_t old_priv[CRYPTO_ED25519_PRIVATE_KEY_SIZE];
	uint8_t old_pub[CRYPTO_ED25519_PUBLIC_KEY_SIZE];
	int had_old_keys;
	time_t now;
	long long ts;

	manifest_keys_clear_loaded();
	if (manifest_keys_build_paths(priv_path, sizeof(priv_path),
			pub_path, sizeof(pub_path)) != 0) {
		manifest_keys_set_error(err, err_len, "failed to resolve keys directory");
		return -1;
	}
	if (access(priv_path, F_OK) == 0 && manifest_priv_permissions_ok(priv_path) != 0) {
		manifest_keys_set_error(err, err_len,
			"ed25519.priv permissions too open (expected 0600)");
		return -1;
	}
	had_old_keys = 0;
	if (access(priv_path, F_OK) == 0 && access(pub_path, F_OK) == 0) {
		if (manifest_read_key_file(priv_path, old_priv, sizeof(old_priv)) == 0 &&
		    manifest_read_key_file(pub_path, old_pub, sizeof(old_pub)) == 0)
			had_old_keys = 1;
	}
	now = time(NULL);
	if (now == (time_t)-1) {
		manifest_keys_set_error(err, err_len, "failed to read current time");
		return -1;
	}
	ts = (long long)now;
	if (manifest_keys_ensure_dir(priv_path) != 0) {
		manifest_keys_set_error(err, err_len, "failed to create keys directory");
		return -1;
	}
	if (crypto_ed25519_keypair(g_manifest_pubkey, g_manifest_privkey) != 0) {
		manifest_keys_set_error(err, err_len, "failed to generate Ed25519 keypair");
		return -1;
	}
	if (had_old_keys) {
		char bak_priv[576];
		char bak_pub[576];
		if (snprintf(bak_priv, sizeof(bak_priv), "%s.bak.%lld", priv_path, ts) >=
		    (int)sizeof(bak_priv) ||
		    snprintf(bak_pub, sizeof(bak_pub), "%s.bak.%lld", pub_path, ts) >=
		    (int)sizeof(bak_pub)) {
			manifest_keys_set_error(err, err_len, "backup path too long");
			return -1;
		}
		if (manifest_write_key_file(bak_priv, old_priv, sizeof(old_priv)) != 0) {
			manifest_keys_set_error(err, err_len, "failed to backup ed25519.priv");
			return -1;
		}
		if (manifest_write_key_file(bak_pub, old_pub, sizeof(old_pub)) != 0) {
			manifest_keys_set_error(err, err_len, "failed to backup ed25519.pub");
			return -1;
		}
	}
	if (manifest_write_key_file_atomic(priv_path, g_manifest_privkey,
			CRYPTO_ED25519_PRIVATE_KEY_SIZE) != 0) {
		manifest_keys_set_error(err, err_len, "failed to write ed25519.priv");
		return -1;
	}
	if (manifest_write_key_file_atomic(pub_path, g_manifest_pubkey,
			CRYPTO_ED25519_PUBLIC_KEY_SIZE) != 0) {
		manifest_keys_set_error(err, err_len, "failed to write ed25519.pub");
		if (had_old_keys) {
			if (manifest_write_key_file_atomic(priv_path, old_priv,
					sizeof(old_priv)) != 0)
				manifest_keys_set_error(err, err_len,
					"failed to restore ed25519.priv after pub write error");
		} else {
			unlink(priv_path);
		}
		return -1;
	}
	g_manifest_keys_loaded = 1;
	return 0;
}
