/**
 * @file hardware.h
 * @brief Hardware abstraction (GPIO, sensors) — stub for Phase 5.
 */

#ifndef SHELLCLAW_HARDWARE_H
#define SHELLCLAW_HARDWARE_H

#ifdef __cplusplus
extern "C" {
#endif

struct config;
typedef struct config config_t;

/**
 * Optional stub init invoked from the tool registry when config is applied.
 * Real GPIO/I2C backends replace this in Phase 5.
 * @return 0 on success (stub always succeeds), -1 on fatal init error.
 */
int hardware_stub_init(const config_t *cfg);

/** Returns 1 when the stub layer reports hardware ready (always 1 for stub). */
int hardware_stub_is_available(void);

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_HARDWARE_H */
