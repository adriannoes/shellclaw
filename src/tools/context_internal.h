/**
 * @file context_internal.h
 * @brief Shared state and cross-module helpers for get_context (not public API).
 */

#ifndef SHELLCLAW_TOOLS_CONTEXT_INTERNAL_H
#define SHELLCLAW_TOOLS_CONTEXT_INTERNAL_H

#include "core/config.h"
#include "cJSON.h"
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define CTX_URL_IP_API "https://ip-api.com/json/"
#define CTX_RESP_MAX   (96 * 1024)
#define CTX_CURL_TO_SEC 5L
#define CTX_WX_TTL_SEC  3600
#define CTX_HY_TTL_SEC  (86400 * 365)

extern pthread_mutex_t ctx_g_mx;
extern const config_t *ctx_g_cfg;

extern int ctx_g_geo_inited;
extern double ctx_g_lat;
extern double ctx_g_lon;
extern char ctx_g_city[96];
extern char ctx_g_country[96];
extern char ctx_g_cc[8];
extern char ctx_g_tz[64];
extern char ctx_g_src[24];

extern double ctx_g_ck_lat;
extern double ctx_g_ck_lon;
extern time_t ctx_g_wx_exp;
extern char *ctx_g_wx_store;

extern int ctx_g_cal_y_a;
extern int ctx_g_cal_y_b;
extern char ctx_g_cal_cc[8];
extern time_t ctx_g_h_exp;
extern char *ctx_g_h_store;

extern char *ctx_g_last_json;

time_t ctx_tnow(void);
void ctx_cc_up(char *s);
int ctx_dbl_eq(double x, double y);

int ctx_fetch_url(const char *url, long *http_code, char **body);

void ctx_geo_emit(cJSON *loc);
int ctx_geo_resolve(cJSON *loc);

int ctx_wx_fill(cJSON *wx_root, const char *ref_iso);
int ctx_holidays_fill(cJSON *hy_root, const char *d0, const char *d1, int ya, int yb,
                      const char *ccode);
void ctx_dashboard_fill(cJSON *root_out);
char *ctx_minimal_snapshot(void);

#ifdef SHELLCLAW_CONTEXT_TEST
typedef struct ctx_test_fake {
	const char *geo;
	const char *wx;
	const char *hy;
	time_t nw;
	int use_nw;
} ctx_test_fake_t;

extern ctx_test_fake_t ctx_g_fake;

void ctx_test_reset_fake(void);
#endif

#endif /* SHELLCLAW_TOOLS_CONTEXT_INTERNAL_H */
