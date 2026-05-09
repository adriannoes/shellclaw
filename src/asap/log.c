/**
 * @file log.c
 * @brief Lock-protected ring buffer of last ASAP_LOG_RING_SIZE envelope events.
 *
 * Uses a pthread mutex for thread safety; the mutex is static and
 * initialized at compile time (PTHREAD_MUTEX_INITIALIZER).
 */
#define _POSIX_C_SOURCE 200809L

#include "asap/log.h"
#include <pthread.h>
#include <string.h>
#include <time.h>

static asap_log_entry_t s_ring[ASAP_LOG_RING_SIZE];
static int s_head = 0;
static int s_count = 0;
static pthread_mutex_t s_mu = PTHREAD_MUTEX_INITIALIZER;

static void copy_truncated(char *dst, size_t dst_sz, const char *src)
{
	size_t n;
	if (!dst || dst_sz == 0) return;
	if (!src) { dst[0] = '\0'; return; }
	n = strlen(src);
	if (n >= dst_sz) n = dst_sz - 1;
	memcpy(dst, src, n);
	dst[n] = '\0';
}

static void do_append(int direction, const char *payload_type,
	const char *id, const char *json_snippet)
{
	asap_log_entry_t *e = &s_ring[s_head];
	e->ts = time(NULL);
	e->direction = direction;
	copy_truncated(e->payload_type, sizeof e->payload_type, payload_type);
	copy_truncated(e->id, sizeof e->id, id);
	copy_truncated(e->json_snippet, sizeof e->json_snippet, json_snippet);
	s_head = (s_head + 1) % ASAP_LOG_RING_SIZE;
	if (s_count < ASAP_LOG_RING_SIZE) s_count++;
}

void asap_log_append_in(const char *payload_type, const char *id, const char *json_snippet)
{
	pthread_mutex_lock(&s_mu);
	do_append(ASAP_LOG_DIR_IN, payload_type, id, json_snippet);
	pthread_mutex_unlock(&s_mu);
}

void asap_log_append_out(const char *payload_type, const char *id, const char *json_snippet)
{
	pthread_mutex_lock(&s_mu);
	do_append(ASAP_LOG_DIR_OUT, payload_type, id, json_snippet);
	pthread_mutex_unlock(&s_mu);
}

int asap_log_get_snapshot(asap_log_entry_t *out, int max_entries)
{
	int n;
	int start;
	int i;
	if (!out || max_entries <= 0) return 0;
	pthread_mutex_lock(&s_mu);
	n = s_count < max_entries ? s_count : max_entries;
	start = (s_head - s_count + ASAP_LOG_RING_SIZE * 2) % ASAP_LOG_RING_SIZE;
	for (i = 0; i < n; i++)
		out[i] = s_ring[(start + i) % ASAP_LOG_RING_SIZE];
	pthread_mutex_unlock(&s_mu);
	return n;
}

void asap_log_reset(void)
{
	pthread_mutex_lock(&s_mu);
	s_head = 0;
	s_count = 0;
	memset(s_ring, 0, sizeof s_ring);
	pthread_mutex_unlock(&s_mu);
}
