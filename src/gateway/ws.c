/**
 * @file ws.c
 * @brief WebSocket message queue and connection map.
 */
#define _POSIX_C_SOURCE 200809L

#include "gateway/ws.h"
#include <libwebsockets.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_CONNECTIONS 16
#define MAX_QUEUE 64
#define MSG_MAX 8192

typedef struct ws_msg {
	int conn_id;
	char *text;
	struct ws_msg *next;
} ws_msg_t;

typedef struct {
	ws_conn_t wsi;
	int conn_id;
} ws_conn_entry_t;

static ws_conn_entry_t g_conns[MAX_CONNECTIONS];
static ws_msg_t *g_queue_head;
static ws_msg_t *g_queue_tail;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
static int g_next_conn_id;
static struct lws_context *g_lws_ctx;

void ws_set_context(struct lws_context *ctx)
{
	g_lws_ctx = ctx;
}

int ws_next_conn_id(void)
{
	return __sync_add_and_fetch(&g_next_conn_id, 1);
}

void ws_register_conn(int conn_id, ws_conn_t wsi)
{
	pthread_mutex_lock(&g_mutex);
	for (int i = 0; i < MAX_CONNECTIONS; i++) {
		if (g_conns[i].wsi == NULL) {
			g_conns[i].wsi = wsi;
			g_conns[i].conn_id = conn_id;
			break;
		}
	}
	pthread_mutex_unlock(&g_mutex);
}

void ws_unregister_conn(int conn_id)
{
	pthread_mutex_lock(&g_mutex);
	for (int i = 0; i < MAX_CONNECTIONS; i++) {
		if (g_conns[i].conn_id == conn_id) {
			g_conns[i].wsi = NULL;
			g_conns[i].conn_id = 0;
			break;
		}
	}
	pthread_mutex_unlock(&g_mutex);
}

void ws_push_incoming(int conn_id, const char *text)
{
	if (!text) return;
	ws_msg_t *m = malloc(sizeof(*m));
	if (!m) return;
	m->conn_id = conn_id;
	m->text = strdup(text);
	m->next = NULL;
	if (!m->text) { free(m); return; }
	pthread_mutex_lock(&g_mutex);
	if (!g_queue_tail)
		g_queue_head = g_queue_tail = m;
	else {
		g_queue_tail->next = m;
		g_queue_tail = m;
	}
	pthread_cond_signal(&g_cond);
	pthread_mutex_unlock(&g_mutex);
}

static int parse_conn_id(const char *session_id)
{
	if (!session_id || strncmp(session_id, "webchat:", 8) != 0) return -1;
	return atoi(session_id + 8);
}

int ws_pop_incoming(char *session_id_out, size_t session_size,
                    char *text_out, size_t text_size, int timeout_ms)
{
	if (!session_id_out || session_size == 0 || !text_out || text_size == 0) return -1;
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += timeout_ms / 1000;
	ts.tv_nsec += (timeout_ms % 1000) * 1000000;
	if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
	pthread_mutex_lock(&g_mutex);
	while (!g_queue_head) {
		int r = pthread_cond_timedwait(&g_cond, &g_mutex, &ts);
		if (r == ETIMEDOUT) {
			pthread_mutex_unlock(&g_mutex);
			return 0;
		}
	}
	ws_msg_t *m = g_queue_head;
	g_queue_head = m->next;
	if (!g_queue_head) g_queue_tail = NULL;
	pthread_mutex_unlock(&g_mutex);
	snprintf(session_id_out, session_size, "webchat:%d", m->conn_id);
	size_t len = strlen(m->text);
	if (len >= text_size) len = text_size - 1;
	memcpy(text_out, m->text, len);
	text_out[len] = '\0';
	free(m->text);
	free(m);
	return 1;
}

int ws_send_to(const char *session_id, const char *text)
{
	if (!session_id || !text) return -1;
	int conn_id = parse_conn_id(session_id);
	if (conn_id < 0) return -1;
	ws_conn_t wsi = NULL;
	pthread_mutex_lock(&g_mutex);
	for (int i = 0; i < MAX_CONNECTIONS; i++) {
		if (g_conns[i].conn_id == conn_id) {
			wsi = g_conns[i].wsi;
			break;
		}
	}
	pthread_mutex_unlock(&g_mutex);
	if (!wsi || !g_lws_ctx) return -1;
	size_t len = strlen(text);
	unsigned char buf[LWS_PRE + MSG_MAX];
	if (len >= MSG_MAX) len = MSG_MAX - 1;
	memcpy(buf + LWS_PRE, text, len + 1);
	int n = lws_write((struct lws *)wsi, buf + LWS_PRE, len, LWS_WRITE_TEXT);
	return (n < 0) ? -1 : 0;
}
