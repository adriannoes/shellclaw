/**
 * @file hardware_libgpiod.h
 * @brief libgpiod v2 GPIO backend (Jetson / RPi).
 */

#ifndef SHELLCLAW_HARDWARE_LIBGPIOD_H
#define SHELLCLAW_HARDWARE_LIBGPIOD_H

#include "hardware/pin_table.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Bind the libgpiod backend to a board pin table.
 * @param table Pin table (must outlive the backend; not copied).
 * @return 0 on success, -1 on error.
 */
int hardware_libgpiod_init(const hardware_pin_table_t *table);

/** Release chips and line requests held by the backend. */
void hardware_libgpiod_shutdown(void);

/** Non-zero after successful #hardware_libgpiod_init. */
int hardware_libgpiod_is_available(void);

/**
 * Read a physical header pin (1–40).
 * @param pin Physical pin number.
 * @param value_out Set to 0 (LOW) or 1 (HIGH) on success.
 * @param errbuf Optional error buffer.
 * @param errbufsz Size of errbuf.
 * @return 0 on success, -1 on error.
 */
int hardware_gpio_read(int pin, int *value_out, char *errbuf, size_t errbufsz);

/**
 * Write a physical header pin (1–40).
 * @param pin Physical pin number.
 * @param value 0 = LOW, non-zero = HIGH.
 */
int hardware_gpio_write(int pin, int value, char *errbuf, size_t errbufsz);

/**
 * Configure pin direction. @p mode is "input" or "output".
 */
int hardware_gpio_mode(int pin, const char *mode, char *errbuf, size_t errbufsz);

/**
 * Override pin table for unit tests (pass NULL to clear).
 * Only available when compiling tests with SHELLCLAW_HARDWARE_LIBGPIOD_TEST.
 */
void hardware_libgpiod_set_pin_table_for_test(const hardware_pin_table_t *table);

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_HARDWARE_LIBGPIOD_H */
