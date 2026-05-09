/**
 * @file rate_limit.c
 * @brief Per-IP sliding-window rate limiter for the ASAP endpoint.
 *
 * Fixed-size table with linear probing.  Each entry tracks the start of
 * the current 60-second window and the request count within it.
 * When a window expires the counter is reset automatically on the next hit.
 */
#define _POSIX_C_SOURCE 200809L

#include "gateway/rate_limit.h"
#include <pthread.h>
#include <string.h>
#include <time.h>

#define RATE_LIMIT_TABLE_SIZE 64
#define IP_BUF_SIZE 48

typedef struct rate_entry {
	char ip[IP_BUF_SIZE];
	time_t window_start;
	int count;
} rate_entry_t;

static rate_entry_t s_table[RATE_LIMIT_TABLE_SIZE];
static pthread_mutex_t s_mu = PTHREAD_MUTEX_INITIALIZER;

static unsigned int ip_hash(const char *ip)
{
	unsigned int h = 5381u;
	const unsigned char *p = (const unsigned char *)ip;
	while (*p) h = ((h << 5) + h) ^ (unsigned int)(*p++);
	return h;
}

static rate_entry_t *find_or_create(const char *ip)
{
	unsigned int idx = ip_hash(ip) % RATE_LIMIT_TABLE_SIZE;
	unsigned int i;
	int free_slot = -1;
	for (i = 0; i < RATE_LIMIT_TABLE_SIZE; i++) {
		unsigned int slot = (idx + i) % RATE_LIMIT_TABLE_SIZE;
		rate_entry_t *e = &s_table[slot];
		if (e->ip[0] == '\0') {
			if (free_slot < 0) free_slot = (int)slot;
			continue;
		}
		if (strncmp(e->ip, ip, IP_BUF_SIZE - 1) == 0)
			return e;
	}
	if (free_slot >= 0) {
		rate_entry_t *e = &s_table[free_slot];
		strncpy(e->ip, ip, IP_BUF_SIZE - 1);
		e->ip[IP_BUF_SIZE - 1] = '\0';
		e->window_start = 0;
		e->count = 0;
		return e;
	}
	/* Table full: evict the slot at the hash position (LRU would be better
	 * but adds complexity; this is a security best-effort guard). */
	rate_entry_t *e = &s_table[idx];
	strncpy(e->ip, ip, IP_BUF_SIZE - 1);
	e->ip[IP_BUF_SIZE - 1] = '\0';
	e->window_start = 0;
	e->count = 0;
	return e;
}

int rate_limit_asap(const char *ip, time_t now)
{
	const char *safe_ip;
	rate_entry_t *e;
	int limited;
	safe_ip = (ip && ip[0] != '\0') ? ip : "unknown";
	pthread_mutex_lock(&s_mu);
	e = find_or_create(safe_ip);
	if (now - e->window_start >= ASAP_RATE_WINDOW_SECS) {
		e->window_start = now;
		e->count = 0;
	}
	limited = (e->count >= ASAP_RATE_LIMIT_RPM) ? 1 : 0;
	if (!limited) e->count++;
	pthread_mutex_unlock(&s_mu);
	return limited;
}

void rate_limit_reset(void)
{
	pthread_mutex_lock(&s_mu);
	memset(s_table, 0, sizeof s_table);
	pthread_mutex_unlock(&s_mu);
}
