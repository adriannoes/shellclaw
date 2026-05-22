/**
 * @file daemon.c
 * @brief Double-fork daemon mode, PID file lock, log file 0600.
 */
#define _POSIX_C_SOURCE 200809L

#include "core/daemon.h"
#include "core/config.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_want_daemon;
static int g_pid_fd = -1;
static char *g_pid_path_alloc;

static int pid_file_try_lock_exclusive(int fd)
{
	struct flock fl;
	memset(&fl, 0, sizeof(fl));
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;
	if (fcntl(fd, F_SETLK, &fl) != 0)
		return -1;
	return 0;
}

void daemon_set_want(int want)
{
	g_want_daemon = want ? 1 : 0;
}

int finish_daemon_stdio_and_pid(void)
{
	char *base;
	char pidpath[PATH_MAX];
	char logpath[PATH_MAX];
	int logfd;
	int nullfd;
	const char *home = getenv("HOME");
	if (!home || !home[0]) {
		fprintf(stderr, "daemon: HOME is not set\n");
		return -1;
	}
	base = config_expand_tilde("~/.shellclaw");
	if (!base)
		return -1;
	if (mkdir(base, 0700) != 0 && errno != EEXIST) {
		fprintf(stderr, "daemon: mkdir %s: %s\n", base, strerror(errno));
		free(base);
		return -1;
	}
	if (snprintf(pidpath, sizeof(pidpath), "%s/shellclaw.pid", base) >= (int)sizeof(pidpath) ||
	    snprintf(logpath, sizeof(logpath), "%s/shellclaw.log", base) >= (int)sizeof(logpath)) {
		fprintf(stderr, "daemon: state path too long\n");
		free(base);
		return -1;
	}
	free(base);
	logfd = open(logpath, O_WRONLY | O_CREAT | O_APPEND, 0600);
	if (logfd < 0) {
		fprintf(stderr, "daemon: open log %s: %s\n", logpath, strerror(errno));
		return -1;
	}
	nullfd = open("/dev/null", O_RDONLY);
	if (nullfd < 0) {
		close(logfd);
		return -1;
	}
	if (dup2(nullfd, STDIN_FILENO) < 0 || dup2(logfd, STDOUT_FILENO) < 0 ||
	    dup2(logfd, STDERR_FILENO) < 0) {
		close(nullfd);
		close(logfd);
		return -1;
	}
	close(nullfd);
	close(logfd);
	g_pid_fd = open(pidpath, O_RDWR | O_CREAT, 0644);
	if (g_pid_fd < 0) {
		fprintf(stderr, "daemon: open pid file %s: %s\n", pidpath, strerror(errno));
		return -1;
	}
	if (pid_file_try_lock_exclusive(g_pid_fd) != 0) {
		fprintf(stderr, "daemon: another instance holds %s (pid file lock)\n", pidpath);
		fflush(stderr);
		close(g_pid_fd);
		g_pid_fd = -1;
		return -1;
	}
	if (ftruncate(g_pid_fd, 0) != 0) {
		fprintf(stderr, "daemon: ftruncate pid file: %s\n", strerror(errno));
		close(g_pid_fd);
		g_pid_fd = -1;
		return -1;
	}
	{
		char pidbuf[64];
		int n = snprintf(pidbuf, sizeof(pidbuf), "%ld\n", (long)getpid());
		ssize_t w;
		if (n <= 0 || n >= (int)sizeof(pidbuf)) {
			close(g_pid_fd);
			g_pid_fd = -1;
			return -1;
		}
		w = write(g_pid_fd, pidbuf, (size_t)n);
		if (w != (ssize_t)n) {
			fprintf(stderr, "daemon: write pid file: %s\n", strerror(errno));
			close(g_pid_fd);
			g_pid_fd = -1;
			return -1;
		}
	}
	g_pid_path_alloc = strdup(pidpath);
	if (!g_pid_path_alloc) {
		close(g_pid_fd);
		g_pid_fd = -1;
		return -1;
	}
	return 0;
}

int enter_daemon_mode(void)
{
	pid_t p1;
	pid_t p2;
	if (!g_want_daemon)
		return 0;
	fflush(NULL);
	p1 = fork();
	if (p1 < 0) {
		fprintf(stderr, "daemon: fork: %s\n", strerror(errno));
		return -1;
	}
	if (p1 > 0)
		exit(0);
	if (setsid() < 0) {
		fprintf(stderr, "daemon: setsid: %s\n", strerror(errno));
		return -1;
	}
	p2 = fork();
	if (p2 < 0) {
		fprintf(stderr, "daemon: second fork: %s\n", strerror(errno));
		return -1;
	}
	if (p2 > 0)
		exit(0);
	return finish_daemon_stdio_and_pid();
}

void daemon_pid_cleanup(void)
{
	if (g_pid_fd >= 0) {
		close(g_pid_fd);
		g_pid_fd = -1;
	}
	if (g_pid_path_alloc) {
		unlink(g_pid_path_alloc);
		free(g_pid_path_alloc);
		g_pid_path_alloc = NULL;
	}
}
