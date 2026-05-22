/**
 * @file context_cache.c
 * @brief Weather, holidays, and dashboard cache for get_context.
 */
#define _POSIX_C_SOURCE 200809L

#include "tools/context_internal.h"
#include "core/config.h"
#include "cJSON.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

time_t ctx_tnow(void)
{
#ifdef SHELLCLAW_CONTEXT_TEST
	if (ctx_g_fake.use_nw)
		return ctx_g_fake.nw;
#endif
	return time(NULL);
}

void ctx_cc_up(char *s)
{
	while (s && *s) {
		if (*s >= 'a' && *s <= 'z')
			*s = (char)(*s - ' ');
		++s;
	}
}

int ctx_dbl_eq(double x, double y)
{
	return fabs(x - y) < 1e-5;
}

static cJSON *hol_merge_arrays(cJSON *a, cJSON *b)
{
	int i;
	int cnt;
	cJSON *out;
	out = cJSON_CreateArray();
	if (out == NULL)
		return NULL;
	if (cJSON_IsArray(a)) {
		cnt = (int)cJSON_GetArraySize(a);
		for (i = 0; i < cnt; i++) {
			cJSON *x = cJSON_GetArrayItem(a, i);
			cJSON *cp = x != NULL ? cJSON_Duplicate(x, 1) : NULL;
			if (cp == NULL)
				goto fail;
			cJSON_AddItemToArray(out, cp);
		}
	}
	if (cJSON_IsArray(b)) {
		cnt = (int)cJSON_GetArraySize(b);
		for (i = 0; i < cnt; i++) {
			cJSON *x = cJSON_GetArrayItem(b, i);
			cJSON *cp = x != NULL ? cJSON_Duplicate(x, 1) : NULL;
			if (cp == NULL)
				goto fail;
			cJSON_AddItemToArray(out, cp);
		}
	}
	return out;
fail:
	cJSON_Delete(out);
	return NULL;
}

static void hol_mark(cJSON *dst, const cJSON *items, const char *d_today, const char *d_tomo)
{
	int i;
	const char *closest = NULL;
	const char *cname = "";
	int today_flag = 0;
	int tomo_flag = 0;
	if (!cJSON_IsArray(items))
		return;
	for (i = 0; i < (int)cJSON_GetArraySize(items); i++) {
		cJSON *entry = cJSON_GetArrayItem(items, i);
		cJSON *dt;
		cJSON *nm;
		const char *ds;
		const char *ns;
		if (!cJSON_IsObject(entry))
			continue;
		dt = cJSON_GetObjectItem(entry, "date");
		nm = cJSON_GetObjectItem(entry, "localName");
		if (!cJSON_IsString(dt) || dt->valuestring == NULL ||
		    strlen(dt->valuestring) != (size_t)10)
			continue;
		ds = dt->valuestring;
		ns = (cJSON_IsString(nm) && nm->valuestring != NULL) ? nm->valuestring : "";
		if (strcmp(ds, d_today) == 0)
			today_flag = 1;
		if (strcmp(ds, d_tomo) == 0)
			tomo_flag = 1;
		if (strcmp(ds, d_today) >= 0 && (closest == NULL || strcmp(ds, closest) < 0)) {
			closest = ds;
			cname = ns;
		}
	}
	cJSON_AddBoolToObject(dst, "available", 1);
	cJSON_AddBoolToObject(dst, "is_public_holiday_today", today_flag ? 1 : 0);
	cJSON_AddBoolToObject(dst, "is_public_holiday_tomorrow", tomo_flag ? 1 : 0);
	if (closest != NULL) {
		cJSON_AddStringToObject(dst, "next_public_holiday_date", closest);
		cJSON_AddStringToObject(dst, "next_public_holiday_name", cname != NULL ? cname : "");
	}
}

static void wx_day_line(char *buf, size_t cap, const cJSON *daily, const char *day_iso)
{
	cJSON *time_arr;
	int n;
	int i;
	int ix = -1;
	cJSON *tmax_arr;
	cJSON *tmin_arr;
	cJSON *pr_arr;
	cJSON *wc_arr;
	double vmax = 0.0;
	double vmin = 0.0;
	double pr_mm = 0.0;
	long wcode = -1;
	if (buf == NULL || cap == 0 || daily == NULL || day_iso == NULL)
		return;
	buf[0] = '\0';
	time_arr = cJSON_GetObjectItem(daily, "time");
	tmax_arr = cJSON_GetObjectItem(daily, "temperature_2m_max");
	tmin_arr = cJSON_GetObjectItem(daily, "temperature_2m_min");
	pr_arr = cJSON_GetObjectItem(daily, "precipitation_sum");
	wc_arr = cJSON_GetObjectItem(daily, "weathercode");
	if (!cJSON_IsArray(time_arr))
		return;
	n = (int)cJSON_GetArraySize(time_arr);
	for (i = 0; i < n; i++) {
		cJSON *t = cJSON_GetArrayItem(time_arr, i);
		if (!cJSON_IsString(t) || t->valuestring == NULL)
			continue;
		if (strcmp(t->valuestring, day_iso) == 0) {
			ix = i;
			break;
		}
	}
	if (ix < 0)
		return;
	if (cJSON_IsArray(tmax_arr) && ix < (int)cJSON_GetArraySize(tmax_arr) &&
	    cJSON_IsNumber(cJSON_GetArrayItem(tmax_arr, ix)))
		vmax = cJSON_GetArrayItem(tmax_arr, ix)->valuedouble;
	if (cJSON_IsArray(tmin_arr) && ix < (int)cJSON_GetArraySize(tmin_arr) &&
	    cJSON_IsNumber(cJSON_GetArrayItem(tmin_arr, ix)))
		vmin = cJSON_GetArrayItem(tmin_arr, ix)->valuedouble;
	if (cJSON_IsArray(pr_arr) && ix < (int)cJSON_GetArraySize(pr_arr) &&
	    cJSON_IsNumber(cJSON_GetArrayItem(pr_arr, ix)))
		pr_mm = cJSON_GetArrayItem(pr_arr, ix)->valuedouble;
	if (cJSON_IsArray(wc_arr) && ix < (int)cJSON_GetArraySize(wc_arr) &&
	    cJSON_IsNumber(cJSON_GetArrayItem(wc_arr, ix)))
		wcode = (long)cJSON_GetArrayItem(wc_arr, ix)->valuedouble;
	snprintf(buf, cap, "%s max %.1fC min %.1fC precip %.2f mm wcode %ld", day_iso, vmax,
	         vmin, pr_mm, wcode);
}

int ctx_wx_fill(cJSON *wx_root, const char *ref_iso)
{
	char url_buf[448];
	time_t nw;
	long hc = 0;
	char *rsp = NULL;
	cJSON *doc;
	cJSON *daily_blk;
	cJSON *copy;
	double lat;
	double lon;
	pthread_mutex_lock(&ctx_g_mx);
	lat = ctx_g_lat;
	lon = ctx_g_lon;
	nw = ctx_tnow();
	if (ctx_g_wx_store != NULL && nw < ctx_g_wx_exp && ctx_dbl_eq(ctx_g_ck_lat, lat) &&
	    ctx_dbl_eq(ctx_g_ck_lon, lon)) {
		int cache_used;
		doc = cJSON_Parse(ctx_g_wx_store);
		daily_blk = (doc != NULL) ? cJSON_GetObjectItem(doc, "daily") : NULL;
		cache_used = (daily_blk != NULL && cJSON_IsObject(daily_blk)) ? 1 : 0;
		if (cache_used != 0) {
			cJSON_AddBoolToObject(wx_root, "available", 1);
			copy = cJSON_Duplicate(daily_blk, 1);
			cJSON_DeleteItemFromObject(wx_root, "daily");
			if (copy != NULL)
				cJSON_AddItemToObject(wx_root, "daily", copy);
			cJSON_DeleteItemFromObject(wx_root, "reference_day");
			cJSON_AddStringToObject(wx_root, "reference_day", ref_iso);
			cJSON_AddNumberToObject(wx_root, "cached_until", (double)ctx_g_wx_exp);
		}
		if (doc != NULL)
			cJSON_Delete(doc);
		if (cache_used != 0) {
			pthread_mutex_unlock(&ctx_g_mx);
			return 0;
		}
	}
	pthread_mutex_unlock(&ctx_g_mx);
	snprintf(url_buf, sizeof(url_buf),
	         "https://api.open-meteo.com/v1/forecast?"
	         "latitude=%.6f&longitude=%.6f"
	         "&daily=temperature_2m_max,temperature_2m_min,precipitation_sum,weathercode"
	         "&timezone=auto&forecast_days=3",
	         lat, lon);
	if (ctx_fetch_url(url_buf, &hc, &rsp) != 0 || rsp == NULL || hc != 200L) {
		free(rsp);
		cJSON_AddBoolToObject(wx_root, "available", 0);
		cJSON_AddStringToObject(wx_root, "error", "open-meteo unavailable");
		return -1;
	}
	doc = cJSON_Parse(rsp);
	free(rsp);
	daily_blk = (doc != NULL) ? cJSON_GetObjectItem(doc, "daily") : NULL;
	if (!cJSON_IsObject(daily_blk)) {
		cJSON_Delete(doc);
		cJSON_AddBoolToObject(wx_root, "available", 0);
		cJSON_AddStringToObject(wx_root, "error", "open-meteo parse failure");
		return -1;
	}
	pthread_mutex_lock(&ctx_g_mx);
	free(ctx_g_wx_store);
	ctx_g_wx_store = doc != NULL ? cJSON_PrintUnformatted(doc) : NULL;
	nw = ctx_tnow();
	ctx_g_wx_exp = nw + (time_t)CTX_WX_TTL_SEC;
	ctx_g_ck_lat = lat;
	ctx_g_ck_lon = lon;
	if (doc != NULL)
		cJSON_Delete(doc);
	doc = ctx_g_wx_store != NULL ? cJSON_Parse(ctx_g_wx_store) : NULL;
	daily_blk = doc != NULL ? cJSON_GetObjectItem(doc, "daily") : NULL;
	cJSON_AddBoolToObject(wx_root, "available", 1);
	copy = daily_blk != NULL ? cJSON_Duplicate(daily_blk, 1) : NULL;
	cJSON_DeleteItemFromObject(wx_root, "daily");
	if (copy != NULL)
		cJSON_AddItemToObject(wx_root, "daily", copy);
	cJSON_AddStringToObject(wx_root, "reference_day", ref_iso);
	cJSON_AddNumberToObject(wx_root, "cached_until", (double)ctx_g_wx_exp);
	if (doc != NULL)
		cJSON_Delete(doc);
	pthread_mutex_unlock(&ctx_g_mx);
	return (copy != NULL) ? 0 : -1;
}

int ctx_holidays_fill(cJSON *hy_root, const char *d0, const char *d1, int ya, int yb,
                      const char *ccode)
{
	char u[144];
	time_t nw;
	long hc = 0;
	char *rsp;
	cJSON *a1 = NULL;
	cJSON *a2 = NULL;
	cJSON *merged;
	char *print_m;
	char cc_norm[sizeof(ctx_g_cal_cc)];
	if (!ccode || ccode[0] == '\0') {
		cJSON_AddBoolToObject(hy_root, "available", 0);
		cJSON_AddStringToObject(hy_root, "error", "missing country code for holidays");
		return -1;
	}
	snprintf(cc_norm, sizeof(cc_norm), "%s", ccode);
	ctx_cc_up(cc_norm);
	pthread_mutex_lock(&ctx_g_mx);
	nw = ctx_tnow();
	if (ctx_g_h_store != NULL && nw < ctx_g_h_exp && strcmp(ctx_g_cal_cc, cc_norm) == 0 &&
	    ctx_g_cal_y_a == ya && ctx_g_cal_y_b == yb) {
		merged = cJSON_Parse(ctx_g_h_store);
		hol_mark(hy_root, merged, d0, d1);
		cJSON_AddNumberToObject(hy_root, "cached_until", (double)ctx_g_h_exp);
		cJSON_Delete(merged);
		pthread_mutex_unlock(&ctx_g_mx);
		return 0;
	}
	pthread_mutex_unlock(&ctx_g_mx);
	snprintf(u, sizeof(u), "https://date.nager.at/api/v3/PublicHolidays/%d/%s", ya, cc_norm);
	rsp = NULL;
	if (ctx_fetch_url(u, &hc, &rsp) != 0 || rsp == NULL || hc != 200L) {
		free(rsp);
		cJSON_AddBoolToObject(hy_root, "available", 0);
		cJSON_AddStringToObject(hy_root, "error", "nager fetch failed");
		return -1;
	}
	a1 = cJSON_Parse(rsp);
	free(rsp);
	if (!cJSON_IsArray(a1)) {
		cJSON_Delete(a1);
		cJSON_AddBoolToObject(hy_root, "available", 0);
		cJSON_AddStringToObject(hy_root, "error", "nager parse failed");
		return -1;
	}
	a2 = NULL;
	if (yb != ya) {
		snprintf(u, sizeof(u), "https://date.nager.at/api/v3/PublicHolidays/%d/%s", yb,
		         cc_norm);
		if (ctx_fetch_url(u, &hc, &rsp) == 0 && rsp != NULL && hc == 200L) {
			a2 = cJSON_Parse(rsp);
			free(rsp);
			rsp = NULL;
		} else {
			free(rsp);
			rsp = NULL;
		}
	}
	merged = hol_merge_arrays(a1, (a2 != NULL && cJSON_IsArray(a2)) ? a2 : NULL);
	cJSON_Delete(a1);
	if (a2 != NULL)
		cJSON_Delete(a2);
	if (merged == NULL) {
		cJSON_AddBoolToObject(hy_root, "available", 0);
		cJSON_AddStringToObject(hy_root, "error", "holiday merge failed");
		return -1;
	}
	print_m = cJSON_PrintUnformatted(merged);
	pthread_mutex_lock(&ctx_g_mx);
	free(ctx_g_h_store);
	ctx_g_h_store = print_m != NULL ? strdup(print_m) : NULL;
	free(print_m);
	nw = ctx_tnow();
	ctx_g_h_exp = nw + (time_t)CTX_HY_TTL_SEC;
	snprintf(ctx_g_cal_cc, sizeof(ctx_g_cal_cc), "%s", cc_norm);
	ctx_g_cal_y_a = ya;
	ctx_g_cal_y_b = yb;
	hol_mark(hy_root, merged, d0, d1);
	cJSON_AddNumberToObject(hy_root, "cached_until", (double)ctx_g_h_exp);
	pthread_mutex_unlock(&ctx_g_mx);
	cJSON_Delete(merged);
	return 0;
}

void ctx_dashboard_fill(cJSON *root_out)
{
	cJSON *ds;
	char line_wx[256];
	char tmp_loc[208];
	char hol_sum[208];
	char *ref_iso;
	cJSON *wobj;
	cJSON *dlay;
	pthread_mutex_lock(&ctx_g_mx);
	ds = cJSON_CreateObject();
	if (ds == NULL) {
		pthread_mutex_unlock(&ctx_g_mx);
		return;
	}
	line_wx[0] = '\0';
	tmp_loc[0] = '\0';
	hol_sum[0] = '\0';
	if (ctx_g_geo_inited != 0 && strcmp(ctx_g_src, "none") != 0 && ctx_g_city[0])
		snprintf(tmp_loc, sizeof(tmp_loc), "%s, %s", ctx_g_city,
		         ctx_g_cc[0] != '\0' ? ctx_g_cc : ctx_g_country);
	else if (ctx_g_geo_inited != 0 && strcmp(ctx_g_src, "none") != 0)
		snprintf(tmp_loc, sizeof(tmp_loc), "%.4f %.4f", ctx_g_lat, ctx_g_lon);
	wobj = cJSON_GetObjectItem(root_out, "weather");
	ref_iso = (wobj != NULL && cJSON_IsString(cJSON_GetObjectItem(wobj, "reference_day")) &&
	           cJSON_GetObjectItem(wobj, "reference_day")->valuestring != NULL)
	              ? cJSON_GetObjectItem(wobj, "reference_day")->valuestring
	              : "";
	dlay = (wobj != NULL) ? cJSON_GetObjectItem(wobj, "daily") : NULL;
	if (wobj != NULL && cJSON_GetObjectItem(wobj, "available") &&
	    (cJSON_IsTrue(cJSON_GetObjectItem(wobj, "available")) ||
	     cJSON_IsBool(cJSON_GetObjectItem(wobj, "available"))))
		wx_day_line(line_wx, sizeof(line_wx), cJSON_IsObject(dlay) ? dlay : NULL,
		            ref_iso != NULL ? ref_iso : "");
	if (strlen(line_wx) > (size_t)0)
		cJSON_AddStringToObject(ds, "weather_line", line_wx);
	if (tmp_loc[0])
		cJSON_AddStringToObject(ds, "location_line", tmp_loc);
	{
		const char *nxd;
		const char *nxnm;
		cJSON *hh = cJSON_GetObjectItem(root_out, "holidays");
		nxd = (hh != NULL &&
		       cJSON_IsString(cJSON_GetObjectItem(hh, "next_public_holiday_date")) &&
		       cJSON_GetObjectItem(hh, "next_public_holiday_date")->valuestring != NULL)
		          ? cJSON_GetObjectItem(hh, "next_public_holiday_date")->valuestring
		          : "";
		nxnm = (hh != NULL &&
		        cJSON_IsString(cJSON_GetObjectItem(hh, "next_public_holiday_name")) &&
		        cJSON_GetObjectItem(hh, "next_public_holiday_name")->valuestring != NULL)
		           ? cJSON_GetObjectItem(hh, "next_public_holiday_name")->valuestring
		           : "";
		if (hh != NULL && cJSON_GetObjectItem(hh, "available") &&
		    (cJSON_IsTrue(cJSON_GetObjectItem(hh, "available")) ||
		     cJSON_IsBool(cJSON_GetObjectItem(hh, "available"))) &&
		    strcmp(nxd, "") != 0) {
			snprintf(hol_sum, sizeof(hol_sum), "next %s on %s", nxnm != NULL ? nxnm : "",
			         nxd);
			cJSON_AddStringToObject(ds, "holiday_line", hol_sum);
		}
	}
	cJSON_DeleteItemFromObject(root_out, "dashboard");
	cJSON_AddItemToObject(root_out, "dashboard", ds);
	pthread_mutex_unlock(&ctx_g_mx);
}

char *ctx_minimal_snapshot(void)
{
	cJSON *r;
	cJSON *dash;
	char loc_hint[208];
	pthread_mutex_lock(&ctx_g_mx);
	r = cJSON_CreateObject();
	dash = cJSON_CreateObject();
	if (!r || !dash) {
		cJSON_Delete(r);
		cJSON_Delete(dash);
		pthread_mutex_unlock(&ctx_g_mx);
		return strdup("{}");
	}
	loc_hint[0] = '\0';
	if (ctx_g_cfg != NULL && config_agent_has_latitude(ctx_g_cfg) &&
	    config_agent_has_longitude(ctx_g_cfg)) {
		snprintf(loc_hint, sizeof(loc_hint), "agent fallback coords %.5f %.5f",
		         config_agent_latitude(ctx_g_cfg), config_agent_longitude(ctx_g_cfg));
		if (config_agent_country_code(ctx_g_cfg) && config_agent_country_code(ctx_g_cfg)[0]) {
			char ccbuf[40];
			snprintf(ccbuf, sizeof(ccbuf), " (%s)", config_agent_country_code(ctx_g_cfg));
			strncat(loc_hint, ccbuf, sizeof(loc_hint) - strlen(loc_hint) - 1u);
		}
		cJSON_AddStringToObject(dash, "location_hint", loc_hint);
	}
	cJSON_AddStringToObject(dash, "note",
	                        "Dashboard cache empty - invoke get_context after startup for live enrichment.");
	cJSON_AddItemToObject(r, "dashboard", dash);
	pthread_mutex_unlock(&ctx_g_mx);
	{
		char *sprint = cJSON_PrintUnformatted(r);
		char *fallback;
		cJSON_Delete(r);
		if (!sprint)
			return strdup("{}");
		fallback = strdup(sprint);
		free(sprint);
		if (!fallback)
			return strdup("{}");
		return fallback;
	}
}
