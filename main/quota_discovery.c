#include "quota_discovery.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool xml_text(const char *start, const char *end, char *out, size_t cap) {
    size_t n = 0;
    for (const char *p = start; p < end; p++) {
        unsigned char c = *p;
        if (c == '&') {
            static const char *entities[] = {"amp;", "lt;", "gt;", "quot;", "apos;"};
            static const char decoded[] = "&<>\"'";
            bool found = false;
            for (int i = 0; i < 5; i++) {
                size_t length = strlen(entities[i]);
                if ((size_t)(end - p - 1) >= length && !memcmp(p + 1, entities[i], length)) {
                    c = decoded[i]; p += length; found = true; break;
                }
            }
            if (!found) return false;
        }
        if (c < 32 || c == '<' || n + 1 >= cap) return false;
        out[n++] = c;
    }
    out[n] = 0;
    return true;
}
/* Small bounded XML reader for COS ListBucketResult. No DTDs, external
 * entities or recursive descent. Unknown fields are traversed, never acted on. */
bool quota_cos_parse_list(const char *xml, size_t length, const char *prefix, quota_cos_page_t *page) {
    if (!xml || !length || length >= 24576 || strlen(xml) != length) return false;
    memset(page, 0, sizeof(*page));
    struct { char name[32]; const char *text; } stack[8];
    unsigned depth = 0, roots = 0;
    bool ended = false, has_truncated = false, has_marker = false;
    const char *p = xml, *end = xml + length;
    while (p < end) {
        if (*p != '<') {
            if (!depth && !isspace((unsigned char)*p)) return false;
            p++; continue;
        }
        if (!strncmp(p, "<?xml ", 6) && !roots && !depth) {
            const char *stop = strstr(p, "?>"); if (!stop) return false;
            p = stop + 2; continue;
        }
        const char *tag = p++;
        bool closing = *p == '/';
        if (closing) p++;
        char name[32]; size_t n = 0;
        while (p < end && (isalnum((unsigned char)*p) || *p == '-' || *p == '_')) {
            if (n + 1 >= sizeof(name)) return false;
            name[n++] = *p++;
        }
        name[n] = 0;
        if (!n) return false;
        bool quoted = false; char quote = 0;
        while (p < end && (*p != '>' || quoted)) {
            if (*p == '<') return false;
            if (closing && !isspace((unsigned char)*p)) return false;
            if (*p == '\'' || *p == '"') {
                if (!quoted) { quote = *p; quoted = true; }
                else if (*p == quote) quoted = false;
            }
            p++;
        }
        if (p == end || quoted) return false;
        bool empty = !closing && p > tag && p[-1] == '/';
        p++;
        if (!closing) {
            if (ended || depth >= 8) return false;
            if (!depth && (roots++ || strcmp(name, "ListBucketResult"))) return false;
            snprintf(stack[depth].name, sizeof(stack[depth].name), "%s", name);
            stack[depth++].text = p;
            if (!empty) continue;
            tag = p;
        }
        if (!depth || strcmp(stack[depth - 1].name, name)) return false;
        if (depth == 2 && !strcmp(name, "IsTruncated")) {
            char value[8];
            if (has_truncated || !xml_text(stack[depth - 1].text, tag, value, sizeof(value)) ||
                (strcmp(value, "true") && strcmp(value, "false"))) return false;
            has_truncated = true; page->truncated = !strcmp(value, "true");
        } else if (depth == 2 && !strcmp(name, "NextMarker")) {
            if (has_marker || !xml_text(stack[depth - 1].text, tag, page->next_marker, sizeof(page->next_marker))) return false;
            has_marker = true;
        } else if (depth == 3 && !strcmp(name, "Prefix") && !strcmp(stack[1].name, "CommonPrefixes")) {
            char value[224], base[132];
            snprintf(base, sizeof(base), "%s%s", prefix, prefix[0] ? "/" : "");
            if (!xml_text(stack[depth - 1].text, tag, value, sizeof(value))) return false;
            size_t b = strlen(base), v = strlen(value);
            if (v <= b + 1 || strncmp(value, base, b) || value[v - 1] != '/') return false;
            value[v - 1] = 0;
            const char *collector = value + b;
            if (!quota_collector_valid(collector) || page->count >= QUOTA_COS_LIST_SIZE) return false;
            for (int i = 0; i < page->count; i++) if (!strcmp(page->collectors[i], collector)) return false;
            snprintf(page->collectors[page->count++], 65, "%s", collector);
        }
        if (!--depth) ended = true;
    }
    return ended && !depth && has_truncated && (!page->truncated || page->next_marker[0]);
}
static bool alias_valid(const cJSON *name) {
    if (!cJSON_IsString(name) || !name->valuestring[0] || strlen(name->valuestring) > 64) return false;
    for (const unsigned char *p = (const unsigned char *)name->valuestring; *p; p++)
        if (*p < 32 || *p > 126) return false;
    return true;
}
static quota_provider_t *find(quota_snapshot_t *s, const char *collector, const char *account) {
    for (int i = 0; i < s->codex_count; i++) {
        quota_provider_t *p = &s->providers[QUOTA_CODEX + i];
        if (!strcmp(p->collector, collector) && !strcmp(p->account, account)) return p;
    }
    return NULL;
}
void quota_codex_fail_collector(quota_snapshot_t *s, const char *collector, quota_status_t status) {
    for (int i = 0; i < s->codex_count; i++) {
        quota_provider_t *p = &s->providers[QUOTA_CODEX + i];
        if (!strcmp(p->collector, collector)) p->status = status;
    }
}
void quota_codex_remove_collector(quota_snapshot_t *s, const char *collector) {
    int n = 0;
    for (int i = 0; i < s->codex_count; i++)
        if (strcmp(s->providers[QUOTA_CODEX + i].collector, collector)) s->providers[QUOTA_CODEX + n++] = s->providers[QUOTA_CODEX + i];
    s->codex_count = n;
}
quota_status_t quota_codex_apply_report(quota_snapshot_t *s, const cJSON *root, const char *collector) {
    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema_version");
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "collector_id");
    const cJSON *accounts = cJSON_GetObjectItemCaseSensitive(root, "accounts");
    if (!cJSON_IsNumber(schema) || schema->valuedouble != 1 || !cJSON_IsString(id) || strcmp(id->valuestring, collector) || !cJSON_IsArray(accounts)) return QUOTA_INVALID;
    int count = cJSON_GetArraySize(accounts);
    if (count > QUOTA_MAX_CODEX) return QUOTA_LIMIT;
    quota_provider_t *batch = calloc(count ? count : 1, sizeof(*batch));
    if (!batch) return QUOTA_STORAGE;
    quota_status_t result = QUOTA_OK;
    for (int i = 0; i < count; i++) {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(accounts, i), "name");
        if (!alias_valid(name)) { result = QUOTA_INVALID; break; }
        for (int j = 0; j < i; j++) if (!strcmp(batch[j].account, name->valuestring)) result = QUOTA_INVALID;
        if (result != QUOTA_OK) break;
        quota_status_t status = quota_parse_codex(root, collector, name->valuestring, &batch[i]);
        if (status == QUOTA_INVALID || status == QUOTA_ACCOUNT_MISSING) { result = QUOTA_INVALID; break; }
        if (status != QUOTA_OK) {
            quota_provider_t *old = find(s, collector, name->valuestring);
            if (old) batch[i] = *old;
            batch[i].status = status;
        }
        snprintf(batch[i].account, sizeof(batch[i].account), "%s", name->valuestring);
        snprintf(batch[i].collector, sizeof(batch[i].collector), "%s", collector);
    }
    int others = 0;
    for (int i = 0; i < s->codex_count; i++) if (strcmp(s->providers[QUOTA_CODEX + i].collector, collector)) others++;
    if (others + count > QUOTA_MAX_CODEX) result = QUOTA_LIMIT;
    if (result == QUOTA_OK) {
        int n = 0;
        for (int i = 0; i < s->codex_count; i++)
            if (strcmp(s->providers[QUOTA_CODEX + i].collector, collector)) s->providers[QUOTA_CODEX + n++] = s->providers[QUOTA_CODEX + i];
        for (int i = 0; i < count; i++) s->providers[QUOTA_CODEX + n++] = batch[i];
        s->codex_count = n;
    }
    free(batch);
    return result;
}
void quota_codex_prune(quota_snapshot_t *s, const char collectors[][65], int count) {
    int n = 0;
    for (int i = 0; i < s->codex_count; i++) {
        bool keep = false;
        for (int j = 0; j < count; j++) if (!strcmp(s->providers[QUOTA_CODEX + i].collector, collectors[j])) keep = true;
        if (keep) s->providers[QUOTA_CODEX + n++] = s->providers[QUOTA_CODEX + i];
    }
    s->codex_count = n;
}
static int order(const void *a, const void *b) {
    const quota_provider_t *x = a, *y = b;
    if (x->source[0] || y->source[0]) {
        int result = strcmp(x->source, y->source);
        if (!result) result = strcmp(x->account_id, y->account_id);
        if (result) return result;
    }
    int result = strcmp(x->collector, y->collector);
    return result ? result : strcmp(x->account, y->account);
}
void quota_codex_sort(quota_snapshot_t *s) {
    qsort(s->providers + QUOTA_CODEX, s->codex_count, sizeof(s->providers[0]), order);
}
