#include "quota_discovery.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static cJSON *report(const char *collector, int count) {
    cJSON *root=cJSON_CreateObject(),*accounts=cJSON_CreateArray();
    cJSON_AddNumberToObject(root,"schema_version",1);
    cJSON_AddStringToObject(root,"collector_id",collector);
    cJSON_AddStringToObject(root,"collected_at","2026-10-15T08:22:45Z");
    cJSON_AddItemToObject(root,"accounts",accounts);
    for(int i=0;i<count;i++) {
        cJSON *account=cJSON_Parse("{\"status\":\"ok\",\"collected_at\":\"2026-10-15T08:22:44Z\",\"plan_type\":\"plus\",\"five_hour\":{\"used_percent\":21,\"remaining_percent\":79,\"window_minutes\":300,\"resets_at_unix\":1792063238},\"weekly\":null}");
        char alias[32];snprintf(alias,sizeof(alias),"account-%02d",i);
        cJSON_AddStringToObject(account,"name",alias);
        cJSON_AddItemToArray(accounts,account);
    }
    return root;
}
static void apply(quota_snapshot_t *s,const char *collector,int n) {
    cJSON *root=report(collector,n);
    assert(quota_codex_apply_report(s,root,collector)==QUOTA_OK);cJSON_Delete(root);
    quota_codex_sort(s);
}
int main(void) {
    const char *xml="<?xml version=\"1.0\" encoding=\"UTF-8\"?><ListBucketResult xmlns=\"http://cos.example/doc/\"><Prefix>codex-usage/</Prefix><Marker/><MaxKeys>16</MaxKeys><IsTruncated>true</IsTruncated><NextMarker>codex-usage/b/</NextMarker><CommonPrefixes><Prefix>codex-usage/a/</Prefix></CommonPrefixes><CommonPrefixes><Prefix>codex-usage/b/</Prefix></CommonPrefixes><Contents><Key>codex-usage/ignore.json</Key><Size>4</Size></Contents></ListBucketResult>";
    quota_cos_page_t page;
    assert(quota_cos_parse_list(xml,strlen(xml),"codex-usage",&page));
    assert(page.count==2 && page.truncated && !strcmp(page.collectors[1],"b") && !strcmp(page.next_marker,"codex-usage/b/"));
    const char *bad[]={"<ListBucketResult><IsTruncated>true</IsTruncated></ListBucketResult>","<!DOCTYPE x><ListBucketResult><IsTruncated>false</IsTruncated></ListBucketResult>","<ListBucketResult><IsTruncated>false</IsTruncated><CommonPrefixes><Prefix>elsewhere/a/</Prefix></CommonPrefixes></ListBucketResult>","<ListBucketResult><IsTruncated>false</IsTruncated><CommonPrefixes><Prefix>codex-usage/../</Prefix></CommonPrefixes></ListBucketResult>","<ListBucketResult><IsTruncated>false</IsTruncated></Other>","<ListBucketResult><IsTruncated>false</IsTruncated></ListBucketResult>garbage","<ListBucketResult><IsTruncated>false</IsTruncated><IsTruncated>true</IsTruncated></ListBucketResult>"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) assert(!quota_cos_parse_list(bad[i],strlen(bad[i]),"codex-usage",&page));
    xml="<ListBucketResult><IsTruncated>false</IsTruncated></ListBucketResult>";
    assert(quota_cos_parse_list(xml,strlen(xml),"codex-usage",&page)&&page.count==0&&!page.truncated);
    quota_snapshot_t s={.discovery_status=QUOTA_OK};
    quota_selection_t selection={.index=QUOTA_CODEX};quota_view_t view;
    quota_select(&s,&selection,&view);assert(view.count==QUOTA_BASE_PROVIDERS+1&&view.provider.status==QUOTA_EMPTY);
    apply(&s,"pc-b",1);apply(&s,"pc-c",1);assert(s.codex_count==2);
    quota_select(&s,&selection,&view);assert(view.count==QUOTA_BASE_PROVIDERS+2 && !strcmp(view.provider.collector,"pc-b"));
    apply(&s,"pc-a",1);assert(s.codex_count==3);
    quota_select(&s,&selection,&view);assert(view.count==QUOTA_BASE_PROVIDERS+3 && selection.index==QUOTA_CODEX+1 && !strcmp(view.provider.collector,"pc-b"));
    apply(&s,"pc-c",2);assert(s.codex_count==4); /* Multiple accounts in one collector. */
    quota_select(&s,&selection,&view);assert(view.count==QUOTA_BASE_PROVIDERS+4 && !strcmp(view.provider.collector,"pc-b"));
    assert(quota_next_provider(5,1,6)==0 && quota_next_provider(0,-1,6)==5);
    cJSON *root=report("pc-b",1);
    cJSON *account=cJSON_GetArrayItem(cJSON_GetObjectItem(root,"accounts"),0);
    cJSON_ReplaceItemInObjectCaseSensitive(account,"status",cJSON_CreateString("error"));
    assert(quota_codex_apply_report(&s,root,"pc-b")==QUOTA_OK);cJSON_Delete(root);quota_codex_sort(&s);
    quota_select(&s,&selection,&view);assert(view.provider.status==QUOTA_COLLECTOR && view.provider.windows[0].remaining==79 && view.provider.updated_at==1792052564);
    root=report("pc-b",2);account=cJSON_GetArrayItem(cJSON_GetObjectItem(root,"accounts"),1);
    cJSON_ReplaceItemInObjectCaseSensitive(account,"name",cJSON_CreateString("account-00"));
    assert(quota_codex_apply_report(&s,root,"pc-b")==QUOTA_INVALID && s.codex_count==4);cJSON_Delete(root);
    quota_codex_fail_collector(&s,"pc-a",QUOTA_NETWORK);
    assert(s.providers[QUOTA_CODEX].status==QUOTA_NETWORK && s.providers[QUOTA_CODEX].updated_at==1792052564);
    apply(&s,"pc-c",1);assert(s.codex_count==3); /* Deleted account disappears. */
    const char keep[][65]={"pc-b","pc-c"};quota_codex_prune(&s,keep,2);assert(s.codex_count==2);
    quota_codex_remove_collector(&s,"pc-b");assert(s.codex_count==1);
    quota_select(&s,&selection,&view);assert(view.count==QUOTA_BASE_PROVIDERS+1 && selection.index==QUOTA_CODEX && !strcmp(view.provider.collector,"pc-c"));
    apply(&s,"pc-c",0);assert(s.codex_count==0);quota_select(&s,&selection,&view);assert(view.provider.status==QUOTA_EMPTY);
    apply(&s,"pc-a",QUOTA_MAX_CODEX);assert(s.codex_count==32);
    root=report("pc-b",1);assert(quota_codex_apply_report(&s,root,"pc-b")==QUOTA_LIMIT && s.codex_count==32);cJSON_Delete(root);
    root=report("pc-a",33);assert(quota_codex_apply_report(&s,root,"pc-a")==QUOTA_LIMIT && s.codex_count==32);cJSON_Delete(root);
    selection.index=QUOTA_PROVIDERS-1;selection.collector[0]=0;quota_select(&s,&selection,&view);assert(view.count==QUOTA_PROVIDERS&&view.index==QUOTA_PROVIDERS-1);
    puts("Automatic discovery: XML pagination, 2->3->4 accounts, removal, identity, errors and capacity: PASS");
    return 0;
}
