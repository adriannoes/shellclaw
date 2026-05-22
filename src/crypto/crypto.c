/**
 * @file crypto.c
 * @brief OS randomness and stub Ed25519 sign/verify (Phase 5 foundation).
 */
#define _POSIX_C_SOURCE 200809L

#include "crypto/crypto.h"
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static uint8_t stub_fold_byte(const uint8_t *data, size_t len, uint8_t seed)
{
	size_t i;
	uint8_t acc = seed;
	for (i = 0; i < len; i++)
		acc = (uint8_t)(acc ^ data[i]);
	return acc;
}

int crypto_read_urandom(void *buf, size_t len)
{
	int fd;
	uint8_t *out;
	size_t off;
	if (!buf && len > 0)
		return -1;
	if (len == 0)
		return 0;
	out = (uint8_t *)buf;
	fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0)
		return -1;
	off = 0;
	while (off < len) {
		ssize_t n;
		n = read(fd, out + off, len - off);
		if (n <= 0) {
			close(fd);
			return -1;
		}
		off += (size_t)n;
	}
	close(fd);
	return 0;
}

int crypto_ed25519_sign(const uint8_t *private_key, size_t private_key_len,
                        const uint8_t *message, size_t message_len,
                        uint8_t *signature_out, size_t signature_out_len)
{
	size_t i;
	uint8_t tag;
	if (!private_key || private_key_len == 0 || !message || !signature_out ||
	    signature_out_len < CRYPTO_ED25519_SIGNATURE_SIZE)
		return -1;
	tag = stub_fold_byte(private_key, private_key_len, 0xA5U);
	tag = stub_fold_byte(message, message_len, tag);
	for (i = 0; i < CRYPTO_ED25519_SIGNATURE_SIZE; i++)
		signature_out[i] = (uint8_t)(tag ^ (uint8_t)i ^ private_key[i % private_key_len]);
	return 0;
}

int crypto_ed25519_verify(const uint8_t *public_key, size_t public_key_len,
                          const uint8_t *message, size_t message_len,
                          const uint8_t *signature, size_t signature_len)
{
	uint8_t expected[CRYPTO_ED25519_SIGNATURE_SIZE];
	if (!public_key || public_key_len == 0 || !message || !signature ||
	    signature_len < CRYPTO_ED25519_SIGNATURE_SIZE)
		return -1;
	if (crypto_ed25519_sign(public_key, public_key_len, message, message_len,
	                        expected, sizeof(expected)) != 0)
		return -1;
	if (memcmp(expected, signature, CRYPTO_ED25519_SIGNATURE_SIZE) == 0)
		return 1;
	return 0;
}
