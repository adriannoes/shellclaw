/**
 * @file hardware_stub.c
 * @brief No-op hardware backend for builds without Pi GPIO/I2C.
 */
#define _POSIX_C_SOURCE 200809L

#include "hardware/hardware.h"
#include <stddef.h>

static int s_hardware_stub_ready;

int hardware_stub_init(const config_t *cfg)
{
	(void)cfg;
	s_hardware_stub_ready = 1;
	return 0;
}

int hardware_stub_is_available(void)
{
	return s_hardware_stub_ready ? 1 : 0;
}
