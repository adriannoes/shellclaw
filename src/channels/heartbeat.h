/**
 * @file heartbeat.h
 * @brief Heartbeat channel: periodic agent tick for autonomous activity.
 */
#ifndef SHELLCLAW_HEARTBEAT_H
#define SHELLCLAW_HEARTBEAT_H

#ifdef __cplusplus
extern "C" {
#endif

struct channel;
typedef struct channel channel_t;

/** Get the heartbeat channel (poll returns tick when interval elapsed, send routes to default or log). */
const channel_t *channel_heartbeat_get(void);

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_HEARTBEAT_H */
