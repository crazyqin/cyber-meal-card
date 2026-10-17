#include "quota_collection.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const cJSON *get(const cJSON *o, const char *key) { return cJSON_GetObjectItemCaseSensitive(o, key); }
static bool text(const cJSON *o, const char *key, char *out, size_t cap) {
    const cJSON *s = get(o, key);
    if (!cJSON_IsString(s) || !s->valuestring[0] || strlen(s->valuestring) >= cap) return false;
    for (const unsigned char *p = (const unsigned char *)s->valuestring; *p; p++)
        if (*p < 32 || *p > 126) return false;
    snprintf(out, cap, "%s", s->valuestring);
    return true;
}
static bool numeric(const cJSON *o, const char *key, double *out) {
    const cJSON *n = get(o, key);
    if (!cJSON_IsNumber(n) || !isfinite(n->valuedouble)) return false;
    *out = n->valuedouble;
    return true;
}
static bool timestamp(const cJSON *o, const char *key, int64_t *out) {
    const cJSON *n = get(o, key);
    *out = 0;
    if (!n || cJSON_IsNull(n)) return true;
    double value;
    if (!numeric(o, key, &value) || value < 0 || value > 4102444799.0 || floor(value) != value) return false;
    *out = (int64_t)value;
    return true;
}
static bool money(const cJSON *o, const char *key, char *out, size_t cap) {
    if (!text(o, key, out, cap)) return false;
    unsigned dots = 0, digits = 0;
    for (const char *p = out; *p; p++) {
        if (*p == '.' && !dots++) continue;
        if (*p < '0' || *p > '9') return false;
        digits++;
    }
    return digits > 0;
}
static bool parse_row(const cJSON *row, int64_t report_time, quota_provider_t *p) {
    if (!text(row, "provider", p->source, sizeof(p->source)) || !quota_collector_valid(p->source) ||
        !text(row, "name", p->account, sizeof(p->account)) ||
        !text(row, "account_id", p->account_id, sizeof(p->account_id)) || !quota_collector_valid(p->account_id)) return false;
    p->kind = !strcmp(p->source, "clinepass") ? QUOTA_CLINE : !strcmp(p->source, "deepseek") ? QUOTA_DEEPSEEK :
              !strcmp(p->source, "tokenhub") ? QUOTA_TOKENHUB : !strcmp(p->source, "chatgpt") ? QUOTA_CODEX : QUOTA_GENERIC;
    if (!text(row, "label", p->label, sizeof(p->label))) snprintf(p->label, sizeof(p->label), "%.23s", p->source);
    text(row, "plan", p->plan, sizeof(p->plan));
    p->reset_count = -1;
    int64_t checked = quota_parse_timestamp(row, "checked_at");
    const cJSON *status = get(row, "status");
    if (!checked || checked > report_time || !cJSON_IsString(status)) return false;
    bool failed = !strcmp(status->valuestring, "error");
    if (!failed && strcmp(status->valuestring, "ok")) return false;
    p->updated_at = quota_parse_timestamp(row, "collected_at");
    const cJSON *collected = get(row, "collected_at");
    if (p->updated_at > checked || (!failed && !p->updated_at) ||
        (collected && !cJSON_IsNull(collected) && !p->updated_at)) return false;
    p->status = failed ? QUOTA_COLLECTOR : QUOTA_OK;
    if (!p->updated_at) return failed;
    const cJSON *windows = get(row, "windows"), *w;
    if (windows && (!cJSON_IsArray(windows) || cJSON_GetArraySize(windows) > QUOTA_WINDOWS)) return false;
    cJSON_ArrayForEach(w, windows) {
        quota_window_t *v = &p->windows[p->window_count++];
        double minutes, used, remaining;
        if (!numeric(w, "window_minutes", &minutes) || minutes <= 0 || minutes > 525600 || floor(minutes) != minutes ||
            !numeric(w, "used_percent", &used) || used < 0 || !numeric(w, "remaining_percent", &remaining) ||
            remaining < 0 || remaining > 100 || fabs(fmax(0, 100 - used) - remaining) > 0.001 ||
            !timestamp(w, "resets_at_unix", &v->resets_at)) return false;
        v->minutes = (int)minutes; v->remaining = (float)remaining;
    }
    const cJSON *balances = get(row, "balances"), *b;
    if (balances && (!cJSON_IsArray(balances) || cJSON_GetArraySize(balances) > QUOTA_BALANCES)) return false;
    cJSON_ArrayForEach(b, balances) {
        quota_balance_t *v = &p->balances[p->balance_count++];
        if (!text(b, "currency", v->currency, sizeof(v->currency)) ||
            !money(b, "total", v->total, sizeof(v->total)) || !money(b, "granted", v->granted, sizeof(v->granted)) ||
            !money(b, "topped_up", v->topped_up, sizeof(v->topped_up))) return false;
    }
    p->available = cJSON_IsTrue(get(row, "available"));
    const cJSON *reset = get(row, "reset_credits");
    if (reset && !cJSON_IsObject(reset)) return false;
    const cJSON *count = get(reset, "available_count");
    if (count && !cJSON_IsNull(count)) {
        double value;
        if (!numeric(reset, "available_count", &value) || value < 0 || value > 1000000 || floor(value) != value) return false;
        p->reset_count = (int)value;
    }
    if (!timestamp(reset, "latest_expires_at_unix", &p->latest_reset_expires_at) ||
        !timestamp(reset, "next_expires_at_unix", &p->next_reset_expires_at)) return false;
    const cJSON *points = get(row, "points");
    if (points) {
        /* A generic points shape works for future collector plugins too. */
        if (!cJSON_IsObject(points) || p->window_count || p->balance_count || get(row, "tokenhub") || reset ||
            !money(points, "balance", p->points.balance, sizeof(p->points.balance)) ||
            !money(points, "spent_today", p->points.spent_today, sizeof(p->points.spent_today)) ||
            !money(points, "added_today", p->points.added_today, sizeof(p->points.added_today))) return false;
        p->kind = QUOTA_POINTS;
    }
    if (p->kind == QUOTA_TOKENHUB) {
        /* Reuse the validated decimal/unlimited semantics of the legacy reader. */
        cJSON *wrapper = cJSON_CreateObject();
        if (!wrapper) return false;
        cJSON_AddNumberToObject(wrapper, "schema_version", 1);
        cJSON_AddStringToObject(wrapper, "source", "telecom-tokenhub");
        cJSON_AddStringToObject(wrapper, "status", "ok");
        const cJSON *collected = get(row, "collected_at");
        cJSON_AddStringToObject(wrapper, "collected_at", collected->valuestring);
        cJSON_AddItemToObject(wrapper, "data", cJSON_Duplicate(get(row, "tokenhub"), true));
        quota_provider_t parsed = {0};
        quota_status_t result = quota_parse_tokenhub(wrapper, &parsed);
        cJSON_Delete(wrapper);
        if (result != QUOTA_OK) return false;
        p->balance_count = parsed.balance_count;
        memcpy(p->balances, parsed.balances, sizeof(p->balances));
    }
    return true;
}

quota_status_t quota_collection_apply(quota_snapshot_t *s, const cJSON *root, const char *collector) {
    const cJSON *schema = get(root, "schema_version");
    if (cJSON_IsNumber(schema) && schema->valuedouble == 1) return quota_codex_apply_report(s, root, collector);
    const cJSON *id = get(root, "collector_id"), *accounts = get(root, "accounts");
    int64_t report_time = quota_parse_timestamp(root, "collected_at");
    if (!cJSON_IsNumber(schema) || schema->valuedouble != 2 || !cJSON_IsString(id) || strcmp(id->valuestring, collector) ||
        !quota_collector_valid(collector) || !report_time || !cJSON_IsArray(accounts)) return QUOTA_INVALID;
    int count = cJSON_GetArraySize(accounts), others = 0;
    if (count > QUOTA_MAX_CODEX) return QUOTA_LIMIT;
    quota_provider_t *batch = calloc(count ? count : 1, sizeof(*batch));
    if (!batch) return QUOTA_STORAGE;
    quota_status_t result = QUOTA_OK;
    for (int i = 0; i < count; i++) {
        if (!parse_row(cJSON_GetArrayItem(accounts, i), report_time, &batch[i])) { result = QUOTA_INVALID; break; }
        snprintf(batch[i].collector, sizeof(batch[i].collector), "%s", collector);
        for (int j = 0; j < i; j++)
            if (!strcmp(batch[j].source, batch[i].source) && !strcmp(batch[j].account_id, batch[i].account_id)) result = QUOTA_INVALID;
        /* Errors with no new data retain the old source timestamp and quota. */
        if (batch[i].status != QUOTA_OK && !batch[i].updated_at) {
            for (int j = 0; j < s->codex_count; j++) {
                quota_provider_t *old = &s->providers[QUOTA_CODEX + j];
                if (!strcmp(old->collector, collector) && !strcmp(old->source, batch[i].source) && !strcmp(old->account_id, batch[i].account_id)) {
                    batch[i] = *old; batch[i].status = QUOTA_COLLECTOR; break;
                }
            }
        }
    }
    for (int i = 0; i < s->codex_count; i++) if (strcmp(s->providers[QUOTA_CODEX + i].collector, collector)) others++;
    if (others + count > QUOTA_MAX_CODEX) result = QUOTA_LIMIT;
    if (result == QUOTA_OK) {
        quota_codex_remove_collector(s, collector);
        for (int i = 0; i < count; i++) s->providers[QUOTA_CODEX + s->codex_count++] = batch[i];
        s->unified_mode = true;
    }
    free(batch);
    return result;
}
