#pragma once
#include "quota_model.h"
#include "cJSON.h"
#include <stddef.h>
int64_t quota_parse_timestamp(const cJSON *object, const char *key);
cJSON *quota_parse_document(const char *text, size_t length);
bool quota_parse_cline(const cJSON *root, quota_provider_t *out);
bool quota_parse_deepseek(const cJSON *root, quota_provider_t *out);
quota_status_t quota_parse_tokenhub(const cJSON *root, quota_provider_t *out);
quota_status_t quota_parse_codex(const cJSON *root, const char *collector,
                                const char *account, quota_provider_t *out);
