/**
 * @file test_asap_client.c
 * @brief Tests for JSON-RPC request/response helpers and ASAP client error paths.
 */
#define _POSIX_C_SOURCE 200809L

#include "asap/client.h"
#include "asap/envelope.h"
#include "cJSON.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

static int fill_min_task_request(asap_envelope_t *e)
{
	cJSON *pl = cJSON_CreateObject();
	if (!pl) return -1;
	if (!cJSON_AddStringToObject(pl, "input", "test")) { cJSON_Delete(pl); return -1; }
	asap_envelope_init(e);
	e->id = strdup("01HZTEST00");
	e->asap_version = strdup("2.1");
	e->sender = strdup("urn:asap:agent:from");
	e->recipient = strdup("urn:asap:agent:to");
	e->payload_type = strdup("task.request");
	e->payload = pl;
	return 0;
}

static int test_request_roundtrip_string(void)
{
	asap_envelope_t env;
	cJSON *id = cJSON_CreateString("rid-1");
	char *s;
	cJSON *root;
	cJSON *params;
	cJSON *rid;
	int ok;

	ASSERT(fill_min_task_request(&env) == 0);
	cJSON *dup_id = cJSON_Duplicate(id, 1);
	cJSON_Delete(id);
	s = asap_envelope_to_jsonrpc_request_string(&env, dup_id, "asap.send");
	cJSON_Delete(dup_id);
	ASSERT(s != NULL);
	ASSERT(strstr(s, "\"asap.send\"") != NULL);
	ASSERT(strstr(s, "task.request") != NULL);
	root = cJSON_Parse(s);
	free(s);
	ASSERT(root != NULL);
	rid = cJSON_GetObjectItemCaseSensitive(root, "id");
	ASSERT(rid && cJSON_IsString(rid) && strcmp(rid->valuestring, "rid-1") == 0);
	params = cJSON_GetObjectItemCaseSensitive(root, "params");
	ASSERT(params);
	ok = cJSON_GetObjectItemCaseSensitive(params, "id") && cJSON_GetObjectItemCaseSensitive(params, "payload");
	cJSON_Delete(root);
	ASSERT(ok);
	asap_envelope_clear(&env);
	return 0;
}

static int test_parse_response_ok(void)
{
	const char *json = "{\"jsonrpc\":\"2.0\","
		"\"result\":{"
		"\"id\":\"t1\","
		"\"asap_version\":\"2.1\","
		"\"sender\":\"a\",\"recipient\":\"b\","
		"\"payload_type\":\"task.response\","
		"\"payload\":{\"out\":\"x\"}"
		"},"
		"\"id\":\"rid\"}";
	char err[128];
	asap_envelope_t out;
	asap_envelope_init(&out);
	err[0] = 0;
	ASSERT(asap_envelope_parse_jsonrpc_response(json, &out, err, sizeof err) == 0);
	ASSERT(strcmp(err, "") == 0);
	ASSERT(out.jsonrpc_request_id != NULL);
	ASSERT(strcmp(out.id, "t1") == 0);
	ASSERT(strcmp(out.payload_type, "task.response") == 0);
	ASSERT(out.payload != NULL);
	ASSERT(cJSON_GetObjectItem(out.payload, "out") != NULL);
	asap_envelope_clear(&out);
	return 0;
}

static int test_parse_response_jsonrpc_error(void)
{
	const char *json = "{\"jsonrpc\":\"2.0\","
		"\"error\":{\"code\":-32600,\"message\":\"nope\"},"
		"\"id\":1}";
	char err[128];
	asap_envelope_t out;
	asap_envelope_init(&out);
	err[0] = 0;
	ASSERT(asap_envelope_parse_jsonrpc_response(json, &out, err, sizeof err) == -1);
	ASSERT(strstr(err, "nope") != NULL);
	asap_envelope_clear(&out);
	return 0;
}

static int test_config_defaults(void)
{
	asap_client_config_t c;
	asap_client_config_init(&c);
	ASSERT(c.timeout_sec == 30L);
	ASSERT(c.ssl_verifypeer == 1);
	return 0;
}

static int test_send_fails_no_server(void)
{
	asap_envelope_t env, resp;
	char err[256];
	ASSERT(fill_min_task_request(&env) == 0);
	asap_envelope_init(&resp);
	ASSERT(asap_client_send_task("http://127.0.0.1:1/", NULL, "asap.send", &env, NULL, NULL, &resp, err, sizeof err) == -1);
	ASSERT(err[0] != '\0');
	asap_envelope_clear(&env);
	asap_envelope_clear(&resp);
	return 0;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	int failed = 0;
	curl_global_init(CURL_GLOBAL_DEFAULT);
	if (test_request_roundtrip_string() != 0) { fprintf(stderr, "test_request_roundtrip_string failed\n"); failed++; }
	if (test_parse_response_ok() != 0) { fprintf(stderr, "test_parse_response_ok failed\n"); failed++; }
	if (test_parse_response_jsonrpc_error() != 0) { fprintf(stderr, "test_parse_response_jsonrpc_error failed\n"); failed++; }
	if (test_config_defaults() != 0) { fprintf(stderr, "test_config_defaults failed\n"); failed++; }
	if (test_send_fails_no_server() != 0) { fprintf(stderr, "test_send_fails_no_server failed\n"); failed++; }
	curl_global_cleanup();
	if (failed == 0)
		printf("test_asap_client: all tests passed\n");
	return failed;
}
