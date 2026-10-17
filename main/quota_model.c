#include "quota_model.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

bool quota_remaining(double used, float *remaining) {
    if (!remaining || !isfinite(used) || used < 0 || used > 100) return false;
    *remaining = (float)(100.0 - used);
    return true;
}
int quota_next_provider(int current, int direction, int count) {
    if (count < 1) return 0;
    if (current < 0 || current >= count) current = 0;
    return (current + (direction < 0 ? count - 1 : 1)) % count;
}
void quota_select(const quota_snapshot_t *s, quota_selection_t *selection, quota_view_t *view) {
    int n = s->codex_count;
    if (n < 0 || n > QUOTA_MAX_CODEX) n = 0;
    if (s->unified_mode) {
        int winners[QUOTA_MAX_CODEX], count = 0;
        for (int i = 0; i < n; i++) {
            const quota_provider_t *p = &s->providers[QUOTA_CODEX + i];
            int match = -1;
            for (int j = 0; j < count; j++) {
                const quota_provider_t *q = &s->providers[QUOTA_CODEX + winners[j]];
                if (p->source[0] && p->account_id[0] && !strcmp(p->source, q->source) && !strcmp(p->account_id, q->account_id)) match = j;
            }
            if (match < 0) winners[count++] = i;
            else {
                const quota_provider_t *q = &s->providers[QUOTA_CODEX + winners[match]];
                if (p->updated_at > q->updated_at || (p->updated_at == q->updated_at && p->status == QUOTA_OK && q->status != QUOTA_OK)) winners[match] = i;
            }
        }
        int index = selection->index;
        for (int i = 0; i < count && selection->collector[0]; i++) {
            const quota_provider_t *p = &s->providers[QUOTA_CODEX + winners[i]];
            const char *source = p->source[0] ? p->source : p->collector;
            const char *identity = p->account_id[0] ? p->account_id : p->account;
            if (!strcmp(source, selection->collector) && !strcmp(identity, selection->account)) { index = i; break; }
        }
        if (index < 0) index = 0;
        if (index >= count) index = count ? count - 1 : 0;
        view->count = count ? count : 1;
        view->index = selection->index = index;
        memset(&view->provider, 0, sizeof(view->provider));
        view->provider.reset_count = -1;
        view->provider.kind = QUOTA_GENERIC;
        if (count) view->provider = s->providers[QUOTA_CODEX + winners[index]];
        else view->provider.status = s->discovery_status == QUOTA_OK ? QUOTA_EMPTY : s->discovery_status;
        const quota_provider_t *p = &view->provider;
        snprintf(selection->collector, sizeof(selection->collector), "%s", p->source[0] ? p->source : p->collector);
        snprintf(selection->account, sizeof(selection->account), "%s", p->account_id[0] ? p->account_id : p->account);
        view->discovery_status = s->discovery_status;
        view->discovery_limited = s->discovery_limited;
        return;
    }
    view->count = QUOTA_BASE_PROVIDERS + (n ? n : 1);
    int index = selection->index;
    if (selection->collector[0]) {
        for (int i = 0; i < n; i++) {
            const quota_provider_t *p = &s->providers[QUOTA_CODEX + i];
            if (!strcmp(p->collector, selection->collector) && !strcmp(p->account, selection->account)) { index = QUOTA_CODEX + i; break; }
        }
    }
    if (index < 0) index = 0;
    if (index >= view->count) index = view->count - 1;
    view->index = selection->index = index;
    view->provider = s->providers[index];
    if (!n && index == QUOTA_CODEX) {
        memset(&view->provider, 0, sizeof(view->provider));
        view->provider.status = s->discovery_status == QUOTA_OK ? QUOTA_EMPTY : s->discovery_status;
    }
    snprintf(selection->collector, sizeof(selection->collector), "%s", index >= QUOTA_CODEX ? view->provider.collector : "");
    snprintf(selection->account, sizeof(selection->account), "%s", index >= QUOTA_CODEX ? view->provider.account : "");
    view->discovery_status = s->discovery_status;
    view->discovery_limited = s->discovery_limited;
}
bool quota_rotation_due(int64_t now_us, int64_t rotated_us, int64_t hold_until_us) {
    return now_us >= hold_until_us && now_us >= rotated_us && now_us - rotated_us >= 10000000;
}
bool quota_is_stale(const quota_provider_t *p, int64_t now) {
    return p->updated_at <= 0 || now < p->updated_at || now - p->updated_at > 900 || p->status != QUOTA_OK;
}
int quota_sync_wait_seconds(int64_t now, int64_t last, bool manual) {
    int64_t wait = (manual ? 10000 : 60000) - (now - last);
    return wait > 0 ? (int)((wait + 999) / 1000) : 0;
}
void quota_percent_label(float remaining, char *out, unsigned size) {
    /* LVGL's built-in formatter has no %f support with LV_USE_FLOAT=0. Keep
     * display text independent of that option and reject invalid cached data. */
    if (!isfinite(remaining) || remaining < 0 || remaining > 100) snprintf(out, size, "--");
    else snprintf(out, size, "%d%%", (int)(remaining + 0.5f));
}
void quota_window_label(int minutes, char *out, unsigned size) {
    if (minutes > 0 && minutes % 1440 == 0) snprintf(out, size, "%dd", minutes / 1440);
    else if (minutes > 0 && minutes % 60 == 0) snprintf(out, size, "%dh", minutes / 60);
    else snprintf(out, size, "%dm", minutes);
}
void quota_reset_label(int64_t reset, int64_t now, char *out, unsigned size) {
    if (reset <= 0 || now < 1700000000) snprintf(out, size, "--");
    else if (reset <= now) snprintf(out, size, "待刷新");
    else {
        int64_t m = (reset - now + 59) / 60;
        if (m >= 1440) snprintf(out, size, "%lld天%lld时", (long long)(m / 1440), (long long)(m % 1440 / 60));
        else snprintf(out, size, "%lld时%lld分", (long long)(m / 60), (long long)(m % 60));
    }
}
void quota_window_detail_label(const quota_window_t *window, int64_t now, char *out, unsigned size) {
    if (window->resets_at <= 0) {
        if (window->minutes == 300 && window->remaining == 100.0f)
            snprintf(out, size, "尚未开始计时");
        else snprintf(out, size, "重置时间未提供");
    } else if (now < 1700000000) snprintf(out, size, "等待校时");
    else {
        char reset[48];
        quota_reset_label(window->resets_at, now, reset, sizeof(reset));
        snprintf(out, size, "重置 %s", reset);
    }
}
