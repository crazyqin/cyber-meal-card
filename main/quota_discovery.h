#pragma once
#include "quota_cos.h"
#include "quota_parse.h"
typedef struct {
    char collectors[QUOTA_COS_LIST_SIZE][65];
    int count;
    bool truncated;
    char next_marker[512];
} quota_cos_page_t;
bool quota_cos_parse_list(const char *xml, size_t length, const char *prefix, quota_cos_page_t *page);
quota_status_t quota_codex_apply_report(quota_snapshot_t *snapshot, const cJSON *root, const char *collector);
void quota_codex_fail_collector(quota_snapshot_t *snapshot, const char *collector, quota_status_t status);
void quota_codex_remove_collector(quota_snapshot_t *snapshot, const char *collector);
void quota_codex_prune(quota_snapshot_t *snapshot, const char collectors[][65], int count);
void quota_codex_sort(quota_snapshot_t *snapshot);
