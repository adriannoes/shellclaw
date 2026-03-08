/**
 * @file shell.c
 * @brief Shell tool: execute commands via popen with blocklist and timeout.
 */
#define _POSIX_C_SOURCE 200809L

#include "tools/tool.h"
#include "tools/shell.h"
#include "core/config.h"
#include "cJSON.h"
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define DEFAULT_TIMEOUT_SEC 60

static const char SHELL_PARAMS[] =
	"{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\",\"description\":\"Shell command to execute\"}},\"required\":[\"command\"]}";

/**
 * Best-effort blocklist for obviously dangerous commands.
 * WARNING: This is NOT a security sandbox. Determined users can bypass it
 * with aliases, variable expansion, encoding tricks, or absolute paths.
 * For real isolation, use chroot, seccomp, namespaces, or containers.
 */
static const char *const BLOCKLIST[] = {
	"rm -rf /", "rm -rf / ", "rm -rf /$", "rm -rf /*",
	"mkfs", "dd if=", "dd of=", "shutdown", "reboot",
	":(){ :|:& };:", "fork()", "> /dev/sd",
	NULL
};

static int is_blocked(const char *cmd)
{
	if (!cmd) return 1;
	for (const char *const *p = BLOCKLIST; *p; p++) {
		if (strstr(cmd, *p) != NULL)
			return 1;
	}
	return 0;
}

static const config_t *g_shell_cfg;

static int shell_execute(const char *args_json, char *result_buf, size_t max_len)
{
	if (!args_json || !result_buf || max_len == 0) return -1;
	cJSON *root = cJSON_Parse(args_json);
	if (!root || !cJSON_IsObject(root)) {
		if (root) cJSON_Delete(root);
		snprintf(result_buf, max_len, "{\"error\":\"invalid JSON\"}");
		return -1;
	}
	cJSON *cmd = cJSON_GetObjectItem(root, "command");
	if (!cmd || !cJSON_IsString(cmd)) {
		cJSON_Delete(root);
		snprintf(result_buf, max_len, "{\"error\":\"missing or invalid 'command'\"}");
		return -1;
	}
	char *command = strdup(cmd->valuestring ? cmd->valuestring : "");
	cJSON_Delete(root);
	if (!command) {
		snprintf(result_buf, max_len, "{\"error\":\"out of memory\"}");
		return -1;
	}
	if (is_blocked(command)) {
		free(command);
		snprintf(result_buf, max_len, "{\"error\":\"command blocked for safety\"}");
		return -1;
	}
	int timeout_sec = g_shell_cfg ? config_shell_timeout_sec(g_shell_cfg) : DEFAULT_TIMEOUT_SEC;
	int pipefd[2];
	if (pipe(pipefd) != 0) {
		free(command);
		snprintf(result_buf, max_len, "{\"error\":\"pipe failed\"}");
		return -1;
	}
	pid_t pid = fork();
	if (pid < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		free(command);
		snprintf(result_buf, max_len, "{\"error\":\"fork failed\"}");
		return -1;
	}
	if (pid == 0) {
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		dup2(pipefd[1], STDERR_FILENO);
		close(pipefd[1]);
		execl("/bin/sh", "sh", "-c", command, (char *)NULL);
		_exit(127);
	}
	free(command);
	close(pipefd[1]);
	size_t total = 0;
	result_buf[0] = '\0';
	char buf[256];
	int timed_out = 0;
	int elapsed_ms = 0;
	while (total < max_len - 1 && elapsed_ms < timeout_sec * 1000) {
		struct pollfd pfd = { .fd = pipefd[0], .events = POLLIN };
		int rem = timeout_sec * 1000 - elapsed_ms;
		if (rem > 5000) rem = 5000;
		int r = poll(&pfd, 1, rem);
		if (r < 0) break;
		if (r == 0) {
			elapsed_ms += rem;
			if (elapsed_ms >= timeout_sec * 1000) {
				timed_out = 1;
				kill(pid, SIGKILL);
				break;
			}
			continue;
		}
		ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
		if (n <= 0) break;
		buf[n] = '\0';
		size_t add = (size_t)n;
		if (total + add >= max_len - 1) add = max_len - 1 - total;
		memcpy(result_buf + total, buf, add + 1);
		total += add;
	}
	close(pipefd[0]);
	int status;
	waitpid(pid, &status, 0);
	if (timed_out && total < max_len - 32)
		snprintf(result_buf + total, max_len - total, "\n[Command timed out]");
	return 0;
}

static const tool_t SHELL_TOOL = {
	.name = "shell",
	.description = "Execute a shell command. Returns stdout and stderr. Dangerous commands are blocked.",
	.parameters_json = SHELL_PARAMS,
	.execute = shell_execute,
};

const tool_t *tool_shell_get(void)
{
	return &SHELL_TOOL;
}

/* Called by tool_set_config; declared in shell.h. */
void tool_shell_set_config(const config_t *cfg)
{
	g_shell_cfg = cfg;
}
