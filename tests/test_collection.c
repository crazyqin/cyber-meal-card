#include "quota_collection.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static cJSON *report(const char *collector, const char *provider, const char *identity, int second, int resets) {
    cJSON *root = cJSON_CreateObject(), *rows = cJSON_AddArrayToObject(root, "accounts");
    cJSON_AddNumberToObject(root, "schema_version", 2);
    cJSON_AddStringToObject(root, "collector_id", collector);
    cJSON_AddStringToObject(root, "collected_at", "2026-10-16T11:00:30Z");
    cJSON *row = cJSON_CreateObject(); cJSON_AddItemToArray(rows, row);
    cJSON_AddStringToObject(row, "provider", provider);
    cJSON_AddStringToObject(row, "account_id", identity);
    cJSON_AddStringToObject(row, "name", "main");
    cJSON_AddStringToObject(row, "label", provider);
    cJSON_AddStringToObject(row, "status", "ok");
    cJSON_AddStringToObject(row, "checked_at", "2026-10-16T11:00:29Z");
    char stamp[32]; snprintf(stamp, sizeof(stamp), "2026-10-16T11:00:%02dZ", second);
    cJSON_AddStringToObject(row, "collected_at", stamp);
    cJSON *windows = cJSON_Parse("[{\"used_percent\":25,\"remaining_percent\":75,\"window_minutes\":300,\"resets_at_unix\":1792195200}]");
    cJSON_AddItemToObject(row, "windows", windows);
    cJSON *credit = cJSON_AddObjectToObject(row, "reset_credits");
    if (resets >= 0) cJSON_AddNumberToObject(credit, "available_count", resets);
    else cJSON_AddNullToObject(credit, "available_count");
    /* Synthetic expiry dates: 2030-01-01 and 2029-12-31 UTC. */
    cJSON_AddNumberToObject(credit, "latest_expires_at_unix", 1893456000);
    cJSON_AddNumberToObject(credit, "next_expires_at_unix", 1893369600);
    return root;
}
static cJSON *row(cJSON *root) { return cJSON_GetArrayItem(cJSON_GetObjectItem(root, "accounts"), 0); }
static void apply(quota_snapshot_t *s, cJSON *root, const char *collector) {
    assert(quota_collection_apply(s, root, collector) == QUOTA_OK); cJSON_Delete(root); quota_codex_sort(s);
}
int main(int argc, char **argv) {
    quota_snapshot_t s = {.unified_mode=true, .discovery_status=QUOTA_OK};
    quota_selection_t selection = {0}; quota_view_t view;
    quota_select(&s, &selection, &view); assert(view.count==1 && view.provider.status==QUOTA_EMPTY);
    apply(&s, report("pc-a", "chatgpt", "account-one", 10, 3), "pc-a");
    quota_select(&s, &selection, &view);
    assert(view.count==1 && view.provider.kind==QUOTA_CODEX && view.provider.reset_count==3);
    assert(view.provider.latest_reset_expires_at==1893456000);
    assert(view.provider.next_reset_expires_at==1893369600);
    apply(&s, report("pc-b", "chatgpt", "account-one", 20, 2), "pc-b");
    quota_select(&s, &selection, &view);
    assert(view.count==1 && !strcmp(view.provider.collector,"pc-b") && view.provider.reset_count==2);
    apply(&s, report("pc-c", "chatgpt", "other-account", 15, -1), "pc-c");
    quota_select(&s, &selection, &view); assert(view.count==2 && !strcmp(view.provider.account_id,"account-one"));
    /* A late upload from an older PC cannot overwrite the more recent source. */
    apply(&s, report("pc-a", "chatgpt", "account-one", 5, 99), "pc-a");
    quota_select(&s, &selection, &view); assert(view.provider.reset_count==2);
    /* Equal source times prefer the successful machine. */
    quota_codex_fail_collector(&s,"pc-b",QUOTA_NETWORK);
    apply(&s, report("pc-a", "chatgpt", "account-one", 20, 1), "pc-a");
    quota_select(&s, &selection, &view); assert(view.provider.status==QUOTA_OK && !strcmp(view.provider.collector,"pc-a"));
    cJSON *root=report("pc-a", "chatgpt", "account-one", 20, 1);
    cJSON_ReplaceItemInObjectCaseSensitive(row(root),"status",cJSON_CreateString("error"));
    cJSON_ReplaceItemInObjectCaseSensitive(row(root),"collected_at",cJSON_CreateNull());
    apply(&s,root,"pc-a");
    quota_select(&s,&selection,&view); assert(view.provider.updated_at>0 && view.provider.status!=QUOTA_OK);
    /* Atomic rejection: a malformed row never deletes the collector's old data. */
    root=report("pc-a", "chatgpt", "account-one", 31, 1);
    assert(quota_collection_apply(&s,root,"pc-a")==QUOTA_INVALID && s.codex_count==3); cJSON_Delete(root);
    root=report("pc-a", "chatgpt", "account-one", 20, 1);
    cJSON *credit=cJSON_GetObjectItem(row(root),"reset_credits");
    cJSON_ReplaceItemInObjectCaseSensitive(credit,"available_count",cJSON_CreateNumber(-1));
    assert(quota_collection_apply(&s,root,"pc-a")==QUOTA_INVALID); cJSON_Delete(root);
    root=report("pc-z", "custom", "custom-account", 20, -1);
    apply(&s,root,"pc-z");
    selection.index=2; selection.collector[0]=0; quota_select(&s,&selection,&view);
    assert(view.count==3);
    apply(&s,report("pc-x","custom","another-account",20,0),"pc-x");
    /* Generic points do not require a hard-coded provider or add snapshot RAM. */
    assert(sizeof(quota_points_t) <= sizeof(((quota_provider_t *)0)->balances));
    root=report("pc-p","teleagent","points-account",20,-1);
    cJSON_DeleteItemFromObjectCaseSensitive(row(root),"windows");
    cJSON_DeleteItemFromObjectCaseSensitive(row(root),"reset_credits");
    cJSON_AddItemToObject(row(root),"points",cJSON_Parse("{\"balance\":\"12345.67\",\"spent_today\":\"12.34\",\"added_today\":\"0\"}"));
    assert(quota_collection_apply(&s,root,"pc-p")==QUOTA_OK);
    quota_provider_t *points=&s.providers[QUOTA_CODEX+s.codex_count-1];
    assert(points->kind==QUOTA_POINTS && !points->balance_count && !strcmp(points->points.added_today,"0"));
    int before=s.codex_count;
    cJSON *metrics=cJSON_GetObjectItem(row(root),"points");
    cJSON_ReplaceItemInObjectCaseSensitive(metrics,"spent_today",cJSON_CreateString("-1"));
    assert(quota_collection_apply(&s,root,"pc-p")==QUOTA_INVALID && s.codex_count==before);
    cJSON_ReplaceItemInObjectCaseSensitive(metrics,"spent_today",cJSON_CreateString("0"));
    cJSON_DeleteItemFromObjectCaseSensitive(metrics,"added_today");
    assert(quota_collection_apply(&s,root,"pc-p")==QUOTA_INVALID && s.codex_count==before);
    cJSON_AddStringToObject(metrics,"added_today","100");
    cJSON_AddItemToObject(row(root),"balances",cJSON_Parse("[{\"currency\":\"CNY\",\"total\":\"1\",\"granted\":\"0\",\"topped_up\":\"1\"}]"));
    assert(quota_collection_apply(&s,root,"pc-p")==QUOTA_INVALID);
    cJSON_Delete(root);
    /* Real collector JSON can be supplied for end-to-end protocol validation. */
    if (argc==3) {
        FILE *file=fopen(argv[1],"rb"); assert(file);
        char buffer[24576]; size_t length=fread(buffer,1,sizeof(buffer),file); fclose(file);
        cJSON *live=quota_parse_document(buffer,length); assert(live);
        quota_snapshot_t fresh={.unified_mode=true};
        assert(quota_collection_apply(&fresh,live,argv[2])==QUOTA_OK); cJSON_Delete(live);
        assert(fresh.codex_count>0);
    }
    puts("Unified collection: multiple PCs, stable identities, source age, reset credits, errors and extension: PASS");
    return 0;
}
