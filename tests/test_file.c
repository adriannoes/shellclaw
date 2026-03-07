/**
 * @file test_file.c
 * @brief Unit tests for file tool: read, write, list_dir, workspace boundary.
 */

#include "tools/tool.h"
#include "tools/file.h"
#include "core/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

static int tests_run = 0;
static int tests_failed = 0;

#define MU_ASSERT(cond, msg) do { \
	tests_run++; \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", (msg)); \
		tests_failed++; \
		return; \
	} \
} while (0)

#define MU_RUN(test) do { test(); } while (0)

static void test_file_read_write_list(void)
{
	char cwd[PATH_MAX];
	MU_ASSERT(getcwd(cwd, sizeof(cwd)) != NULL, "getcwd");
	char dir[PATH_MAX];
	snprintf(dir, sizeof(dir), "%s/build/test_file_dir", cwd);
	char config_path[PATH_MAX];
	snprintf(config_path, sizeof(config_path), "%s/build/test_file_config.toml", cwd);
	/* Idempotent cleanup: remove leftovers from previous failed run */
	unlink(config_path);
	rmdir(dir);
	mkdir(dir, 0755);
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/test.txt", dir);
	config_t *cfg = NULL;
	char errbuf[256];
	char config_content[512];
	snprintf(config_content, sizeof(config_content),
		"[agent]\nmodel=\"x\"\n[memory]\ndb_path=\"/tmp/db\"\n[sandbox]\nworkspace_only=true\nworkspace_path=\"%s\"\n",
		cwd);
	FILE *f = fopen(config_path, "w");
	MU_ASSERT(f != NULL, "create config file");
	fprintf(f, "%s", config_content);
	fclose(f);
	int load_ret = config_load(config_path, &cfg, errbuf, sizeof(errbuf));
	MU_ASSERT(load_ret == 0 && cfg != NULL, "load config");
	tool_file_set_config(cfg);
	const tool_t *t = tool_file_get();
	char buf[4096];
	char write_path[PATH_MAX];
	snprintf(write_path, sizeof(write_path), "%s/test.txt", dir);
	char args[1024];
	snprintf(args, sizeof(args), "{\"operation\":\"write_file\",\"path\":\"%s\",\"content\":\"hello\"}", write_path);
	int r = t->execute(args, buf, sizeof(buf));
	MU_ASSERT(r == 0, "write_file succeeds");
	snprintf(args, sizeof(args), "{\"operation\":\"read_file\",\"path\":\"%s\"}", write_path);
	r = t->execute(args, buf, sizeof(buf));
	MU_ASSERT(r == 0, "read_file succeeds");
	MU_ASSERT(strcmp(buf, "hello") == 0, "content matches");
	snprintf(args, sizeof(args), "{\"operation\":\"list_dir\",\"path\":\"%s\"}", dir);
	r = t->execute(args, buf, sizeof(buf));
	MU_ASSERT(r == 0, "list_dir succeeds");
	MU_ASSERT(strstr(buf, "test.txt") != NULL, "test.txt in listing");
	config_free(cfg);
	unlink(path);
	rmdir(dir);
	unlink(config_path);
}

static void test_file_empty_workspace_denies_all(void)
{
	config_t *cfg = NULL;
	char errbuf[256];
	char config_path[PATH_MAX];
	char cwd[PATH_MAX];
	MU_ASSERT(getcwd(cwd, sizeof(cwd)) != NULL, "getcwd");
	snprintf(config_path, sizeof(config_path), "%s/build/test_file_empty_ws.toml", cwd);
	FILE *f = fopen(config_path, "w");
	MU_ASSERT(f != NULL, "create config");
	fprintf(f, "[agent]\nmodel=\"x\"\n[memory]\ndb_path=\"/tmp/db\"\n[sandbox]\nworkspace_only=true\nworkspace_path=\"\"\n");
	fclose(f);
	config_load(config_path, &cfg, errbuf, sizeof(errbuf));
	MU_ASSERT(cfg != NULL, "load config");
	tool_file_set_config(cfg);
	const tool_t *t = tool_file_get();
	char buf[256];
	int r = t->execute("{\"operation\":\"read_file\",\"path\":\"/etc/passwd\"}", buf, sizeof(buf));
	MU_ASSERT(r == -1 || strstr(buf, "outside") != NULL, "empty workspace denies all paths");
	config_free(cfg);
	unlink(config_path);
}

static void test_file_outside_workspace_rejected(void)
{
	char cwd[PATH_MAX];
	MU_ASSERT(getcwd(cwd, sizeof(cwd)) != NULL, "getcwd");
	char config_path[PATH_MAX];
	snprintf(config_path, sizeof(config_path), "%s/build/test_file_ws_config.toml", cwd);
	config_t *cfg = NULL;
	char errbuf[256];
	FILE *f = fopen(config_path, "w");
	MU_ASSERT(f != NULL, "create config");
	fprintf(f, "[agent]\nmodel=\"x\"\n[memory]\ndb_path=\"/tmp/db\"\n[sandbox]\nworkspace_only=true\nworkspace_path=\"%s\"\n", cwd);
	fclose(f);
	config_load(config_path, &cfg, errbuf, sizeof(errbuf));
	MU_ASSERT(cfg != NULL, "load config");
	tool_file_set_config(cfg);
	const tool_t *t = tool_file_get();
	char buf[256];
	int r = t->execute("{\"operation\":\"read_file\",\"path\":\"/etc/passwd\"}", buf, sizeof(buf));
	MU_ASSERT(r == -1 || strstr(buf, "outside") != NULL, "outside workspace rejected");
	config_free(cfg);
	unlink(config_path);
}

int main(void)
{
	MU_RUN(test_file_read_write_list);
	MU_RUN(test_file_empty_workspace_denies_all);
	MU_RUN(test_file_outside_workspace_rejected);
	printf("%d tests run, %d failed\n", tests_run, tests_failed);
	return tests_failed ? 1 : 0;
}
