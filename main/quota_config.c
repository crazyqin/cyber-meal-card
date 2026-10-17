#include "quota_config.h"
#include <string.h>
#include <stdio.h>

/* Version 2 is the deployed ClinePass/DeepSeek configuration. Preserve its
 * Wi-Fi and API keys when upgrading only the app at the existing offset. */
typedef struct {
    uint32_t version, generation;
    char ssid[33], password[65], cline[256], deepseek[256];
} quota_config_v2_t;
static bool terminated(const quota_config_t *c) {
#define ENDS(member) memchr(c->member, 0, sizeof(c->member))
    if (!ENDS(ssid) || !ENDS(password) || !ENDS(cline) || !ENDS(deepseek) || !ENDS(cos_bucket) ||
        !ENDS(cos_region) || !ENDS(cos_prefix) || !ENDS(cos_secret_id) || !ENDS(cos_secret_key) || !ENDS(cos_token)) return false;
    for (int i = 0; i < QUOTA_LEGACY_CODEX_SLOTS; i++)
        if (!memchr(c->codex[i].collector, 0, sizeof(c->codex[i].collector)) ||
            !memchr(c->codex[i].account, 0, sizeof(c->codex[i].account))) return false;
#undef ENDS
    return true;
}
bool quota_config_load(const void *blob, size_t length, quota_config_t *out) {
    memset(out, 0, sizeof(*out));
    if (!blob || length < sizeof(uint32_t)) return false;
    uint32_t version;
    memcpy(&version, blob, sizeof(version));
    if (version == QUOTA_CONFIG_VERSION && length == sizeof(*out)) {
        memcpy(out, blob, length);
        cJSON empty = {.type = cJSON_Object};
        if (terminated(out) && quota_config_prepare(&empty, out, out)) return true;
        memset(out, 0, sizeof(*out));
        return false;
    }
    if (version != 2 || length != sizeof(quota_config_v2_t)) return false;
    quota_config_v2_t old;
    memcpy(&old, blob, sizeof(old));
    if (!memchr(old.ssid, 0, sizeof(old.ssid)) || !memchr(old.password, 0, sizeof(old.password)) ||
        !memchr(old.cline, 0, sizeof(old.cline)) || !memchr(old.deepseek, 0, sizeof(old.deepseek))) return false;
    out->version = QUOTA_CONFIG_VERSION;
    out->generation = old.generation;
    memcpy(out->ssid, old.ssid, sizeof(old.ssid));
    memcpy(out->password, old.password, sizeof(old.password));
    memcpy(out->cline, old.cline, sizeof(old.cline));
    memcpy(out->deepseek, old.deepseek, sizeof(old.deepseek));
    cJSON empty = {.type = cJSON_Object};
    if (quota_config_prepare(&empty, out, out)) return true;
    memset(out, 0, sizeof(*out));
    return false;
}
static bool component(const char *s, bool lower, bool path) {
    if (!*s || s[0] == '/' || !strcmp(s, ".") || strstr(s, "..") || strstr(s, "/./")) return false;
    for (const char *p = s; *p; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-') continue;
        if (!lower && ((*p >= 'A' && *p <= 'Z') || *p == '_' || *p == '.' || *p == '~')) continue;
        if (path && *p == '/' && p[1] && p[1] != '/') continue;
        return false;
    }
    return true;
}
bool quota_collector_valid(const char *collector) {
    return collector && strlen(collector) <= 64 && component(collector, false, false);
}
bool quota_cos_configured(const quota_config_t *c) {
    return quota_cos_missing(c) == 0;
}
unsigned quota_cos_missing(const quota_config_t *c) {
    return (!c->cos_bucket[0] ? 1u : 0) | (!c->cos_region[0] ? 2u : 0) |
        (!c->cos_secret_id[0] ? 4u : 0) | (!c->cos_secret_key[0] ? 8u : 0);
}
bool quota_config_apply_cos_defaults(quota_config_t *c, const quota_config_t *defaults) {
    if (quota_cos_configured(c) || !defaults || !quota_cos_configured(defaults)) return false;
#define COPY_COS(member) memcpy(c->member, defaults->member, sizeof(c->member))
    COPY_COS(cos_bucket); COPY_COS(cos_region); COPY_COS(cos_prefix);
    COPY_COS(cos_secret_id); COPY_COS(cos_secret_key); COPY_COS(cos_token);
#undef COPY_COS
    return true;
}
bool quota_config_apply_wifi_defaults(quota_config_t *c, const quota_config_t *defaults) {
    if (c->ssid[0] || !defaults || !defaults->ssid[0]) return false;
    quota_config_t candidate = *c;
    memcpy(candidate.ssid, defaults->ssid, sizeof(candidate.ssid));
    memcpy(candidate.password, defaults->password, sizeof(candidate.password));
    cJSON empty = {.type = cJSON_Object};
    if (!terminated(&candidate) || !quota_config_prepare(&empty, &candidate, &candidate)) return false;
    *c = candidate;
    return true;
}
cJSON *quota_config_export(const quota_config_t *c) {
    cJSON *out = cJSON_CreateObject();
    if (!out) return NULL;
    bool ok = cJSON_AddStringToObject(out, "app", "ai-passport") &&
        cJSON_AddNumberToObject(out, "backup_schema", 1);
#define EXPORT(member) ok = ok && cJSON_AddStringToObject(out, #member, c->member)
    EXPORT(ssid); EXPORT(password); EXPORT(cline); EXPORT(deepseek);
    EXPORT(cos_bucket); EXPORT(cos_region); EXPORT(cos_prefix);
    EXPORT(cos_secret_id); EXPORT(cos_secret_key); EXPORT(cos_token);
#undef EXPORT
    if (!ok) { cJSON_Delete(out); return NULL; }
    return out;
}

static bool field(const cJSON *o, const char *key, char *out, size_t cap, bool required) {
    const cJSON *s = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!s) return !required;
    if (!cJSON_IsString(s) || strlen(s->valuestring) >= cap) return false;
    for (const unsigned char *c = (const unsigned char *)s->valuestring; *c; c++)
        if (*c < 32 || *c == 127) return false;
    memcpy(out, s->valuestring, strlen(s->valuestring) + 1);
    return true;
}
bool quota_config_prepare(const cJSON *json, const quota_config_t *current, quota_config_t *candidate) {
    if (!cJSON_IsObject(json)) return false;
    *candidate = *current;
    bool ok = field(json, "ssid", candidate->ssid, sizeof(candidate->ssid), false) &&
        field(json, "password", candidate->password, sizeof(candidate->password), false) &&
        field(json, "cline", candidate->cline, sizeof(candidate->cline), false) &&
        field(json, "deepseek", candidate->deepseek, sizeof(candidate->deepseek), false) &&
        field(json, "cos_bucket", candidate->cos_bucket, sizeof(candidate->cos_bucket), false) &&
        field(json, "cos_region", candidate->cos_region, sizeof(candidate->cos_region), false) &&
        field(json, "cos_prefix", candidate->cos_prefix, sizeof(candidate->cos_prefix), false) &&
        field(json, "cos_secret_id", candidate->cos_secret_id, sizeof(candidate->cos_secret_id), false) &&
        field(json, "cos_secret_key", candidate->cos_secret_key, sizeof(candidate->cos_secret_key), false) &&
        field(json, "cos_token", candidate->cos_token, sizeof(candidate->cos_token), false);
    if ((candidate->cos_bucket[0] && !component(candidate->cos_bucket, true, false)) ||
        (candidate->cos_region[0] && !component(candidate->cos_region, true, false)) ||
        (candidate->cos_prefix[0] && !component(candidate->cos_prefix, false, true))) ok = false;
    for (const char *p = candidate->cos_secret_id; *p; p++)
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9'))) ok = false;
    if (!ok || !candidate->ssid[0]) return false;
    size_t pass_len = strlen(candidate->password);
    if (pass_len && pass_len < 8) return false;
    if (pass_len == 64) {
        for (unsigned i = 0; i < 64; i++) {
            char c = candidate->password[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
        }
    }
    candidate->version = QUOTA_CONFIG_VERSION;
    return true;
}
