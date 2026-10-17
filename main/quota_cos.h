#pragma once
#include "quota_config.h"
#include <stddef.h>
typedef bool (*quota_cos_hash_fn)(const char *key, size_t key_len,
                                  const char *message, size_t length, char hex[41]);
/* key=NULL selects SHA1; otherwise HMAC-SHA1. Output is lowercase hex. */
bool quota_cos_sign_object(const quota_config_t *config, const char *collector, int64_t now,
                    quota_cos_hash_fn hash, char *url, size_t url_cap,
                    char *authorization, size_t authorization_cap);
bool quota_cos_sign_list(const quota_config_t *config, const char *marker, int64_t now,
                    quota_cos_hash_fn hash, char *url, size_t url_cap,
                    char *authorization, size_t authorization_cap);
#define QUOTA_COS_LIST_SIZE 16
bool quota_cos_sign_tokenhub(const quota_config_t *config, int64_t now,
                    quota_cos_hash_fn hash, char *url, size_t url_cap,
                    char *authorization, size_t authorization_cap);
