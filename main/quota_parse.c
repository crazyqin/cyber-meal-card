#include "quota_parse.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Bound recursive cJSON parsing before touching its call stack. Brackets in
 * escaped strings do not contribute to nesting. Reject trailing documents. */
cJSON *quota_parse_document(const char *text, size_t length) {
    if (!text || !length) return NULL;
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < length; i++) {
        char c = text[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 16) return NULL; }
        else if (c == '}' || c == ']') { if (!depth) return NULL; depth--; }
    }
    if (depth || quoted) return NULL;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(text, length, &end, false);
    if (!root) return NULL;
    while (end < text + length && (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t')) end++;
    if (end != text + length || !cJSON_IsObject(root)) { cJSON_Delete(root); return NULL; }
    return root;
}

static const cJSON *field(const cJSON *o, const char *key) { return cJSON_GetObjectItemCaseSensitive(o, key); }
static bool number(const cJSON *o, const char *key, double *value) {
    const cJSON *n = field(o, key);
    if (!cJSON_IsNumber(n) || !isfinite(n->valuedouble)) return false;
    *value = n->valuedouble;
    return true;
}
/* Cline emits UTC RFC3339 with optional nanoseconds. The calendar portion is
 * sufficient; reject other zones rather than silently using local time. */
int64_t quota_parse_timestamp(const cJSON *o, const char *key) {
    const cJSON *s = field(o, key);
    if (!cJSON_IsString(s)) return 0;
    size_t len = strlen(s->valuestring);
    if (len < 20 || s->valuestring[4] != '-' || s->valuestring[7] != '-' ||
        s->valuestring[10] != 'T' || s->valuestring[13] != ':' || s->valuestring[16] != ':' ||
        s->valuestring[len - 1] != 'Z') return 0;
    for (size_t i = 0; i < len - 1; i++) {
        if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16) continue;
        if (i == 19 && len > 21 && s->valuestring[i] == '.') continue;
        if (s->valuestring[i] < '0' || s->valuestring[i] > '9') return 0;
    }
    if (len != 20 && (len < 22 || s->valuestring[19] != '.')) return 0;
    int y, m, d, h, minute, sec;
    if (sscanf(s->valuestring, "%d-%d-%dT%d:%d:%d", &y, &m, &d, &h, &minute, &sec) != 6 ||
        y < 2023 || y > 2099 || m < 1 || m > 12 || d < 1 || d > 31 ||
        h < 0 || h > 23 || minute < 0 || minute > 59 || sec < 0 || sec > 59) return 0;
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int max_day = days[m - 1] + (m == 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)));
    if (d > max_day) return 0;
    /* Civil date -> epoch, independent of the process timezone. */
    y -= m <= 2;
    int era = y / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return ((int64_t)era * 146097 + doe - 719468) * 86400 + h * 3600 + minute * 60 + sec;
}
bool quota_parse_cline(const cJSON *root, quota_provider_t *out) {
    if (!cJSON_IsTrue(field(root, "success"))) return false;
    const cJSON *limits = field(field(root, "data"), "limits");
    if (!cJSON_IsArray(limits)) return false;
    quota_provider_t p = {0};
    const char *types[] = {"five_hour", "weekly", "monthly"};
    const int durations[] = {300, 10080, 43200};
    bool found[3] = {false};
    const cJSON *w;
    cJSON_ArrayForEach(w, limits) {
        const cJSON *type = field(w, "type");
        if (!cJSON_IsString(type)) continue;
        for (int i = 0; i < 3; i++) {
            if (strcmp(type->valuestring, types[i])) continue;
            double used;
            if (found[i] || !number(w, "percentUsed", &used) ||
                !quota_remaining(used, &p.windows[p.window_count].remaining)) return false;
            found[i] = true;
            p.windows[p.window_count].minutes = durations[i];
            p.windows[p.window_count].resets_at = quota_parse_timestamp(w, "resetsAt");
            p.window_count++;
        }
    }
    if (!p.window_count) return false;
    snprintf(p.plan, sizeof(p.plan), "ClinePass");
    p.status = QUOTA_OK;
    *out = p;
    return true;
}
static bool amount(const cJSON *o, const char *key, char *out, size_t cap) {
    const cJSON *s = field(o, key);
    if (!cJSON_IsString(s) || !s->valuestring[0] || strlen(s->valuestring) >= cap) return false;
    bool dot = false;
    for (const char *c = s->valuestring; *c; c++) {
        if (*c == '.' && !dot) dot = true;
        else if (*c < '0' || *c > '9') return false;
    }
    if (!strcmp(s->valuestring, ".")) return false;
    snprintf(out, cap, "%s", s->valuestring);
    return true;
}
static bool tokenhub_amount(const cJSON *o, const char *key, char *out, size_t cap, bool negative) {
    const cJSON *s = field(o, key);
    if (!cJSON_IsString(s) || strlen(s->valuestring) >= cap) return false;
    const char *p = s->valuestring;
    if (*p == '-' && negative) p++;
    unsigned digits = 0;
    while (*p >= '0' && *p <= '9') { digits++; p++; }
    if (!digits || digits > 15 || *p++ != '.') return false;
    if (strlen(p) != 2 || p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return false;
    snprintf(out, cap, "%s", s->valuestring);
    return true;
}
quota_status_t quota_parse_tokenhub(const cJSON *root, quota_provider_t *out) {
    const cJSON *source = field(root, "source"), *status = field(root, "status");
    double schema;
    if (!out || !number(root, "schema_version", &schema) || schema != 1 ||
        !cJSON_IsString(source) || strcmp(source->valuestring, "telecom-tokenhub") ||
        !cJSON_IsString(status)) return QUOTA_INVALID;
    if (!strcmp(status->valuestring, "error")) return QUOTA_COLLECTOR;
    if (strcmp(status->valuestring, "ok")) return QUOTA_INVALID;
    const cJSON *data = field(root, "data"), *currency = field(data, "currency");
    if (!cJSON_IsObject(data) || !cJSON_IsString(currency) || strcmp(currency->valuestring, "CNY") ||
        !cJSON_IsTrue(field(data, "remaining_is_estimate"))) return QUOTA_INVALID;
    quota_provider_t p = {0};
    p.updated_at = quota_parse_timestamp(root, "collected_at");
    if (!p.updated_at) return QUOTA_INVALID;
    quota_balance_t *a = &p.balances[0], *today = &p.balances[1];
    bool unlimited = cJSON_IsNull(field(data, "allocation_yuan"));
    if (unlimited) {
        if (!cJSON_IsNull(field(data, "estimated_remaining_yuan"))) return QUOTA_INVALID;
        snprintf(a->total, sizeof(a->total), "--");
        snprintf(a->granted, sizeof(a->granted), "不限额");
    } else if (!tokenhub_amount(data, "allocation_yuan", a->granted, sizeof(a->granted), false) ||
               !tokenhub_amount(data, "estimated_remaining_yuan", a->total, sizeof(a->total), true)) return QUOTA_INVALID;
    if (!tokenhub_amount(data, "month_spent_yuan", a->topped_up, sizeof(a->topped_up), false) ||
        !tokenhub_amount(data, "range_spent_yuan", today->total, sizeof(today->total), false)) return QUOTA_INVALID;
    snprintf(a->currency, sizeof(a->currency), "CNY");
    snprintf(today->currency, sizeof(today->currency), "CNY");
    snprintf(p.plan, sizeof(p.plan), "电信 / COS");
    p.balance_count = 2;
    p.status = QUOTA_OK;
    *out = p;
    return QUOTA_OK;
}
bool quota_parse_deepseek(const cJSON *root, quota_provider_t *out) {
    const cJSON *infos = field(root, "balance_infos");
    if (!cJSON_IsArray(infos) || !cJSON_IsBool(field(root, "is_available"))) return false;
    quota_provider_t p = {0};
    p.available = cJSON_IsTrue(field(root, "is_available"));
    const cJSON *row;
    cJSON_ArrayForEach(row, infos) {
        const cJSON *currency = field(row, "currency");
        if (!cJSON_IsString(currency) || (strcmp(currency->valuestring, "CNY") && strcmp(currency->valuestring, "USD"))) continue;
        if (p.balance_count >= QUOTA_BALANCES) return false;
        quota_balance_t *b = &p.balances[p.balance_count];
        for (int i = 0; i < p.balance_count; i++) if (!strcmp(p.balances[i].currency, currency->valuestring)) return false;
        snprintf(b->currency, sizeof(b->currency), "%s", currency->valuestring);
        if (!amount(row, "total_balance", b->total, sizeof(b->total)) ||
            !amount(row, "granted_balance", b->granted, sizeof(b->granted)) ||
            !amount(row, "topped_up_balance", b->topped_up, sizeof(b->topped_up))) return false;
        p.balance_count++;
    }
    if (!p.balance_count) return false;
    snprintf(p.plan, sizeof(p.plan), "API");
    p.status = QUOTA_OK;
    *out = p;
    return true;
}

static bool equals(const cJSON *o, const char *key, const char *value) {
    const cJSON *s = field(o, key);
    return cJSON_IsString(s) && !strcmp(s->valuestring, value);
}
/* Return an omitted window without inventing its remaining percentage. The
 * collector explicitly clamps over-limit usage to zero remaining. */
static bool codex_window(const cJSON *record, const char *key, int minutes, quota_provider_t *p) {
    const cJSON *w = field(record, key);
    if (!w || cJSON_IsNull(w)) return true;
    if (!cJSON_IsObject(w)) return false;
    double duration;
    if (!number(w, "window_minutes", &duration) || duration != minutes) return false;
    const cJSON *remaining = field(w, "remaining_percent"), *used = field(w, "used_percent");
    if ((!remaining || cJSON_IsNull(remaining)) && (!used || cJSON_IsNull(used))) return true;
    if (!cJSON_IsNumber(remaining) || !isfinite(remaining->valuedouble) || remaining->valuedouble < 0 ||
        remaining->valuedouble > 100 || !cJSON_IsNumber(used) || !isfinite(used->valuedouble) || used->valuedouble < 0)
        return false;
    double expected = fmax(0, 100 - used->valuedouble);
    if (fabs(expected - remaining->valuedouble) > 0.001) return false;
    quota_window_t *out = &p->windows[p->window_count];
    out->minutes = minutes;
    out->remaining = (float)remaining->valuedouble;
    const cJSON *reset = field(w, "resets_at_unix");
    if (reset && !cJSON_IsNull(reset)) {
        if (!cJSON_IsNumber(reset) || !isfinite(reset->valuedouble) || floor(reset->valuedouble) != reset->valuedouble ||
            reset->valuedouble < 0 || reset->valuedouble > 4102444799.0) return false;
        out->resets_at = (int64_t)reset->valuedouble;
    }
    p->window_count++;
    return true;
}
quota_status_t quota_parse_codex(const cJSON *root, const char *collector,
                                const char *account, quota_provider_t *out) {
    double schema;
    if (!number(root, "schema_version", &schema) || schema != 1 || !equals(root, "collector_id", collector)) return QUOTA_INVALID;
    const cJSON *records = field(root, "accounts");
    if (!cJSON_IsArray(records)) return QUOTA_INVALID;
    const cJSON *match = NULL, *row;
    cJSON_ArrayForEach(row, records) {
        if (!equals(row, "name", account)) continue;
        if (match) return QUOTA_INVALID;
        match = row;
    }
    if (!match) return QUOTA_ACCOUNT_MISSING;
    if (!equals(match, "status", "ok")) return QUOTA_COLLECTOR;
    quota_provider_t p = {.kind = QUOTA_CODEX, .reset_count = -1};
    snprintf(p.label, sizeof(p.label), "Codex");
    p.updated_at = quota_parse_timestamp(match, "collected_at");
    int64_t report_time = quota_parse_timestamp(root, "collected_at");
    if (!p.updated_at || !report_time || p.updated_at > report_time) return QUOTA_INVALID;
    if (!codex_window(match, "five_hour", 300, &p) || !codex_window(match, "weekly", 10080, &p)) return QUOTA_INVALID;
    if (!p.window_count) return QUOTA_UNSUPPORTED;
    const cJSON *plan = field(match, "plan_type");
    /* Untrusted display strings stay bounded; account identity comes from the
     * exact configured selector, never from the first row of the report. */
    if (cJSON_IsString(plan)) {
        size_t length = strlen(plan->valuestring);
        bool ascii = length < sizeof(p.plan);
        for (size_t i = 0; i < length; i++)
            if ((unsigned char)plan->valuestring[i] < 32 || (unsigned char)plan->valuestring[i] > 126) ascii = false;
        if (ascii) snprintf(p.plan, sizeof(p.plan), "%s", plan->valuestring);
    }
    snprintf(p.account, sizeof(p.account), "%s", account);
    p.status = QUOTA_OK;
    *out = p;
    return QUOTA_OK;
}
