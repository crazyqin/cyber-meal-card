#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif
#include "quota_presentation.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

void quota_date_label(int64_t timestamp, char *out, unsigned size) {
    /* Collection/reset timestamps are UTC; the device consistently shows UTC+8. */
    time_t stamp = timestamp + 8 * 3600;
    struct tm date;
#ifdef _WIN32
    if (gmtime_s(&date, &stamp)) { snprintf(out, size, "--"); return; }
#else
    if (!gmtime_r(&stamp, &date)) { snprintf(out, size, "--"); return; }
#endif
    snprintf(out, size, "%02d/%02d %02d:%02d", date.tm_mon + 1, date.tm_mday, date.tm_hour, date.tm_min);
}

void quota_money_label(const char *currency, const char *amount, char *out, unsigned size) {
    const char *unit = !strcmp(currency, "CNY") ? "¥" : !strcmp(currency, "USD") ? "$" : currency;
    bool negative = amount[0] == '-';
    const char *number = amount + negative;
    char digits[40] = {0};
    unsigned length = 0, integer = 0;
    bool dot = false, nonzero = false;
    for (const char *s = number; *s; s++) {
        if (*s == '.' && !dot) { dot = true; continue; }
        if (*s < '0' || *s > '9' || length >= sizeof(digits) - 5) {
            snprintf(out, size, "--"); return;
        }
        digits[length++] = *s;
        if (!dot) integer++;
        if (*s != '0') nonzero = true;
    }
    if (!length || !integer) { snprintf(out, size, "--"); return; }
    /* Drop redundant leading zeros, retaining the units digit. */
    while (integer > 1 && digits[0] == '0') {
        memmove(digits, digits + 1, length--); integer--;
    }
    while (length < integer + 3) digits[length++] = '0';
    digits[length] = 0;
    if (nonzero && integer == 1 && digits[0] == '0' && digits[1] == '0' && digits[2] == '0') {
        snprintf(out, size, "%s%s%s0.01", negative ? ">" : "<", negative ? "-" : "", unit);
        return;
    }
    /* Decimal arithmetic keeps large balances and half-cent rounding exact. */
    if (digits[integer + 2] >= '5') {
        int i = (int)integer + 1;
        while (i >= 0 && digits[i] == '9') digits[i--] = '0';
        if (i >= 0) digits[i]++;
        else {
            memmove(digits + 1, digits, length + 1); digits[0] = '1'; integer++;
        }
    }
    const char *sign = negative && nonzero ? "-" : "";
    unsigned limit = strlen(unit) == 3 && strcmp(currency, "CNY") ? 6 : 8;
    if (integer > limit) {
        /* Explicit scientific notation fits the small screen without clipping. */
        int a = digits[0] - '0', b = digits[1] - '0', c = digits[2] - '0';
        if (digits[3] >= '5' && ++c == 10) {
            c = 0;
            if (++b == 10) { b = 0; if (++a == 10) { a = 1; integer++; } }
        }
        snprintf(out, size, "%s%s%d.%d%de%u", sign, unit, a, b, c, integer - 1);
    } else snprintf(out, size, "%s%s%.*s.%c%c", sign, unit, (int)integer, digits, digits[integer], digits[integer + 1]);
}

/* Provider names are frequently reused as account aliases and plan labels. */
static bool same_name(const char *a, const char *b) {
    for (;;) {
        while (*a == ' ' || *a == '-' || *a == '_') a++;
        while (*b == ' ' || *b == '-' || *b == '_') b++;
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        if (!*a) return true;
        a++; b++;
    }
}
static bool redundant(const quota_provider_t *p, const char *value, const char *title) {
    return !value[0] || same_name(value, title) || same_name(value, p->label) || same_name(value, p->source) ||
           same_name(value, "API") || same_name(value, "COS") || same_name(value, "CNY") ||
           same_name(value, "USD") || same_name(value, "CNY / estimated") || same_name(value, "电信 / COS");
}
static void balance_row(quota_metric_t *row, const char *name, const quota_balance_t *b, const char *amount) {
    snprintf(row->name, sizeof(row->name), "%s", name);
    quota_money_label(b->currency, amount, row->value, sizeof(row->value));
}
void quota_present_provider(const quota_provider_t *p, int64_t now, quota_presentation_t *out) {
    memset(out, 0, sizeof(*out));
    const char *canonical = p->kind == QUOTA_CLINE ? "ClinePass" : p->kind == QUOTA_DEEPSEEK ? "DeepSeek" :
                            p->kind == QUOTA_TOKENHUB ? "TokenHub" : p->kind == QUOTA_CODEX ? "ChatGPT" : "订阅用量";
    snprintf(out->title, sizeof(out->title), "%s", p->label[0] ? p->label : canonical);
    const char *account = redundant(p, p->account, out->title) || same_name(p->account, canonical) ? "" : p->account;
    const char *plan = redundant(p, p->plan, out->title) || same_name(p->plan, canonical) || same_name(p->plan, account) ? "" : p->plan;
    snprintf(out->subtitle, sizeof(out->subtitle), "%s%s%s", account, account[0] && plan[0] ? " · " : "", plan);
    if (!out->subtitle[0]) snprintf(out->subtitle, sizeof(out->subtitle), "%s", p->window_count ? "剩余额度" : p->kind == QUOTA_POINTS ? "积分明细" : "余额明细");
    if (!p->updated_at && !p->label[0]) snprintf(out->subtitle, sizeof(out->subtitle), "等待采集数据");
    if (!p->updated_at) return;
    if (p->kind == QUOTA_POINTS) {
        const char *names[] = {"积分余额", "今日消耗", "今日新增"};
        const char *values[] = {p->points.balance, p->points.spent_today, p->points.added_today};
        out->count = 3;
        for (int i = 0; i < 3; i++) {
            snprintf(out->rows[i].name, sizeof(out->rows[i].name), "%s", names[i]);
            quota_money_label("", values[i], out->rows[i].value, sizeof(out->rows[i].value));
            snprintf(out->rows[i].detail, sizeof(out->rows[i].detail), "积分");
        }
        return;
    }
    if (p->kind == QUOTA_TOKENHUB && p->balance_count >= 2) {
        const quota_balance_t *b = &p->balances[0];
        out->count = 3;
        bool unlimited = !strcmp(b->granted, "不限额");
        balance_row(&out->rows[0], unlimited ? "本月额度" : "剩余额度", b, b->total);
        if (unlimited) {
            snprintf(out->rows[0].value, sizeof(out->rows[0].value), "不限额");
            snprintf(out->rows[0].detail, sizeof(out->rows[0].detail), "按实际消费统计");
        } else {
            char allocation[48]; quota_money_label(b->currency, b->granted, allocation, sizeof(allocation));
            snprintf(out->rows[0].detail, sizeof(out->rows[0].detail), "分配 %s · 估算", allocation);
        }
        balance_row(&out->rows[1], "本月消费", b, b->topped_up);
        balance_row(&out->rows[2], "今日消费", &p->balances[1], p->balances[1].total);
        out->rows[0].warning = !unlimited && b->total[0] == '-';
        return;
    }
    if (p->balance_count && !p->window_count) {
        const quota_balance_t *b = &p->balances[0];
        if (p->balance_count == 1) {
            out->count = 3;
            balance_row(&out->rows[0], "账户余额", b, b->total);
            balance_row(&out->rows[1], "充值余额", b, b->topped_up);
            balance_row(&out->rows[2], "赠送余额", b, b->granted);
        } else {
            out->count = p->balance_count < 3 ? p->balance_count : 3;
            for (int i = 0; i < out->count; i++) {
                b = &p->balances[i];
                char name[40], topped[48], granted[48];
                snprintf(name, sizeof(name), "%s余额", !strcmp(b->currency, "CNY") ? "人民币" : !strcmp(b->currency, "USD") ? "美元" : b->currency);
                balance_row(&out->rows[i], name, b, b->total);
                quota_money_label(b->currency, b->topped_up, topped, sizeof(topped));
                quota_money_label(b->currency, b->granted, granted, sizeof(granted));
                snprintf(out->rows[i].detail, sizeof(out->rows[i].detail), "充值 %s  赠送 %s", topped, granted);
            }
        }
        return;
    }
    /* Short, weekly and monthly windows occupy the same positions on every page. */
    int order[QUOTA_WINDOWS];
    int count = p->window_count < QUOTA_WINDOWS ? p->window_count : QUOTA_WINDOWS;
    for (int i = 0; i < count; i++) {
        int j = i;
        while (j > 0 && p->windows[order[j - 1]].minutes > p->windows[i].minutes) { order[j] = order[j - 1]; j--; }
        order[j] = i;
    }
    out->count = count;
    for (int i = 0; i < count; i++) {
        const quota_window_t *w = &p->windows[order[i]];
        quota_metric_t *row = &out->rows[i];
        if (w->minutes == 300) snprintf(row->name, sizeof(row->name), "5 小时剩余");
        else if (w->minutes == 10080) snprintf(row->name, sizeof(row->name), "每周剩余");
        else if (w->minutes == 43200) snprintf(row->name, sizeof(row->name), "每月剩余");
        else {
            char window[16]; quota_window_label(w->minutes, window, sizeof(window));
            snprintf(row->name, sizeof(row->name), "%s 剩余", window);
        }
        quota_percent_label(w->remaining, row->value, sizeof(row->value));
        quota_window_detail_label(w, now, row->detail, sizeof(row->detail));
        if (w->resets_at > now && now >= 1700000000) {
            int64_t minutes = (w->resets_at - now + 59) / 60;
            if (minutes >= 1440 && minutes % 1440 / 60) snprintf(row->detail, sizeof(row->detail), "%lld天%lld小时后重置", (long long)(minutes / 1440), (long long)(minutes % 1440 / 60));
            else if (minutes >= 1440) snprintf(row->detail, sizeof(row->detail), "%lld天后重置", (long long)(minutes / 1440));
            else if (minutes >= 60 && minutes % 60) snprintf(row->detail, sizeof(row->detail), "%lld小时%lld分后重置", (long long)(minutes / 60), (long long)(minutes % 60));
            else if (minutes >= 60) snprintf(row->detail, sizeof(row->detail), "%lld小时后重置", (long long)(minutes / 60));
            else snprintf(row->detail, sizeof(row->detail), "%lld分钟后重置", (long long)minutes);
        }
        if (p->kind == QUOTA_CODEX && w->resets_at <= 0) snprintf(row->detail, sizeof(row->detail), "重置时间未提供");
        row->progress = true;
        row->remaining = (int)(w->remaining + 0.5f);
        row->warning = w->remaining < 20;
    }
    if (p->kind == QUOTA_CODEX && count < 3 && p->source[0]) {
        quota_metric_t *row = &out->rows[out->count++];
        snprintf(row->name, sizeof(row->name), "可用重置");
        if (p->reset_count < 0) snprintf(row->value, sizeof(row->value), "--");
        else snprintf(row->value, sizeof(row->value), "%d 次", p->reset_count);
        if (p->reset_count == 0) snprintf(row->detail, sizeof(row->detail), "暂无可用重置");
        else if (p->next_reset_expires_at > 0) {
            char date[24]; quota_date_label(p->next_reset_expires_at, date, sizeof(date));
            snprintf(row->detail, sizeof(row->detail), "最近到期 %s", date);
        } else snprintf(row->detail, sizeof(row->detail), "到期时间未提供");
    }
}
