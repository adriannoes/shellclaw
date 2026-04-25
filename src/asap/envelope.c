/**
 * @file envelope.c
 * @brief ASAP envelope lifecycle and JSON-RPC error object builder.
 */
#define _POSIX_C_SOURCE 200809L

#include "asap/envelope.h"
#include <stdlib.h>
#include <string.h>

void asap_envelope_init(asap_envelope_t *env)
{
	if (!env) return;
	memset(env, 0, sizeof(*env));
}

static void str_free(char **p)
{
	if (p && *p) {
		free(*p);
		*p = NULL;
	}
}

void asap_envelope_clear(asap_envelope_t *env)
{
	if (!env) return;
	str_free(&env->id);
	str_free(&env->asap_version);
	str_free(&env->sender);
	str_free(&env->recipient);
	str_free(&env->payload_type);
	if (env->payload) {
		cJSON_Delete(env->payload);
		env->payload = NULL;
	}
	str_free(&env->correlation_id);
	str_free(&env->trace_id);
	str_free(&env->timestamp);
}

cJSON *asap_jsonrpc_error(int code, const char *message, cJSON *id)
{
	if (!message) return NULL;
	cJSON *root = cJSON_CreateObject();
	if (!root) return NULL;
	cJSON_AddStringToObject(root, "jsonrpc", "2.0");
	cJSON *err = cJSON_CreateObject();
	if (!err) { cJSON_Delete(root); return NULL; }
	if (!cJSON_AddNumberToObject(err, "code", (double)code)) {
		cJSON_Delete(err);
		cJSON_Delete(root);
		return NULL;
	}
	if (!cJSON_AddStringToObject(err, "message", message)) {
		cJSON_Delete(err);
		cJSON_Delete(root);
		return NULL;
	}
	cJSON_AddItemToObject(root, "error", err);
	if (id) {
		cJSON *id_copy = cJSON_Duplicate(id, 1);
		if (!id_copy) { cJSON_Delete(root); return NULL; }
		cJSON_AddItemToObject(root, "id", id_copy);
	} else {
		if (!cJSON_AddNullToObject(root, "id")) {
			cJSON_Delete(root);
			return NULL;
		}
	}
	return root;
}
