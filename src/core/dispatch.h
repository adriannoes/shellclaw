/**
 * @file dispatch.h
 * @brief Incoming message dispatch: slash commands and agent run.
 */

#ifndef SHELLCLAW_DISPATCH_H
#define SHELLCLAW_DISPATCH_H

#include "channels/channel.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Handle an incoming channel message (/reset, /status, or agent run).
 * @return Result of channel send(), or -1 on internal error.
 */
int handle_message(const channel_t *ch, const channel_incoming_msg_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_DISPATCH_H */
