#include "quota_model.h"
#ifdef QUOTA_PARSE_TEST
#include "quota_parse.h"
#include "quota_config.h"
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#ifdef QUOTA_PARSE_TEST
static cJSON *json(const char *text) { cJSON *j = cJSON_Parse(text); assert(j); return j; }
#endif
int main(void) {
    float remaining;
    assert(!quota_rotation_due(9999999,0,0));
    assert(quota_rotation_due(10000000,0,0));
    assert(!quota_rotation_due(29999999,0,30000000));
    assert(quota_rotation_due(30000000,0,30000000));
    assert(!quota_rotation_due(39999999,30000000,30000000));
    assert(quota_rotation_due(40000000,30000000,30000000));
    assert(!quota_rotation_due(30000000,40000000,0));
    assert(quota_sync_wait_seconds(10000,0,true)==0);
    assert(quota_sync_wait_seconds(9999,0,true)==1);
    assert(quota_sync_wait_seconds(0,0,true)==10);
    assert(quota_sync_wait_seconds(10000,0,false)==50);
    assert(quota_sync_wait_seconds(60000,0,false)==0);
    assert(quota_remaining(0, &remaining) && remaining == 100);
    assert(quota_remaining(100, &remaining) && remaining == 0);
    assert(!quota_remaining(NAN, &remaining));
    assert(!quota_remaining(-1, &remaining));
    assert(!quota_remaining(101, &remaining));
    char percent[12];
    quota_percent_label(100, percent, sizeof(percent)); assert(!strcmp(percent, "100%"));
    quota_percent_label(0, percent, sizeof(percent)); assert(!strcmp(percent, "0%"));
    quota_percent_label(87.4f, percent, sizeof(percent)); assert(!strcmp(percent, "87%"));
    quota_percent_label(87.5f, percent, sizeof(percent)); assert(!strcmp(percent, "88%"));
    quota_percent_label(99.9f, percent, sizeof(percent)); assert(!strcmp(percent, "100%"));
    quota_percent_label(NAN, percent, sizeof(percent)); assert(!strcmp(percent, "--"));
    quota_percent_label(INFINITY, percent, sizeof(percent)); assert(!strcmp(percent, "--"));
    quota_percent_label(-1, percent, sizeof(percent)); assert(!strcmp(percent, "--"));
    quota_percent_label(101, percent, sizeof(percent)); assert(!strcmp(percent, "--"));
    assert(quota_next_provider(0, -1, 4) == 3 && quota_next_provider(3, 1, 4) == 0);
    quota_provider_t p = {0};
#ifdef QUOTA_PARSE_TEST
    char nested[42]; memset(nested, '[', 20); memset(nested + 20, ']', 20); nested[40] = 0;
    assert(!quota_parse_document(nested, strlen(nested)));
    assert(!quota_parse_document("{} {}", 5));
    assert(!quota_parse_document("{\"broken\":", 10));
    /* Exact length is checked separately, never inferred from a NUL prefix. */
    const char *safe_document = "{\"text\":\"[[{\\\"quoted\\\"}\"} \n";
    cJSON *document = quota_parse_document(safe_document, strlen(safe_document));
    assert(document); cJSON_Delete(document);
    const char nul_document[] = {'{', '}', 0, 'x'};
    assert(!quota_parse_document(nul_document, sizeof(nul_document)));
    cJSON *j = json("{\"success\":true,\"data\":{\"limits\":[{\"type\":\"five_hour\",\"percentUsed\":13,\"resetsAt\":\"2026-10-11T15:37:19.56967282Z\"},{\"type\":\"weekly\",\"percentUsed\":5},{\"type\":\"monthly\",\"percentUsed\":2},{\"type\":\"future_window\",\"percentUsed\":50}]}}");
    assert(quota_parse_cline(j, &p));
    assert(p.window_count == 3 && p.windows[0].remaining == 87 && p.windows[1].remaining == 95);
    assert(p.windows[0].resets_at == 1791733039 && p.windows[1].resets_at == 0);
    cJSON_Delete(j);
    j = json("{\"success\":true,\"data\":{\"limits\":[]}}"); assert(!quota_parse_cline(j, &p)); cJSON_Delete(j);
    j = json("{\"success\":false,\"data\":{\"limits\":[]}}"); assert(!quota_parse_cline(j, &p)); cJSON_Delete(j);
    j = json("{\"success\":true,\"data\":{\"limits\":[{\"type\":\"five_hour\",\"percentUsed\":0},{\"type\":\"five_hour\",\"percentUsed\":0}]}}");
    assert(!quota_parse_cline(j, &p)); cJSON_Delete(j);
    j = json("{\"success\":true,\"data\":{\"limits\":[{\"type\":\"five_hour\",\"percentUsed\":0,\"resetsAt\":null}]}}");
    assert(quota_parse_cline(j, &p) && p.windows[0].remaining == 100 && p.windows[0].resets_at == 0);
    char detail[64];
    quota_window_detail_label(&p.windows[0], 1800000000, detail, sizeof(detail));
    assert(!strcmp(detail, "尚未开始计时")); cJSON_Delete(j);
    j = json("{\"is_available\":true,\"balance_infos\":[{\"currency\":\"CNY\",\"total_balance\":\"110.00\",\"granted_balance\":\"10.00\",\"topped_up_balance\":\"100.00\"},{\"currency\":\"USD\",\"total_balance\":\"2.00\",\"granted_balance\":\"0.00\",\"topped_up_balance\":\"2.00\"}]}");
    assert(quota_parse_deepseek(j, &p)); assert(p.balance_count == 2 && !strcmp(p.balances[0].total, "110.00")); cJSON_Delete(j);
    j = json("{\"is_available\":false,\"balance_infos\":[{\"currency\":\"CNY\",\"total_balance\":\"0\",\"granted_balance\":\"0\",\"topped_up_balance\":\"0\"}]}");
    assert(quota_parse_deepseek(j, &p) && !p.available && !strcmp(p.balances[0].total, "0")); cJSON_Delete(j);
    j = json("{\"is_available\":true,\"balance_infos\":[{\"currency\":\"CNY\",\"total_balance\":\"oops\"}]}");
    assert(!quota_parse_deepseek(j, &p)); cJSON_Delete(j);
    j = json("{\"is_available\":true,\"balance_infos\":[]}"); assert(!quota_parse_deepseek(j, &p)); cJSON_Delete(j);
    quota_config_t current = {.version = 2, .generation = 42, .ssid = "old-network", .password = "old-password", .cline = "old-key"};
    quota_config_t candidate;
    j = json("{\"ssid\":\"new-network\",\"password\":\"new-password\",\"deepseek\":\"new-key\"}");
    assert(quota_config_prepare(j, &current, &candidate));
    assert(!strcmp(candidate.ssid, "new-network") && !strcmp(current.ssid, "old-network"));
    assert(!strcmp(candidate.cline, "old-key") && candidate.generation == 42); cJSON_Delete(j);
    j = json("{\"ssid\":\"\"}"); assert(!quota_config_prepare(j, &current, &candidate)); cJSON_Delete(j);
    j = json("{\"password\":\"short\"}"); assert(!quota_config_prepare(j, &current, &candidate)); cJSON_Delete(j);
    j = json("{\"password\":\"\"}"); assert(quota_config_prepare(j, &current, &candidate) && !candidate.password[0]); cJSON_Delete(j);
    j = json("{\"cline\":\"bad\\r\\nHeader: injected\"}"); assert(!quota_config_prepare(j, &current, &candidate)); cJSON_Delete(j);
    j = json("{\"cline\":123}"); assert(!quota_config_prepare(j, &current, &candidate)); cJSON_Delete(j);
    j = cJSON_CreateObject();
    char too_long[257]; memset(too_long, 'x', 256); too_long[256] = 0;
    cJSON_AddStringToObject(j, "cline", too_long); assert(!quota_config_prepare(j, &current, &candidate)); cJSON_Delete(j);
#endif
    p.status = QUOTA_OK; p.updated_at = 1800000000;
    assert(!quota_is_stale(&p, 1800000899)); assert(quota_is_stale(&p, 1800000901));
    assert(quota_is_stale(&p, 1799999999)); p.status = QUOTA_AUTH; assert(quota_is_stale(&p, 1800000001));
    char label[48]; quota_reset_label(0, 1800000000, label, sizeof(label)); assert(!strcmp(label, "--"));
    quota_reset_label(1800000000, 1800000001, label, sizeof(label)); assert(!strcmp(label, "待刷新"));
    quota_window_t window = {.minutes = 300, .remaining = 100};
    quota_window_detail_label(&window, 1800000000, label, sizeof(label)); assert(!strcmp(label, "尚未开始计时"));
    window.remaining = 99.9f;
    quota_window_detail_label(&window, 1800000000, label, sizeof(label)); assert(!strcmp(label, "重置时间未提供"));
    window.remaining = 0;
    quota_window_detail_label(&window, 1800000000, label, sizeof(label)); assert(!strcmp(label, "重置时间未提供"));
    window.resets_at = 1800005461;
    quota_window_detail_label(&window, 1800000000, label, sizeof(label)); assert(!strcmp(label, "重置 1时32分"));
    window.remaining = 100;
    quota_window_detail_label(&window, 1800000000, label, sizeof(label)); assert(!strcmp(label, "重置 1时32分"));
    quota_window_detail_label(&window, 1800006000, label, sizeof(label)); assert(!strcmp(label, "重置 待刷新"));
    quota_window_detail_label(&window, 0, label, sizeof(label)); assert(!strcmp(label, "等待校时"));
    window.minutes = 10080; window.remaining = 100; window.resets_at = 0;
    quota_window_detail_label(&window, 1800000000, label, sizeof(label)); assert(!strcmp(label, "重置时间未提供"));
    puts("Quota parsing, navigation and freshness: PASS");
    return 0;
}
