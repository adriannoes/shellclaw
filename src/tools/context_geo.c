/**
 * @file context_geo.c
 * @brief Geolocation (ip-api) and agent config fallback for get_context.
 */
#define _POSIX_C_SOURCE 200809L

#include "tools/context_internal.h"
#include "core/config.h"
#include "cJSON.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ctx_geo_emit(cJSON *loc)
{
	cJSON_DeleteItemFromObject(loc, "available");
	cJSON_DeleteItemFromObject(loc, "source");
	cJSON_DeleteItemFromObject(loc, "latitude");
	cJSON_DeleteItemFromObject(loc, "longitude");
	cJSON_DeleteItemFromObject(loc, "city");
	cJSON_DeleteItemFromObject(loc, "country");
	cJSON_DeleteItemFromObject(loc, "country_code");
	cJSON_DeleteItemFromObject(loc, "timezone");
	cJSON_DeleteItemFromObject(loc, "error");
	if (ctx_g_geo_inited != 0 && strcmp(ctx_g_src, "none") != 0) {
		cJSON_AddBoolToObject(loc, "available", 1);
		cJSON_AddStringToObject(loc, "source", ctx_g_src);
		cJSON_AddNumberToObject(loc, "latitude", ctx_g_lat);
		cJSON_AddNumberToObject(loc, "longitude", ctx_g_lon);
		if (ctx_g_city[0])
			cJSON_AddStringToObject(loc, "city", ctx_g_city);
		if (ctx_g_country[0])
			cJSON_AddStringToObject(loc, "country", ctx_g_country);
		if (ctx_g_cc[0])
			cJSON_AddStringToObject(loc, "country_code", ctx_g_cc);
		if (ctx_g_tz[0])
			cJSON_AddStringToObject(loc, "timezone", ctx_g_tz);
	} else {
		cJSON_AddBoolToObject(loc, "available", 0);
		cJSON_AddStringToObject(loc, "error", "geolocation unavailable");
	}
}

static void geo_cfg_fb(void)
{
	const char *c;
	strncpy(ctx_g_src, "agent_fallback", sizeof(ctx_g_src) - 1);
	ctx_g_src[sizeof(ctx_g_src) - 1] = '\0';
	ctx_g_lat = config_agent_latitude(ctx_g_cfg);
	ctx_g_lon = config_agent_longitude(ctx_g_cfg);
	c = config_agent_country_code(ctx_g_cfg);
	memset(ctx_g_city, 0, sizeof(ctx_g_city));
	memset(ctx_g_country, 0, sizeof(ctx_g_country));
	memset(ctx_g_tz, 0, sizeof(ctx_g_tz));
	if (c != NULL && c[0]) {
		snprintf(ctx_g_cc, sizeof(ctx_g_cc), "%s", c);
		ctx_cc_up(ctx_g_cc);
	} else {
		ctx_g_cc[0] = '\0';
	}
	ctx_g_geo_inited = 1;
}

int ctx_geo_resolve(cJSON *loc)
{
	long hc;
	char *raw;
	cJSON *root;
	cJSON *j;
	const char *st;
	double lat;
	double lon;
	char city[sizeof(ctx_g_city)];
	char country[sizeof(ctx_g_country)];
	char cc[sizeof(ctx_g_cc)];
	char tz[sizeof(ctx_g_tz)];
	int ok;
	if (!loc)
		return -1;
	pthread_mutex_lock(&ctx_g_mx);
	if (ctx_g_geo_inited != 0) {
		ctx_geo_emit(loc);
		ok = strcmp(ctx_g_src, "none") != 0;
		pthread_mutex_unlock(&ctx_g_mx);
		return ok;
	}
	pthread_mutex_unlock(&ctx_g_mx);
	if (ctx_fetch_url(CTX_URL_IP_API, &hc, &raw) != 0 || raw == NULL || hc != 200L) {
		free(raw);
		pthread_mutex_lock(&ctx_g_mx);
		if (ctx_g_cfg != NULL && config_agent_has_latitude(ctx_g_cfg) &&
		    config_agent_has_longitude(ctx_g_cfg)) {
			geo_cfg_fb();
			ctx_geo_emit(loc);
			ok = 1;
		} else {
			strncpy(ctx_g_src, "none", sizeof(ctx_g_src) - 1);
			ctx_g_geo_inited = 1;
			ctx_geo_emit(loc);
			ok = 0;
		}
		pthread_mutex_unlock(&ctx_g_mx);
		return ok;
	}
	root = cJSON_Parse(raw);
	free(raw);
	if (root == NULL) {
		pthread_mutex_lock(&ctx_g_mx);
		strncpy(ctx_g_src, "none", sizeof(ctx_g_src) - 1);
		ctx_g_geo_inited = 1;
		ctx_geo_emit(loc);
		pthread_mutex_unlock(&ctx_g_mx);
		return 0;
	}
	st = "fail";
	j = cJSON_GetObjectItem(root, "status");
	if (cJSON_IsString(j) && j->valuestring != NULL)
		st = j->valuestring;
	if (strcmp(st, "success") != 0) {
		cJSON_Delete(root);
		pthread_mutex_lock(&ctx_g_mx);
		if (ctx_g_cfg != NULL && config_agent_has_latitude(ctx_g_cfg) &&
		    config_agent_has_longitude(ctx_g_cfg)) {
			geo_cfg_fb();
			ctx_geo_emit(loc);
			ok = 1;
		} else {
			strncpy(ctx_g_src, "none", sizeof(ctx_g_src) - 1);
			ctx_g_geo_inited = 1;
			ctx_geo_emit(loc);
			ok = 0;
		}
		pthread_mutex_unlock(&ctx_g_mx);
		return ok;
	}
	j = cJSON_GetObjectItem(root, "lat");
	if (!cJSON_IsNumber(j)) {
		cJSON_Delete(root);
		pthread_mutex_lock(&ctx_g_mx);
		if (ctx_g_cfg != NULL && config_agent_has_latitude(ctx_g_cfg) &&
		    config_agent_has_longitude(ctx_g_cfg)) {
			geo_cfg_fb();
			ctx_geo_emit(loc);
			ok = 1;
		} else {
			strncpy(ctx_g_src, "none", sizeof(ctx_g_src) - 1);
			ctx_g_geo_inited = 1;
			ctx_geo_emit(loc);
			ok = 0;
		}
		pthread_mutex_unlock(&ctx_g_mx);
		return ok;
	}
	lat = j->valuedouble;
	j = cJSON_GetObjectItem(root, "lon");
	if (!cJSON_IsNumber(j)) {
		cJSON_Delete(root);
		pthread_mutex_lock(&ctx_g_mx);
		if (ctx_g_cfg != NULL && config_agent_has_latitude(ctx_g_cfg) &&
		    config_agent_has_longitude(ctx_g_cfg)) {
			geo_cfg_fb();
			ctx_geo_emit(loc);
			ok = 1;
		} else {
			strncpy(ctx_g_src, "none", sizeof(ctx_g_src) - 1);
			ctx_g_geo_inited = 1;
			ctx_geo_emit(loc);
			ok = 0;
		}
		pthread_mutex_unlock(&ctx_g_mx);
		return ok;
	}
	lon = j->valuedouble;
	j = cJSON_GetObjectItem(root, "city");
	snprintf(city, sizeof(city), "%s",
	         cJSON_IsString(j) && j->valuestring != NULL ? j->valuestring : "");
	j = cJSON_GetObjectItem(root, "country");
	snprintf(country, sizeof(country), "%s",
	         cJSON_IsString(j) && j->valuestring != NULL ? j->valuestring : "");
	j = cJSON_GetObjectItem(root, "countryCode");
	snprintf(cc, sizeof(cc), "%s",
	         cJSON_IsString(j) && j->valuestring != NULL ? j->valuestring : "");
	ctx_cc_up(cc);
	j = cJSON_GetObjectItem(root, "timezone");
	snprintf(tz, sizeof(tz), "%s",
	         cJSON_IsString(j) && j->valuestring != NULL ? j->valuestring : "");
	cJSON_Delete(root);
	pthread_mutex_lock(&ctx_g_mx);
	ctx_g_lat = lat;
	ctx_g_lon = lon;
	snprintf(ctx_g_city, sizeof(ctx_g_city), "%s", city);
	snprintf(ctx_g_country, sizeof(ctx_g_country), "%s", country);
	snprintf(ctx_g_cc, sizeof(ctx_g_cc), "%s", cc);
	snprintf(ctx_g_tz, sizeof(ctx_g_tz), "%s", tz);
	strncpy(ctx_g_src, "ip-api", sizeof(ctx_g_src) - 1);
	ctx_g_src[sizeof(ctx_g_src) - 1] = '\0';
	ctx_g_geo_inited = 1;
	ctx_geo_emit(loc);
	pthread_mutex_unlock(&ctx_g_mx);
	return 1;
}
