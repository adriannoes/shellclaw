/**
 * @file test_heartbeat.c
 * @brief Unit tests for heartbeat channel poll interval (SHELLCLAW_TEST time hooks).
 */
#define _POSIX_C_SOURCE 200809L

#include "channels/heartbeat.h"
#include "channels/channel.h"
#include "core/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
#define RUN(t) do { int _r = (t); if (_r) return _r; } while (0)

static int write_heartbeat_cfg(const char *path, int enabled, int interval_min)
{
	FILE *f = fopen(path, "w");
	if (!f) return -1;
	fprintf(f, "[agent]\nmodel = \"test\"\n");
	fprintf(f, "[heartbeat]\nenabled = %s\ninterval_minutes = %d\ndefault_channel = \"log\"\n",
	        enabled ? "true" : "false", interval_min);
	fclose(f);
	return 0;
}

static int test_poll_disabled_returns_zero(void)
{
	const char *path = "/tmp/shellclaw_test_hb_disabled.toml";
	config_t *cfg = NULL;
	char errbuf[256];
	const channel_t *ch;
	channel_incoming_msg_t msg;
	ASSERT(write_heartbeat_cfg(path, 0, 1) == 0);
	ASSERT(config_load(path, &cfg, errbuf, sizeof(errbuf)) == 0);
	ch = channel_heartbeat_get();
	ASSERT(ch != NULL);
	heartbeat_test_reset();
	heartbeat_test_set_now(1000);
	ASSERT(ch->init(cfg) == 0);
	heartbeat_test_set_now(1000);
	ASSERT(ch->poll(&msg, 0) == 0);
	ch->cleanup();
	config_free(cfg);
	remove(path);
	return 0;
}

static int test_poll_fires_after_interval(void)
{
	const char *path = "/tmp/shellclaw_test_hb_fire.toml";
	config_t *cfg = NULL;
	char errbuf[256];
	const channel_t *ch;
	channel_incoming_msg_t msg;
	ASSERT(write_heartbeat_cfg(path, 1, 1) == 0);
	ASSERT(config_load(path, &cfg, errbuf, sizeof(errbuf)) == 0);
	ch = channel_heartbeat_get();
	ASSERT(ch != NULL);
	heartbeat_test_reset();
	heartbeat_test_set_now(2000);
	ASSERT(ch->init(cfg) == 0);
	ASSERT(ch->poll(&msg, 0) == 0);
	heartbeat_test_set_now(2000 + 61);
	ASSERT(ch->poll(&msg, 0) == 1);
	ASSERT(msg.session_id != NULL);
	ASSERT(strstr(msg.session_id, "heartbeat:") != NULL);
	ASSERT(msg.text != NULL);
	ASSERT(strstr(msg.text, "pending tasks") != NULL);
	channel_incoming_msg_clear(&msg);
	ch->cleanup();
	config_free(cfg);
	remove(path);
	return 0;
}

static int test_poll_throttled_within_interval(void)
{
	const char *path = "/tmp/shellclaw_test_hb_throttle.toml";
	config_t *cfg = NULL;
	char errbuf[256];
	const channel_t *ch;
	channel_incoming_msg_t msg;
	ASSERT(write_heartbeat_cfg(path, 1, 5) == 0);
	ASSERT(config_load(path, &cfg, errbuf, sizeof(errbuf)) == 0);
	ch = channel_heartbeat_get();
	ASSERT(ch != NULL);
	heartbeat_test_reset();
	heartbeat_test_set_now(5000);
	ASSERT(ch->init(cfg) == 0);
	heartbeat_test_set_now(5000);
	ASSERT(ch->poll(&msg, 0) == 0);
	heartbeat_test_set_now(5000 + 120);
	ASSERT(ch->poll(&msg, 0) == 0);
	heartbeat_test_set_now(5000 + 301);
	ASSERT(ch->poll(&msg, 0) == 1);
	channel_incoming_msg_clear(&msg);
	ch->cleanup();
	config_free(cfg);
	remove(path);
	return 0;
}

int main(void)
{
	RUN(test_poll_disabled_returns_zero());
	RUN(test_poll_fires_after_interval());
	RUN(test_poll_throttled_within_interval());
	printf("test_heartbeat: all tests passed\n");
	return 0;
}
