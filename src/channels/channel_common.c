/**
 * @file channel_common.c
 * @brief Shared channel utilities: channel_incoming_msg_clear.
 */

#include "channels/channel.h"
#include <stdlib.h>
#include <string.h>

void channel_incoming_msg_clear(channel_incoming_msg_t *msg)
{
	if (!msg) return;
	free(msg->session_id);
	msg->session_id = NULL;
	free(msg->user_id);
	msg->user_id = NULL;
	free(msg->text);
	msg->text = NULL;
	if (msg->attachments) {
		for (size_t i = 0; i < msg->attachments_count; i++)
			free(msg->attachments[i].path_or_base64);
		free(msg->attachments);
		msg->attachments = NULL;
		msg->attachments_count = 0;
	}
}
