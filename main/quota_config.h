#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"
#include <stddef.h>
#define QUOTA_CONFIG_VERSION 3
#define QUOTA_LEGACY_CODEX_SLOTS 2
typedef struct {
    char collector[65], account[65];
} quota_codex_source_t;
typedef struct {
    uint32_t version, generation;
    char ssid[33], password[65];
    char cline[256], deepseek[256];
    char cos_bucket[96], cos_region[48], cos_prefix[129];
    char cos_secret_id[128], cos_secret_key[128], cos_token[2048];
    /* Version-3 NVS compatibility only; discovery ignores old selectors. */
    quota_codex_source_t codex[QUOTA_LEGACY_CODEX_SLOTS];
} quota_config_t;
bool quota_config_prepare(const cJSON *json, const quota_config_t *current, quota_config_t *candidate);
bool quota_config_load(const void *blob, size_t length, quota_config_t *out);
bool quota_cos_configured(const quota_config_t *config);
bool quota_collector_valid(const char *collector);
unsigned quota_cos_missing(const quota_config_t *config);
bool quota_config_apply_cos_defaults(quota_config_t *config, const quota_config_t *defaults);
bool quota_config_apply_wifi_defaults(quota_config_t *config, const quota_config_t *defaults);
cJSON *quota_config_export(const quota_config_t *config);
