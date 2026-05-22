/**
 * @file test_hardware_stub.c
 * @brief hardware_stub_init availability smoke test.
 */

#include "hardware/hardware.h"
#include "core/config.h"
#include <stdio.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
#define RUN(t) do { int _r = (t); if (_r) return _r; } while (0)

static int test_hardware_stub_init(void)
{
	const char *path = "/tmp/shellclaw_test_hw_stub.toml";
	FILE *f = fopen(path, "w");
	config_t *cfg = NULL;
	char errbuf[256];
	ASSERT(f);
	fprintf(f, "[agent]\nmodel = \"test\"\n");
	fclose(f);
	ASSERT(config_load(path, &cfg, errbuf, sizeof(errbuf)) == 0);
	ASSERT(hardware_stub_init(cfg) == 0);
	ASSERT(hardware_stub_is_available() == 1);
	ASSERT(hardware_stub_init(NULL) == 0);
	config_free(cfg);
	remove(path);
	return 0;
}

int main(void)
{
	RUN(test_hardware_stub_init());
	printf("test_hardware_stub: all tests passed\n");
	return 0;
}
