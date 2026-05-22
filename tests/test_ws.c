/**
 * @file test_ws.c
 * @brief WebSocket connection table and MSG_MAX enforcement (no libwebsockets).
 */
#define _POSIX_C_SOURCE 200809L

#include "gateway/ws.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT(c)                                                                              \
	do {                                                                                   \
		if (!(c)) {                                                                    \
			fprintf(stderr, "FAIL: %s:%d %s\n", __FILE__, __LINE__, #c);            \
			return 1;                                                                \
		}                                                                                \
	} while (0)
#define RUN(t)                                                                                 \
	do {                                                                                   \
		int _r = (t);                                                                  \
		if (_r)                                                                        \
			return _r;                                                             \
	} while (0)

/** Must match MSG_MAX in src/gateway/ws.c */
#define WS_MSG_MAX 8192

static int test_register_conn_full_table(void)
{
	int i;
	ws_cleanup();
	for (i = 1; i <= 16; i++)
		ASSERT(ws_register_conn(i, (ws_conn_t)(intptr_t)i) == 0);
	ASSERT(ws_register_conn(17, (ws_conn_t)(intptr_t)17) == -1);
	ws_cleanup();
	return 0;
}

static int test_push_incoming_msg_max(void)
{
	char *big;
	char session[32];
	char text[64];
	int got;
	ws_cleanup();
	ASSERT(ws_register_conn(1, (ws_conn_t)(intptr_t)1) == 0);
	big = malloc((size_t)WS_MSG_MAX + 2);
	ASSERT(big != NULL);
	memset(big, 'a', (size_t)WS_MSG_MAX + 1);
	big[WS_MSG_MAX + 1] = '\0';
	ws_push_incoming(1, big);
	got = ws_pop_incoming(session, sizeof(session), text, sizeof(text), 50);
	ASSERT(got == 0);
	ws_push_incoming(1, "ok");
	got = ws_pop_incoming(session, sizeof(session), text, sizeof(text), 500);
	ASSERT(got == 1);
	ASSERT(strcmp(text, "ok") == 0);
	free(big);
	ws_cleanup();
	return 0;
}

static int test_send_to_rejects_oversized(void)
{
	char *big;
	ws_cleanup();
	ASSERT(ws_register_conn(2, (ws_conn_t)(intptr_t)2) == 0);
	big = malloc((size_t)WS_MSG_MAX + 2);
	ASSERT(big != NULL);
	memset(big, 'b', (size_t)WS_MSG_MAX + 1);
	big[WS_MSG_MAX + 1] = '\0';
	ASSERT(ws_send_to("webchat:2", big) != 0);
	ASSERT(ws_send_to("webchat:2", "hi") == 0);
	free(big);
	ws_cleanup();
	return 0;
}

int main(void)
{
	RUN(test_register_conn_full_table());
	RUN(test_push_incoming_msg_max());
	RUN(test_send_to_rejects_oversized());
	printf("test_ws: all tests passed\n");
	return 0;
}
