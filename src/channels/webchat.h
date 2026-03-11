/**
 * @file webchat.h
 * @brief WebChat channel: WebSocket-based channel for agent loop.
 */

#ifndef SHELLCLAW_CHANNEL_WEBCHAT_H
#define SHELLCLAW_CHANNEL_WEBCHAT_H

#include "channels/channel.h"

#ifdef __cplusplus
extern "C" {
#endif

/** WebChat channel: poll returns from WS queue, send pushes to WS. */
const channel_t *channel_webchat_get(void);

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_CHANNEL_WEBCHAT_H */
