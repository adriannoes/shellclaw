/**
 * @file heartbeat.c
 * @brief Heartbeat channel: fires at configured interval, injects tick into agent loop.
 */
#define _POSIX_C_SOURCE 200809L

#include "channels/heartbeat.h"
#include "channels/channel.h"
#include "core/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HEARTBEAT_PROMPT "Check for pending tasks, reminders, or scheduled items. If nothing urgent, respond briefly."
#define HEARTBEAT_SESSION "heartbeat:default"

static const config_t *g_heartbeat_cfg;
static time_t g_last_heartbeat;

#ifdef SHELLCLAW_TEST
static time_t g_test_now;
static int g_test_time_override;

void heartbeat_test_set_now(time_t now)
{
	g_test_now = now;
	g_test_time_override = 1;
}

void heartbeat_test_reset(void)
{
	g_test_time_override = 0;
	g_last_heartbeat = 0;
}
#endif

static time_t heartbeat_wall_now(void)
{
#ifdef SHELLCLAW_TEST
	if (g_test_time_override)
		return g_test_now;
#endif
	return time(NULL);
}

static int heartbeat_init(const config_t *cfg)
{
	g_heartbeat_cfg = cfg;
	g_last_heartbeat = heartbeat_wall_now();
	return 0;
}

static int heartbeat_poll(channel_incoming_msg_t *out, int timeout_ms)
{
	(void)timeout_ms;
	if (!out || !g_heartbeat_cfg) return -1;
	if (!config_heartbeat_enabled(g_heartbeat_cfg)) return 0;
	int interval_min = config_heartbeat_interval_minutes(g_heartbeat_cfg);
	if (interval_min <= 0) return 0;
	time_t now = heartbeat_wall_now();
	if ((long)(now - g_last_heartbeat) < interval_min * 60L) return 0;
	g_last_heartbeat = now;
	memset(out, 0, sizeof(*out));
	out->session_id = strdup(HEARTBEAT_SESSION);
	out->user_id = NULL;
	out->text = strdup(HEARTBEAT_PROMPT);
	out->attachments = NULL;
	out->attachments_count = 0;
	if (!out->session_id || !out->text) {
		channel_incoming_msg_clear(out);
		return -1;
	}
	return 1;
}

static int heartbeat_send(const char *recipient, const char *text,
                         const channel_attachment_t *attachments, size_t att_count)
{
	(void)recipient;
	(void)attachments;
	(void)att_count;
	if (!text) return -1;
	if (!g_heartbeat_cfg) {
		fprintf(stderr, "[heartbeat] %s\n", text);
		return 0;
	}
	const char *ch = config_heartbeat_default_channel(g_heartbeat_cfg);
	if (ch && strcmp(ch, "log") == 0) {
		fprintf(stderr, "[heartbeat] %s\n", text);
		return 0;
	}
	const char *target = (ch && ch[0]) ? ch : "cli";
	const channel_t *dest = channel_get_by_name(target);
	if (!dest || !dest->send) return -1;
	return dest->send("default", text, NULL, 0);
}

static void heartbeat_cleanup(void)
{
	g_heartbeat_cfg = NULL;
}

static const channel_t heartbeat_channel = {
	.name = "heartbeat",
	.init = heartbeat_init,
	.poll = heartbeat_poll,
	.send = heartbeat_send,
	.cleanup = heartbeat_cleanup,
};

const channel_t *channel_heartbeat_get(void)
{
	return &heartbeat_channel;
}

void heartbeat_set_live_config(const config_t *cfg)
{
	g_heartbeat_cfg = cfg;
}
