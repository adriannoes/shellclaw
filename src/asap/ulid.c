/**
 * @file ulid.c
 * @brief ULID generation (Crockford Base32, monotonic within same millisecond).
 */
#define _POSIX_C_SOURCE 200809L

#include "asap/ulid.h"
#include "crypto/crypto.h"
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
/** Crockford Base32 (no I, L, O, U). */
static const char C32[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

static pthread_mutex_t g_ulid_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_last_ms;
static uint8_t g_rand[10];

static int get_epoch_ms_u48(uint64_t *out_ms)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return -1;
	uint64_t ms = (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
	*out_ms = ms & 0xFFFFFFFFFFFFu;
	return 0;
}

/**
 * Big-endian 80-bit increment. Returns 1 on overflow to zero.
 */
static int u80_inc(uint8_t b[10])
{
	for (int i = 9; i >= 0; i--) {
		if ((unsigned)++b[i] != 0) return 0;
	}
	return 1;
}

/** Pack 48-bit timestamp into 6 big-endian bytes (MSB first). */
static void time_to_bytes(uint64_t t48, uint8_t dst[6])
{
	for (int i = 0; i < 6; i++)
		dst[i] = (uint8_t)((t48 >> (40 - 8 * i)) & 0xFFu);
}

/**
 * Encode 16 bytes to 26 Crockford characters (128 data bits + 2 zero padding bits = 26×5).
 * Bit 0 of the stream = MSB of bytes[0].
 */
static void encode_bytes(const uint8_t bytes[16], char *out)
{
	for (int c = 0; c < 26; c++) {
		unsigned v = 0;
		for (int k = 0; k < 5; k++) {
			int bit = c * 5 + k;
			int b;
			if (bit < 128)
				b = (bytes[bit / 8] >> (7 - (bit % 8))) & 1;
			else
				b = 0;
			v = (v << 1) | (unsigned)b;
		}
		out[c] = C32[v & 0x1Fu];
	}
	out[26] = '\0';
}

int ulid_generate(char *out, size_t out_size)
{
	uint8_t raw[16];
	uint64_t ms;

	if (!out || out_size < (size_t)ULID_STRING_LEN + 1u) return -1;

	if (get_epoch_ms_u48(&ms) != 0) return -1;

	if (pthread_mutex_lock(&g_ulid_lock) != 0) return -1;
	if (ms < g_last_ms) {
		g_last_ms = ms;
		if (crypto_read_urandom(g_rand, sizeof(g_rand)) != 0) {
			(void)pthread_mutex_unlock(&g_ulid_lock);
			return -1;
		}
	} else if (ms > g_last_ms) {
		g_last_ms = ms;
		if (crypto_read_urandom(g_rand, sizeof(g_rand)) != 0) {
			(void)pthread_mutex_unlock(&g_ulid_lock);
			return -1;
		}
	} else {
		if (u80_inc(g_rand) != 0) {
			do {
				if (get_epoch_ms_u48(&ms) != 0) {
					(void)pthread_mutex_unlock(&g_ulid_lock);
					return -1;
				}
			} while (ms <= g_last_ms);
			g_last_ms = ms;
			if (crypto_read_urandom(g_rand, sizeof(g_rand)) != 0) {
				(void)pthread_mutex_unlock(&g_ulid_lock);
				return -1;
			}
		}
	}
	time_to_bytes(g_last_ms, raw);
	(void)memcpy(raw + 6, g_rand, 10);
	encode_bytes(raw, out);
	(void)pthread_mutex_unlock(&g_ulid_lock);
	return 0;
}
