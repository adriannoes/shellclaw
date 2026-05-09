/**
 * @file ulid.h
 * @brief ULID (v1) generator: 48-bit ms timestamp, 80-bit randomness, Crockford Base32, 26 chars.
 */
#ifndef SHELLCLAW_ASAP_ULID_H
#define SHELLCLAW_ASAP_ULID_H

#include <stddef.h>

/** Encoded length without NUL. */
#define ULID_STRING_LEN 26

/**
 * Generate a new ULID. Uses /dev/urandom for the random component and is
 * monotonically non-decreasing when multiple IDs are created in the same
 * millisecond. Thread-safe.
 *
 * @param out       Destination; must be at least #ULID_STRING_LEN + 1 bytes
 * @param out_size  Size of @p out
 * @return          0 on success, -1 on error
 */
int ulid_generate(char *out, size_t out_size);

#endif /* SHELLCLAW_ASAP_ULID_H */
