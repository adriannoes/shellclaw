/**
 * @file stub.c
 * @brief Stub channel for tests and verification of channel_t vtable.
 */

#include "channels/channel.h"
#include "core/config.h"
#include <stddef.h>

static int stub_init(const config_t *cfg)
{
	(void)cfg;
	return 0;
}

static int stub_poll(channel_incoming_msg_t *out, int timeout_ms)
{
	(void)out;
	(void)timeout_ms;
	return 0;
}

static int stub_send(const char *recipient, const char *text,
                     const channel_attachment_t *attachments, size_t att_count)
{
	(void)recipient;
	(void)text;
	(void)attachments;
	(void)att_count;
	return 0;
}

static void stub_cleanup(void) {}

static const channel_t stub_channel = {
	.name = "stub",
	.init = stub_init,
	.poll = stub_poll,
	.send = stub_send,
	.cleanup = stub_cleanup,
};

const channel_t *channel_stub_get(void)
{
	return &stub_channel;
}
