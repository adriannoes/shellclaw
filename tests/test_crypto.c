/**
 * @file test_crypto.c
 * @brief crypto_read_urandom and stub Ed25519 sign/verify.
 */
#define _POSIX_C_SOURCE 200809L

#include "crypto/crypto.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
#define RUN(t) do { int _r = (t); if (_r) return _r; } while (0)

static int test_read_urandom(void)
{
	uint8_t a[16];
	uint8_t b[16];
	ASSERT(crypto_read_urandom(a, sizeof(a)) == 0);
	ASSERT(crypto_read_urandom(b, sizeof(b)) == 0);
	ASSERT(memcmp(a, b, sizeof(a)) != 0);
	ASSERT(crypto_read_urandom(NULL, 4) == -1);
	ASSERT(crypto_read_urandom(a, 0) == 0);
	return 0;
}

static int test_ed25519_sign_verify_roundtrip(void)
{
	uint8_t key[CRYPTO_ED25519_PUBLIC_KEY_SIZE];
	uint8_t sig[CRYPTO_ED25519_SIGNATURE_SIZE];
	const uint8_t msg[] = "shellclaw phase5";
	ASSERT(crypto_read_urandom(key, sizeof(key)) == 0);
	ASSERT(crypto_ed25519_sign(key, sizeof(key), msg, sizeof(msg) - 1U, sig, sizeof(sig)) == 0);
	ASSERT(crypto_ed25519_verify(key, sizeof(key), msg, sizeof(msg) - 1U, sig, sizeof(sig)) == 1);
	sig[0] ^= 0xFFU;
	ASSERT(crypto_ed25519_verify(key, sizeof(key), msg, sizeof(msg) - 1U, sig, sizeof(sig)) == 0);
	return 0;
}

int main(void)
{
	RUN(test_read_urandom());
	RUN(test_ed25519_sign_verify_roundtrip());
	printf("test_crypto: all tests passed\n");
	return 0;
}
