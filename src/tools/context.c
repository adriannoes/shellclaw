/**
 * @file context.c
 * @brief get_context tool registry glue and exec_tool orchestration.
 */
#define _POSIX_C_SOURCE 200809L

#include "tools/context.h"
#include "tools/context_internal.h"
#include "tools/tool.h"
#include "cJSON.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SCHEMA                                                                         \
	"{\"type\":\"object\",\"additionalProperties\":true,"                               \
	"\"properties\":{\"location\":{\"type\":\"object\",\"description\":\"Geo or "       \
	"agent fallback\"},\"weather\":{\"type\":\"object\"},\"holidays\":{\"type\":"       \
	"\"object\"},\"dashboard\":{\"type\":\"object\",\"description\":\"UI-facing "        \
	"summary lines\"}},\"description\":\"Merged context JSON; slices may be partial "   \
	"when upstream APIs fail.\"}"

pthread_mutex_t ctx_g_mx = PTHREAD_MUTEX_INITIALIZER;
const config_t *ctx_g_cfg;

int ctx_g_geo_inited;
double ctx_g_lat;
double ctx_g_lon;
char ctx_g_city[96];
char ctx_g_country[96];
char ctx_g_cc[8];
char ctx_g_tz[64];
char ctx_g_src[24];

double ctx_g_ck_lat;
double ctx_g_ck_lon;
time_t ctx_g_wx_exp;
char *ctx_g_wx_store;

int ctx_g_cal_y_a;
int ctx_g_cal_y_b;
char ctx_g_cal_cc[8];
time_t ctx_g_h_exp;
char *ctx_g_h_store;

char *ctx_g_last_json;

#ifdef SHELLCLAW_CONTEXT_TEST
ctx_test_fake_t ctx_g_fake;

void ctx_test_reset_fake(void)
{
	memset(&ctx_g_fake, 0, sizeof(ctx_g_fake));
}
#endif

void tool_context_set_config(const config_t *cfg)
{
	pthread_mutex_lock(&ctx_g_mx);
	ctx_g_cfg = cfg;
	pthread_mutex_unlock(&ctx_g_mx);
}

static int exec_tool(const char *args_json, char *result_buf, size_t max_len)
{
	cJSON *root;
	cJSON *loc;
	cJSON *wx;
	cJSON *hy;
	struct tm lt;
	struct tm lt2;
	time_t nw;
	time_t nw2;
	char *serialized;
	size_t pn;
	int has_geo;
	(void)args_json;
	if (!result_buf || max_len == 0)
		return -1;
	root = cJSON_CreateObject();
	loc = cJSON_CreateObject();
	wx = cJSON_CreateObject();
	hy = cJSON_CreateObject();
	if (!root || !loc || !wx || !hy) {
		cJSON_Delete(root);
		cJSON_Delete(loc);
		cJSON_Delete(wx);
		cJSON_Delete(hy);
		snprintf(result_buf, max_len, "{\"error\":\"allocation failed\"}");
		return -1;
	}
	has_geo = ctx_geo_resolve(loc);
	cJSON_AddItemToObject(root, "location", loc);
	nw = ctx_tnow();
	if (!localtime_r(&nw, &lt)) {
		cJSON_AddBoolToObject(wx, "available", 0);
		cJSON_AddStringToObject(wx, "error", "time unavailable");
	} else if (!has_geo) {
		cJSON_AddBoolToObject(wx, "available", 0);
		cJSON_AddStringToObject(wx, "error", "no coordinates");
	} else {
		char day0[16];
		char day1[16];
		const char *hcode;
		int y_cal_a;
		int y_cal_b;
		strftime(day0, sizeof(day0), "%Y-%m-%d", &lt);
		nw2 = nw + (time_t)86400;
		if (!localtime_r(&nw2, &lt2))
			snprintf(day1, sizeof(day1), "%s", day0);
		else
			strftime(day1, sizeof(day1), "%Y-%m-%d", &lt2);
		(void)ctx_wx_fill(wx, day0);
		y_cal_a = lt.tm_year + 1900;
		y_cal_b = lt2.tm_year + 1900;
		pthread_mutex_lock(&ctx_g_mx);
		hcode = NULL;
		if (ctx_g_cc[0])
			hcode = ctx_g_cc;
		else if (ctx_g_cfg != NULL && config_agent_country_code(ctx_g_cfg) &&
		         config_agent_country_code(ctx_g_cfg)[0])
			hcode = config_agent_country_code(ctx_g_cfg);
		pthread_mutex_unlock(&ctx_g_mx);
		if (hcode != NULL && hcode[0])
			(void)ctx_holidays_fill(hy, day0, day1, y_cal_a, y_cal_b, hcode);
		else {
			cJSON_AddBoolToObject(hy, "available", 0);
			cJSON_AddStringToObject(hy, "error", "country code unavailable for holidays");
		}
	}
	cJSON_AddItemToObject(root, "weather", wx);
	cJSON_AddItemToObject(root, "holidays", hy);
	ctx_dashboard_fill(root);
	pthread_mutex_lock(&ctx_g_mx);
	serialized = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	root = NULL;
	free(ctx_g_last_json);
	ctx_g_last_json = serialized;
	pthread_mutex_unlock(&ctx_g_mx);
	if (ctx_g_last_json == NULL) {
		snprintf(result_buf, max_len, "{\"error\":\"serialization failed\"}");
		return -1;
	}
	pn = strlen(ctx_g_last_json);
	if (pn >= max_len) {
		snprintf(result_buf, max_len, "{\"error\":\"get_context payload too large for buffer\"}");
		return -1;
	}
	memcpy(result_buf, ctx_g_last_json, pn + 1u);
	return 0;
}

char *tool_context_snapshot_json(void)
{
	char *duplicate;
	pthread_mutex_lock(&ctx_g_mx);
	if (ctx_g_last_json != NULL)
		duplicate = strdup(ctx_g_last_json);
	else
		duplicate = NULL;
	pthread_mutex_unlock(&ctx_g_mx);
	return duplicate != NULL ? duplicate : ctx_minimal_snapshot();
}

static const tool_t CTX_TOOL = {
	.name = "get_context",
	.description = "Return JSON with location hints (ip-api/agent fallback), Open-Meteo 3-day daily "
	              "forecast, and public holidays via Nager.Date. Partial results when APIs fail; safe to "
	              "call anytime.",
	.parameters_json = SCHEMA,
	.execute = exec_tool,
};

const tool_t *tool_context_get(void)
{
	return &CTX_TOOL;
}

#ifdef SHELLCLAW_CONTEXT_TEST
void tool_context_test_reset(void)
{
	pthread_mutex_lock(&ctx_g_mx);
	ctx_test_reset_fake();
	ctx_g_cfg = NULL;
	ctx_g_geo_inited = 0;
	memset(ctx_g_city, 0, sizeof(ctx_g_city));
	memset(ctx_g_country, 0, sizeof(ctx_g_country));
	memset(ctx_g_cc, 0, sizeof(ctx_g_cc));
	memset(ctx_g_tz, 0, sizeof(ctx_g_tz));
	memset(ctx_g_src, 0, sizeof(ctx_g_src));
	ctx_g_ck_lat = ctx_g_ck_lon = 0.0;
	ctx_g_wx_exp = 0;
	free(ctx_g_wx_store);
	ctx_g_wx_store = NULL;
	ctx_g_cal_y_a = ctx_g_cal_y_b = 0;
	memset(ctx_g_cal_cc, 0, sizeof(ctx_g_cal_cc));
	ctx_g_h_exp = 0;
	free(ctx_g_h_store);
	ctx_g_h_store = NULL;
	free(ctx_g_last_json);
	ctx_g_last_json = NULL;
	pthread_mutex_unlock(&ctx_g_mx);
}

void tool_context_test_set_unix_time(time_t tt)
{
	pthread_mutex_lock(&ctx_g_mx);
	ctx_g_fake.use_nw = 1;
	ctx_g_fake.nw = tt;
	pthread_mutex_unlock(&ctx_g_mx);
}

void tool_context_test_set_http_bodies(const char *g, const char *w, const char *h)
{
	pthread_mutex_lock(&ctx_g_mx);
	ctx_g_fake.geo = g;
	ctx_g_fake.wx = w;
	ctx_g_fake.hy = h;
	pthread_mutex_unlock(&ctx_g_mx);
}

const char *tool_context_test_geo_url(void)
{
	return CTX_URL_IP_API;
}
#endif
