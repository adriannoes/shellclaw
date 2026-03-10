/**
 * @file skill.c
 * @brief Skill loader: scan skills directory for .md files and concatenate contents.
 */
#define _POSIX_C_SOURCE 200809L

#include "config.h"
#include "skill.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define SKILL_SEP "\n\n---\n\n"
#define PROMPT_SEP "\n\n"
#define READ_CHUNK 4096
#define MAX_PATH_LEN 1024

static int ends_with_md(const char *name)
{
	size_t len = strlen(name);
	return len >= 3 && strcmp(name + len - 3, ".md") == 0;
}

static size_t append_str(char *out_buf, size_t out_size, size_t *used, const char *str)
{
	size_t len = strlen(str);
	size_t remain = out_size > *used ? out_size - *used - 1 : 0;
	if (remain == 0) return 0;
	if (len > remain) len = remain;
	memcpy(out_buf + *used, str, len);
	*used += len;
	out_buf[*used] = '\0';
	return len;
}

static size_t append_file_content(FILE *f, char *out_buf, size_t out_size, size_t *used)
{
	char buf[READ_CHUNK];
	size_t total = 0;
	while (*used < out_size - 1 && fgets(buf, (int)sizeof(buf), f) != NULL) {
		size_t n = strlen(buf);
		size_t remain = out_size - *used - 1;
		if (n > remain) n = remain;
		memcpy(out_buf + *used, buf, n);
		*used += n;
		out_buf[*used] = '\0';
		total += n;
	}
	return total;
}

static void append_file_by_path(const char *path, char *out_buf, size_t out_size, size_t *used)
{
	if (!path || path[0] == '\0') return;
	FILE *f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "warning: cannot read file for system prompt: %s\n", path);
		return;
	}
	append_file_content(f, out_buf, out_size, used);
	fclose(f);
}

int skill_load_all(const config_t *cfg, char *out_buf, size_t out_size)
{
	if (!cfg || !out_buf || out_size == 0) return -1;
	out_buf[0] = '\0';
	const char *dir_path = config_skills_dir(cfg);
	if (!dir_path || dir_path[0] == '\0') return 0;
	DIR *dir = opendir(dir_path);
	if (!dir) {
		if (errno == ENOENT || errno == ENOTDIR)
			fprintf(stderr, "warning: skills directory not found or not a directory: %s\n", dir_path);
		else
			fprintf(stderr, "warning: cannot open skills directory %s: %s\n", dir_path, strerror(errno));
		return 0;
	}
	size_t used = 0;
	int first = 1;
	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.') continue;
		if (!ends_with_md(ent->d_name)) continue;
		char path[MAX_PATH_LEN];
		int n = snprintf(path, sizeof(path), "%s/%s", dir_path, ent->d_name);
		if (n < 0 || (size_t)n >= sizeof(path)) continue;
		FILE *f = fopen(path, "r");
		if (!f) continue;
		if (!first) append_str(out_buf, out_size, &used, SKILL_SEP);
		first = 0;
		append_file_content(f, out_buf, out_size, &used);
		fclose(f);
	}
	closedir(dir);
	return 0;
}

int skill_build_system_prompt_base(const config_t *cfg, const char *skills_content,
                                  char *out_buf, size_t out_size)
{
	if (!cfg || !out_buf || out_size == 0) return -1;
	out_buf[0] = '\0';
	size_t used = 0;
	const char *soul = config_agent_soul_path(cfg);
	if (soul && soul[0] != '\0') {
		append_file_by_path(soul, out_buf, out_size, &used);
		append_str(out_buf, out_size, &used, PROMPT_SEP);
	}
	const char *identity = config_agent_identity_path(cfg);
	if (identity && identity[0] != '\0') {
		append_file_by_path(identity, out_buf, out_size, &used);
		append_str(out_buf, out_size, &used, PROMPT_SEP);
	}
	if (skills_content && skills_content[0] != '\0')
		append_str(out_buf, out_size, &used, skills_content);
	return 0;
}

static int is_safe_skill_name(const char *name)
{
	if (!name || name[0] == '\0') return 0;
	if (name[0] == '.') return 0;
	for (const char *p = name; *p; p++) {
		if (*p == '/' || *p == '\\') return 0;
		if (p[0] == '.' && p[1] == '.') return 0;
	}
	return 1;
}

static int build_skill_path(const config_t *cfg, const char *name, char *path_out, size_t path_size)
{
	if (!cfg || !name || !path_out || path_size == 0) return -1;
	if (!is_safe_skill_name(name)) return -1;
	const char *dir = config_skills_dir(cfg);
	if (!dir || dir[0] == '\0') return -1;
	int n = snprintf(path_out, path_size, "%s/%s.md", dir, name);
	return (n < 0 || (size_t)n >= path_size) ? -1 : 0;
}

int skill_list_names(const config_t *cfg, char **names_out, int max_count)
{
	if (!cfg || !names_out || max_count <= 0) return -1;
	const char *dir_path = config_skills_dir(cfg);
	if (!dir_path || dir_path[0] == '\0') return 0;
	DIR *dir = opendir(dir_path);
	if (!dir) return 0;
	int count = 0;
	struct dirent *ent;
	while (count < max_count && (ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.') continue;
		if (!ends_with_md(ent->d_name)) continue;
		size_t len = strlen(ent->d_name);
		if (len <= 3) continue;
		names_out[count] = strndup(ent->d_name, len - 3);
		if (!names_out[count]) {
			for (int i = 0; i < count; i++) free(names_out[i]);
			closedir(dir);
			return -1;
		}
		count++;
	}
	closedir(dir);
	return count;
}

int skill_get_content(const config_t *cfg, const char *name, char *out_buf, size_t out_size)
{
	if (!cfg || !name || !out_buf || out_size == 0) return -1;
	char path[MAX_PATH_LEN];
	if (build_skill_path(cfg, name, path, sizeof(path)) != 0) return -1;
	FILE *f = fopen(path, "r");
	if (!f) return -1;
	out_buf[0] = '\0';
	size_t used = 0;
	append_file_content(f, out_buf, out_size, &used);
	fclose(f);
	return 0;
}

int skill_create(const config_t *cfg, const char *name, const char *content)
{
	if (!cfg || !name || !content) return -1;
	char path[MAX_PATH_LEN];
	if (build_skill_path(cfg, name, path, sizeof(path)) != 0) return -1;
	FILE *f = fopen(path, "r");
	if (f) {
		fclose(f);
		return -1;
	}
	f = fopen(path, "w");
	if (!f) return -1;
	size_t len = strlen(content);
	size_t written = fwrite(content, 1, len, f);
	fclose(f);
	return (written == len) ? 0 : -1;
}

int skill_update(const config_t *cfg, const char *name, const char *content)
{
	if (!cfg || !name || !content) return -1;
	char path[MAX_PATH_LEN];
	if (build_skill_path(cfg, name, path, sizeof(path)) != 0) return -1;
	FILE *f = fopen(path, "w");
	if (!f) return -1;
	size_t len = strlen(content);
	size_t written = fwrite(content, 1, len, f);
	fclose(f);
	return (written == len) ? 0 : -1;
}

int skill_delete(const config_t *cfg, const char *name)
{
	if (!cfg || !name) return -1;
	char path[MAX_PATH_LEN];
	if (build_skill_path(cfg, name, path, sizeof(path)) != 0) return -1;
	return remove(path) == 0 ? 0 : -1;
}
