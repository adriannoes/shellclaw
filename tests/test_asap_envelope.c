/**
 * @file test_asap_envelope.c
 * @brief Unit tests for ASAP envelope and JSON-RPC error helper.
 */
#define _POSIX_C_SOURCE 200809L

#include "asap/envelope.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

static int test_init_clear_frees_payload(void)
{
	asap_envelope_t env;
	asap_envelope_init(&env);
	ASSERT(env.payload == NULL);
	env.payload = cJSON_CreateObject();
	ASSERT(env.payload != NULL);
	cJSON_AddStringToObject(env.payload, "k", "v");
	ASSERT(cJSON_GetObjectItem(env.payload, "k") != NULL);
	asap_envelope_clear(&env);
	ASSERT(env.payload == NULL);
	return 0;
}

static int test_clear_idempotent(void)
{
	asap_envelope_t env;
	asap_envelope_init(&env);
	env.id = strdup("e1");
	ASSERT(env.id != NULL);
	asap_envelope_clear(&env);
	ASSERT(env.id == NULL);
	asap_envelope_clear(&env);
	return 0;
}

static int test_jsonrpc_error_shape(void)
{
	cJSON *req_id = cJSON_CreateString("r1");
	ASSERT(req_id != NULL);
	cJSON *err = asap_jsonrpc_error(-32602, "Invalid params", req_id);
	cJSON_Delete(req_id);
	ASSERT(err != NULL);
	cJSON *jv = cJSON_GetObjectItem(err, "jsonrpc");
	ASSERT(jv && cJSON_IsString(jv) && strcmp(jv->valuestring, "2.0") == 0);
	cJSON *e = cJSON_GetObjectItem(err, "error");
	ASSERT(e && cJSON_IsObject(e));
	cJSON *code = cJSON_GetObjectItem(e, "code");
	ASSERT(code && cJSON_IsNumber(code) && (int)code->valuedouble == -32602);
	cJSON *msg = cJSON_GetObjectItem(e, "message");
	ASSERT(msg && cJSON_IsString(msg) && strcmp(msg->valuestring, "Invalid params") == 0);
	cJSON *id = cJSON_GetObjectItem(err, "id");
	ASSERT(id && cJSON_IsString(id) && strcmp(id->valuestring, "r1") == 0);
	cJSON_Delete(err);
	return 0;
}

static int test_jsonrpc_error_null_id(void)
{
	cJSON *err = asap_jsonrpc_error(-32600, "bad", NULL);
	ASSERT(err != NULL);
	cJSON *id = cJSON_GetObjectItem(err, "id");
	ASSERT(id && cJSON_IsNull(id));
	cJSON_Delete(err);
	return 0;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	int failed = 0;
	if (test_init_clear_frees_payload() != 0) { fprintf(stderr, "test_init_clear_frees_payload failed\n"); failed++; }
	if (test_clear_idempotent() != 0) { fprintf(stderr, "test_clear_idempotent failed\n"); failed++; }
	if (test_jsonrpc_error_shape() != 0) { fprintf(stderr, "test_jsonrpc_error_shape failed\n"); failed++; }
	if (test_jsonrpc_error_null_id() != 0) { fprintf(stderr, "test_jsonrpc_error_null_id failed\n"); failed++; }
	if (failed == 0)
		printf("test_asap_envelope: all tests passed\n");
	return failed;
}
