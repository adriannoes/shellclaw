/**
 * @file crypto.h
 * @brief Cryptographic helpers: OS randomness and Ed25519 sign/verify (stub for Phase 5).
 */

#ifndef SHELLCLAW_CRYPTO_H
#define SHELLCLAW_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Ed25519 public key size (bytes). */
#define CRYPTO_ED25519_PUBLIC_KEY_SIZE 32U
/** Ed25519 private key size (bytes). */
#define CRYPTO_ED25519_PRIVATE_KEY_SIZE 64U
/** Ed25519 signature size (bytes). */
#define CRYPTO_ED25519_SIGNATURE_SIZE 64U

/**
 * Read @p len cryptographically strong random bytes from the OS (e.g. `/dev/urandom`).
 * @return 0 on success, -1 on invalid args or I/O failure.
 */
int crypto_read_urandom(void *buf, size_t len);

/**
 * Stub Ed25519 sign: deterministic placeholder signature for tests and future hardware integration.
 * @return 0 on success, -1 on invalid args.
 */
int crypto_ed25519_sign(const uint8_t *private_key, size_t private_key_len,
                        const uint8_t *message, size_t message_len,
                        uint8_t *signature_out, size_t signature_out_len);

/**
 * Stub Ed25519 verify: accepts signatures produced by @ref crypto_ed25519_sign for the same message/key.
 * @return 1 if valid, 0 if invalid, -1 on invalid args.
 */
int crypto_ed25519_verify(const uint8_t *public_key, size_t public_key_len,
                          const uint8_t *message, size_t message_len,
                          const uint8_t *signature, size_t signature_len);

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_CRYPTO_H */
