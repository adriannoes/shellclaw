/**
 * @file cli.c
 * @brief CLI channel: stdin/stdout, -m one-shot, interactive prompt.
 */
#define _POSIX_C_SOURCE 200809L

#include "channels/channel.h"
#include "core/config.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#define CLI_SESSION_ID "cli:default"
#define CLI_USER_ID    "default"
#define LINE_BUF_SIZE  8192

static char *g_one_shot;
static int g_verbose;

void channel_cli_set_one_shot(const char *msg)
{
	free(g_one_shot);
	g_one_shot = msg ? strdup(msg) : NULL;
}

void channel_cli_set_verbose(int v)
{
	g_verbose = v;
}

static int cli_init(const config_t *cfg)
{
	(void)cfg;
	return 0;
}

static int cli_poll(channel_incoming_msg_t *out, int timeout_ms)
{
	if (!out) return -1;
	memset(out, 0, sizeof(*out));
	if (g_one_shot) {
		out->session_id = strdup(CLI_SESSION_ID);
		out->user_id = strdup(CLI_USER_ID);
		out->text = strdup(g_one_shot);
		out->attachments = NULL;
		out->attachments_count = 0;
		free(g_one_shot);
		g_one_shot = NULL;
		return 1;
	}
	fd_set rfds;
	struct timeval tv;
	FD_ZERO(&rfds);
	FD_SET(STDIN_FILENO, &rfds);
	tv.tv_sec = timeout_ms / 1000;
	tv.tv_usec = (timeout_ms % 1000) * 1000;
	int r = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
	if (r <= 0) return r;
	if (!FD_ISSET(STDIN_FILENO, &rfds)) return 0;
	if (isatty(STDIN_FILENO))
		fprintf(stdout, "You: ");
	fflush(stdout);
	char line[LINE_BUF_SIZE];
	if (!fgets(line, sizeof(line), stdin)) return 0;
	size_t len = strlen(line);
	while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
		line[--len] = '\0';
	out->session_id = strdup(CLI_SESSION_ID);
	out->user_id = strdup(CLI_USER_ID);
	out->text = strdup(line);
	out->attachments = NULL;
	out->attachments_count = 0;
	return 1;
}

static int cli_send(const char *recipient, const char *text,
                    const channel_attachment_t *attachments, size_t att_count)
{
	(void)recipient;
	(void)attachments;
	(void)att_count;
	if (!text) return -1;
	if (g_verbose)
		fprintf(stdout, "\033[36mAgent:\033[0m %s\n", text);
	else
		fprintf(stdout, "%s\n", text);
	fflush(stdout);
	return 0;
}

static void cli_cleanup(void)
{
	free(g_one_shot);
	g_one_shot = NULL;
}

static const channel_t cli_channel = {
	.name = "cli",
	.init = cli_init,
	.poll = cli_poll,
	.send = cli_send,
	.cleanup = cli_cleanup,
};

const channel_t *channel_cli_get(void)
{
	return &cli_channel;
}
