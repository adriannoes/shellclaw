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

static const char *SAMPLE_TR =
	"{\"jsonrpc\":\"2.0\",\"method\":\"asap.send\","
	"\"params\":{"
	"\"id\":\"01HZABC123\","
	"\"asap_version\":\"2.1\","
	"\"sender\":\"urn:asap:agent:a\","
	"\"recipient\":\"urn:asap:agent:b\","
	"\"payload_type\":\"task.request\","
	"\"payload\":{\"input\":\"x\"},"
	"\"correlation_id\":\"c1\","
	"\"trace_id\":\"t1\","
	"\"timestamp\":\"2026-01-01T00:00:00Z\""
	"},\"id\":42}";

static int test_parse_valid(void)
{
	asap_envelope_t env;
	asap_envelope_init(&env);
	cJSON *err = NULL;
	int r = asap_envelope_parse(SAMPLE_TR, &env, &err);
	if (err) cJSON_Delete(err);
	ASSERT(r == 0);
	ASSERT(err == NULL);
	ASSERT(env.jsonrpc_request_id != NULL);
	char *id_printed = cJSON_PrintUnformatted(env.jsonrpc_request_id);
	ASSERT(id_printed && strcmp(id_printed, "42") == 0);
	free(id_printed);
	ASSERT(strcmp(env.id, "01HZABC123") == 0);
	ASSERT(strcmp(env.asap_version, "2.1") == 0);
	ASSERT(strcmp(env.sender, "urn:asap:agent:a") == 0);
	ASSERT(strcmp(env.recipient, "urn:asap:agent:b") == 0);
	ASSERT(strcmp(env.payload_type, "task.request") == 0);
	ASSERT(env.payload != NULL);
	cJSON *inp = cJSON_GetObjectItem(env.payload, "input");
	ASSERT(inp && cJSON_IsString(inp) && strcmp(inp->valuestring, "x") == 0);
	ASSERT(strcmp(env.correlation_id, "c1") == 0);
	ASSERT(strcmp(env.trace_id, "t1") == 0);
	ASSERT(strcmp(env.timestamp, "2026-01-01T00:00:00Z") == 0);
	asap_envelope_clear(&env);
	return 0;
}

#define ASSERT_ERR_CODE(err, want) do { \
	cJSON *_e = cJSON_GetObjectItem((err), "error"); \
	ASSERT(_e != NULL); \
	cJSON *_c = cJSON_GetObjectItem(_e, "code"); \
	ASSERT(_c != NULL && cJSON_IsNumber(_c) && (int)_c->valuedouble == (want)); \
} while (0)

static int test_parse_unquoted_body_fails(void)
{
	asap_envelope_t env;
	cJSON *err = NULL;
	int r = asap_envelope_parse("not json", &env, &err);
	ASSERT(r == -1);
	ASSERT(err);
	ASSERT_ERR_CODE(err, -32700);
	cJSON_Delete(err);
	return 0;
}

static int test_parse_wrong_jsonrpc(void)
{
	const char *j = "{\"jsonrpc\":\"1.0\",\"method\":\"a\",\"params\":{},\"id\":1}";
	asap_envelope_t env;
	cJSON *err = NULL;
	int r = asap_envelope_parse(j, &env, &err);
	ASSERT(r == -1);
	ASSERT(err);
	ASSERT_ERR_CODE(err, -32602);
	cJSON_Delete(err);
	return 0;
}

static int test_parse_missing_field(const char *params_json, int want_code)
{
	char buf[1024];
	snprintf(buf, sizeof buf,
		"{\"jsonrpc\":\"2.0\",\"method\":\"m\",\"params\":%s,\"id\":null}", params_json);
	asap_envelope_t env;
	cJSON *err = NULL;
	int r = asap_envelope_parse(buf, &env, &err);
	ASSERT(r == -1);
	ASSERT(err);
	ASSERT_ERR_CODE(err, want_code);
	cJSON_Delete(err);
	return 0;
}

static int test_parse_all_missing_in_turn(void)
{
	/* each required key absent from a complete baseline */
	if (test_parse_missing_field(
		"{\"asap_version\":\"2.1\",\"sender\":\"s\",\"recipient\":\"r\","
		"\"payload_type\":\"task.request\",\"payload\":{}}", -32602) != 0) return 1;
	if (test_parse_missing_field(
		"{\"id\":\"i\",\"sender\":\"s\",\"recipient\":\"r\","
		"\"payload_type\":\"task.request\",\"payload\":{}}", -32602) != 0) return 1;
	if (test_parse_missing_field(
		"{\"id\":\"i\",\"asap_version\":\"2.1\",\"recipient\":\"r\","
		"\"payload_type\":\"task.request\",\"payload\":{}}", -32602) != 0) return 1;
	if (test_parse_missing_field(
		"{\"id\":\"i\",\"asap_version\":\"2.1\",\"sender\":\"s\","
		"\"payload_type\":\"task.request\",\"payload\":{}}", -32602) != 0) return 1;
	if (test_parse_missing_field(
		"{\"id\":\"i\",\"asap_version\":\"2.1\",\"sender\":\"s\",\"recipient\":\"r\","
		"\"payload\":{}}", -32602) != 0) return 1;
	if (test_parse_missing_field(
		"{\"id\":\"i\",\"asap_version\":\"2.1\",\"sender\":\"s\",\"recipient\":\"r\","
		"\"payload_type\":\"task.request\"}", -32602) != 0) return 1;
	return 0;
}

static int test_parse_wrong_types(void)
{
	if (test_parse_missing_field(
		"{\"id\":1,\"asap_version\":\"2.1\",\"sender\":\"s\",\"recipient\":\"r\","
		"\"payload_type\":\"task.request\",\"payload\":{}}", -32602) != 0) return 1;
	if (test_parse_missing_field(
		"{\"id\":\"i\",\"asap_version\":2.1,\"sender\":\"s\",\"recipient\":\"r\","
		"\"payload_type\":\"task.request\",\"payload\":{}}", -32602) != 0) return 1;
	if (test_parse_missing_field(
		"{\"id\":\"i\",\"asap_version\":\"2.1\",\"sender\":\"s\",\"recipient\":\"r\","
		"\"payload_type\":\"task.request\",\"payload\":null}", -32602) != 0) return 1;
	return 0;
}

static int test_parse_unknown_payload_type(void)
{
	if (test_parse_missing_field(
		"{\"id\":\"i\",\"asap_version\":\"2.1\",\"sender\":\"s\",\"recipient\":\"r\","
		"\"payload_type\":\"foo.bar\",\"payload\":{}}", -32602) != 0) return 1;
	return 0;
}

static int test_parse_params_not_object(void)
{
	const char *j = "{\"jsonrpc\":\"2.0\",\"method\":\"m\",\"params\":[],\"id\":1}";
	asap_envelope_t env;
	cJSON *err = NULL;
	ASSERT(asap_envelope_parse(j, &env, &err) == -1);
	ASSERT(err);
	ASSERT_ERR_CODE(err, -32602);
	cJSON_Delete(err);
	return 0;
}

static int test_parse_bad_optional_type(void)
{
	if (test_parse_missing_field(
		"{\"id\":\"i\",\"asap_version\":\"2.1\",\"sender\":\"s\",\"recipient\":\"r\","
		"\"payload_type\":\"task.request\",\"payload\":{},"
		"\"trace_id\":123}", -32602) != 0) return 1;
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
	if (test_parse_valid() != 0) { fprintf(stderr, "test_parse_valid failed\n"); failed++; }
	if (test_parse_unquoted_body_fails() != 0) { fprintf(stderr, "test_parse_unquoted_body_fails failed\n"); failed++; }
	if (test_parse_wrong_jsonrpc() != 0) { fprintf(stderr, "test_parse_wrong_jsonrpc failed\n"); failed++; }
	if (test_parse_all_missing_in_turn() != 0) { fprintf(stderr, "test_parse_all_missing_in_turn failed\n"); failed++; }
	if (test_parse_wrong_types() != 0) { fprintf(stderr, "test_parse_wrong_types failed\n"); failed++; }
	if (test_parse_unknown_payload_type() != 0) { fprintf(stderr, "test_parse_unknown_payload_type failed\n"); failed++; }
	if (test_parse_params_not_object() != 0) { fprintf(stderr, "test_parse_params_not_object failed\n"); failed++; }
	if (test_parse_bad_optional_type() != 0) { fprintf(stderr, "test_parse_bad_optional_type failed\n"); failed++; }
	if (failed == 0)
		printf("test_asap_envelope: all tests passed\n");
	return failed;
}
