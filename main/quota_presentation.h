#pragma once
#include "quota_model.h"

typedef struct {
    char name[40], value[48], detail[160];
    bool progress, warning;
    int remaining;
} quota_metric_t;

typedef struct {
    char title[65], subtitle[100];
    int count;
    quota_metric_t rows[3];
} quota_presentation_t;

/* Formatting is independent of LVGL, including its no-float printf profile. */
void quota_money_label(const char *currency, const char *amount, char *out, unsigned size);
void quota_date_label(int64_t timestamp, char *out, unsigned size);
void quota_present_provider(const quota_provider_t *provider, int64_t now, quota_presentation_t *out);
