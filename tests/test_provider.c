/**
 * @file test_provider.c
 * @brief Verify provider_t vtable and stub provider compile and run.
 */

#include "providers/provider.h"
#include <stdio.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

static int test_stub_provider_vtable(void)
{
	const provider_t *p = provider_stub_get();
	ASSERT(p != NULL);
	ASSERT(p->name != NULL);
	ASSERT(p->init != NULL);
	ASSERT(p->chat != NULL);
	ASSERT(p->cleanup != NULL);
	ASSERT(p->init(NULL) == 0);
	provider_response_t response = {0};
	ASSERT(p->chat(NULL, 0, NULL, 0, &response) == 0);
	p->cleanup();
	provider_response_clear(&response);
	return 0;
}

int main(void)
{
	if (test_stub_provider_vtable() != 0) return 1;
	printf("test_provider: all tests passed\n");
	return 0;
}
