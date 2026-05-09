/**
 * @file asap_invoke.c
 * @brief asap_invoke tool: delegate tasks to remote ASAP agents.
 *
 * Execution flow:
 *   1. Parse args: urn (required), skill_id (optional), input (object, required).
 *   2. Build a task.request envelope with a fresh ULID as id, sender from config.
 *   3. Resolve urn via registry (5-min TTL cache) + fresh revocation check.
 *   4. POST the envelope to the resolved base_url via asap_client_send_task.
 *   5. Return the task.response payload serialised as JSON, or a clear error string.
 */
#define _POSIX_C_SOURCE 200809L

#include "tools/asap_invoke.h"
#include "asap/envelope.h"
#include "asap/client.h"
#include "asap/registry.h"
#include "asap/ulid.h"
#include "asap/asap_version.h"
#include "core/config.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* JSON Schema exposed to the LLM — mirrors OpenAI/Anthropic tool spec. */
#define ASAP_INVOKE_PARAMS \
	"{\"type\":\"object\"," \
	"\"properties\":{" \
	"\"urn\":{\"type\":\"string\",\"description\":\"Target agent URN (e.g. urn:asap:agent:name)\"}," \
	"\"skill_id\":{\"type\":\"string\",\"description\":\"Optional skill identifier on the remote agent\"}," \
	"\"input\":{\"type\":\"object\",\"description\":\"Free-form task input sent as the payload\"}" \
	"}," \
	"\"required\":[\"urn\",\"input\"]}"

/* Module-level state — set once via tool_asap_invoke_set_config. */
static const config_t *s_cfg;
static registry_cache_t s_registry_cache;
static int s_cache_initialised;

static void ensure_cache_init(void)
{
	if (!s_cache_initialised) {
		registry_cache_init(&s_registry_cache);
		s_cache_initialised = 1;
	}
}

#ifdef SHELLCLAW_ASAP_INVOKE_TEST
/* asap_invoke_send_fn typedef is provided by the header when test mode is on. */
static asap_invoke_send_fn s_send_fn;
static const char *s_test_registry_url;

void asap_invoke_test_set_send_fn(asap_invoke_send_fn fn)
{
	s_send_fn = fn;
}

void asap_invoke_test_reset_cache(void)
{
	registry_cache_clear(&s_registry_cache);
	registry_cache_init(&s_registry_cache);
}

int asap_invoke_test_load_registry(const char *url, const char *json)
{
	char errbuf[256];
	ensure_cache_init();
	return registry_cache_test_load_json(&s_registry_cache, url, json,
	        errbuf, sizeof(errbuf));
}

void asap_invoke_test_set_registry_url(const char *url)
{
	s_test_registry_url = url;
}
#endif /* SHELLCLAW_ASAP_INVOKE_TEST */

void tool_asap_invoke_set_config(const config_t *cfg)
{
	s_cfg = cfg;
	ensure_cache_init();
}

/**
 * Extract a human-readable result string from a task.response envelope.
 * Prefers payload.result (string); falls back to serialised payload; falls back
 * to a placeholder when the payload is absent or not an object.
 * Writes at most max_len-1 bytes to buf.
 */
static void extract_response_text(const asap_envelope_t *resp,
        char *buf, size_t max_len)
{
	if (!resp->payload) {
		snprintf(buf, max_len, "(empty response)");
		return;
	}
	cJSON *result = cJSON_GetObjectItem(resp->payload, "result");
	if (result && cJSON_IsString(result)) {
		snprintf(buf, max_len, "%s", result->valuestring);
		return;
	}
	char *s = cJSON_PrintUnformatted(resp->payload);
	if (s) {
		snprintf(buf, max_len, "%s", s);
		free(s);
	} else {
		snprintf(buf, max_len, "(serialisation error)");
	}
}

static int asap_invoke_execute(const char *args_json, char *result_buf, size_t max_len)
{
	cJSON *root = NULL;
	cJSON *urn_node, *skill_node, *input_node;
	const char *urn;
	asap_envelope_t req_env, resp_env;
	registry_resolve_ctx_t rctx;
	registry_agent_t agent;
	asap_client_config_t ccfg;
	cJSON *payload = NULL;
	char ulid_buf[ULID_STRING_LEN + 1];
	char errbuf[256];
	int ret = -1;
	int send_result = -1;
	const char *sender;

	if (!args_json || !result_buf || max_len == 0) return -1;
	errbuf[0] = '\0';

	root = cJSON_Parse(args_json);
	if (!root) {
		snprintf(result_buf, max_len, "{\"error\":\"invalid JSON\"}");
		return -1;
	}

	urn_node = cJSON_GetObjectItem(root, "urn");
	if (!cJSON_IsString(urn_node) || !urn_node->valuestring[0]) {
		snprintf(result_buf, max_len, "{\"error\":\"urn required\"}");
		cJSON_Delete(root);
		return -1;
	}
	urn = urn_node->valuestring;

	input_node = cJSON_GetObjectItem(root, "input");
	if (!input_node || !cJSON_IsObject(input_node)) {
		snprintf(result_buf, max_len, "{\"error\":\"input object required\"}");
		cJSON_Delete(root);
		return -1;
	}

	skill_node = cJSON_GetObjectItem(root, "skill_id");

	/* Build task.request payload: {skill_id?, input} */
	payload = cJSON_CreateObject();
	if (!payload) {
		snprintf(result_buf, max_len, "{\"error\":\"out of memory\"}");
		cJSON_Delete(root);
		return -1;
	}
	if (skill_node && cJSON_IsString(skill_node) && skill_node->valuestring[0]) {
		if (!cJSON_AddStringToObject(payload, "skill_id", skill_node->valuestring)) {
			cJSON_Delete(payload);
			cJSON_Delete(root);
			snprintf(result_buf, max_len, "{\"error\":\"out of memory\"}");
			return -1;
		}
	}
	cJSON *input_dup = cJSON_Duplicate(input_node, 1);
	if (!input_dup || !cJSON_AddItemToObject(payload, "input", input_dup)) {
		if (input_dup) cJSON_Delete(input_dup);
		cJSON_Delete(payload);
		cJSON_Delete(root);
		snprintf(result_buf, max_len, "{\"error\":\"out of memory\"}");
		return -1;
	}

	/* Generate ULID for envelope id */
	if (ulid_generate(ulid_buf, sizeof(ulid_buf)) != 0) {
		snprintf(result_buf, max_len, "{\"error\":\"failed to generate ULID\"}");
		cJSON_Delete(payload);
		cJSON_Delete(root);
		return -1;
	}

	/* Build the request envelope */
	asap_envelope_init(&req_env);
	req_env.id = strdup(ulid_buf);
	req_env.asap_version = strdup(ASAP_PROTOCOL_VERSION);
	sender = (s_cfg && config_asap_agent_urn(s_cfg)) ? config_asap_agent_urn(s_cfg) : "";
	req_env.sender = strdup(sender);
	req_env.recipient = strdup(urn);
	req_env.payload_type = strdup("task.request");
	req_env.payload = payload; /* ownership transferred */
	payload = NULL;

	if (!req_env.id || !req_env.asap_version || !req_env.sender ||
	    !req_env.recipient || !req_env.payload_type) {
		snprintf(result_buf, max_len, "{\"error\":\"out of memory\"}");
		asap_envelope_clear(&req_env);
		cJSON_Delete(root);
		return -1;
	}

	/* Resolve URN via registry (cache + revocation check) */
	ensure_cache_init();
	asap_client_config_from_config(s_cfg, &ccfg);
	registry_resolve_ctx_init(&rctx);
	rctx.cache = &s_registry_cache;
#ifdef SHELLCLAW_ASAP_INVOKE_TEST
	rctx.registry_url = s_test_registry_url
	        ? s_test_registry_url
	        : (s_cfg ? config_asap_registry_url(s_cfg) : NULL);
#else
	rctx.registry_url = s_cfg ? config_asap_registry_url(s_cfg) : NULL;
#endif
	rctx.revocation_list_url = s_cfg ? config_asap_revocation_list_url(s_cfg) : NULL;
	rctx.client_opt = &ccfg;

	memset(&agent, 0, sizeof(agent));
	if (registry_resolve(&rctx, urn, &agent, errbuf, sizeof(errbuf)) != 0) {
		snprintf(result_buf, max_len, "{\"error\":\"registry resolve failed: %s\"}",
		        errbuf[0] ? errbuf : "unknown");
		asap_envelope_clear(&req_env);
		cJSON_Delete(root);
		return -1;
	}

	/* Delegate to client */
	asap_envelope_init(&resp_env);

#ifdef SHELLCLAW_ASAP_INVOKE_TEST
	if (s_send_fn) {
		send_result = s_send_fn(agent.base_url, NULL, ASAP_DEFAULT_JSONRPC_METHOD,
		        &req_env, &ccfg, s_cfg, &resp_env, errbuf, sizeof(errbuf));
	} else {
		send_result = asap_client_send_task(agent.base_url, NULL, ASAP_DEFAULT_JSONRPC_METHOD,
		        &req_env, &ccfg, s_cfg, &resp_env, errbuf, sizeof(errbuf));
	}
#else
	send_result = asap_client_send_task(agent.base_url, NULL, ASAP_DEFAULT_JSONRPC_METHOD,
	        &req_env, &ccfg, s_cfg, &resp_env, errbuf, sizeof(errbuf));
#endif

	if (send_result != 0) {
		snprintf(result_buf, max_len, "{\"error\":\"send_task failed: %s\"}",
		        errbuf[0] ? errbuf : "transport error");
		asap_envelope_clear(&req_env);
		asap_envelope_clear(&resp_env);
		registry_agent_clear(&agent);
		cJSON_Delete(root);
		return -1;
	}

	extract_response_text(&resp_env, result_buf, max_len);
	ret = 0;

	asap_envelope_clear(&req_env);
	asap_envelope_clear(&resp_env);
	registry_agent_clear(&agent);
	cJSON_Delete(root);
	return ret;
}

static const tool_t ASAP_INVOKE_TOOL = {
	.name = "asap_invoke",
	.description = "Delegate a task to a remote ASAP agent identified by URN. "
	               "Resolves the agent via the registry, checks revocation, "
	               "sends a task.request, and returns the response.",
	.parameters_json = ASAP_INVOKE_PARAMS,
	.execute = asap_invoke_execute,
};

const tool_t *tool_asap_invoke_get(void)
{
	return &ASAP_INVOKE_TOOL;
}
