#include "quota_parse.h"
#include "quota_cos.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static cJSON *report(void) {
    return cJSON_Parse("{\"schema_version\":1,\"source\":\"telecom-tokenhub\",\"collected_at\":\"2026-10-15T15:25:41Z\",\"checked_at\":\"2026-10-15T15:25:42Z\",\"status\":\"ok\",\"data\":{\"currency\":\"CNY\",\"remaining_is_estimate\":true,\"allocation_yuan\":\"1000.00\",\"estimated_remaining_yuan\":\"993.09\",\"month_spent_yuan\":\"6.91\",\"range_spent_yuan\":\"2.31\"}}");
}
static void replace(cJSON *o, const char *key, cJSON *value) {
    assert(cJSON_ReplaceItemInObjectCaseSensitive(o,key,value));
}
static int stage;
/* Fixtures computed independently with Python hashlib/hmac, fixed test key. */
static bool hash_fixture(const char *key, size_t key_len, const char *message, size_t len, char hex[41]) {
    assert(len==strlen(message));
    if(stage==0) {
        assert(!key && !key_len);
        assert(!strcmp(message,"get\n/codex-usage/tokenhub.json\n\nhost=bucket-1250000000.cos.ap-shanghai.myqcloud.com\n"));
        strcpy(hex,"228bde023d351471a8298e4c0444a217340b131f");
    } else if(stage==1) {
        assert(key_len==11 && !strcmp(key,"test-secret"));
        assert(!strcmp(message,"1799999940;1800000300"));
        strcpy(hex,"cda3ba383c6a84c9a61d9011cf4e436d861c20ca");
    } else {
        assert(stage==2 && key_len==40 && !strcmp(key,"cda3ba383c6a84c9a61d9011cf4e436d861c20ca"));
        assert(!strcmp(message,"sha1\n1799999940;1800000300\n228bde023d351471a8298e4c0444a217340b131f\n"));
        strcpy(hex,"1bde0a614eb42d503fb471fb6e42314db2d13836");
    }
    stage++;
    return true;
}
int main(void) {
    cJSON *root=report(); assert(root);
    quota_provider_t p={0};
    assert(quota_parse_tokenhub(root,&p)==QUOTA_OK);
    assert(p.balance_count==2 && !strcmp(p.balances[0].total,"993.09"));
    assert(!strcmp(p.balances[0].granted,"1000.00") && !strcmp(p.balances[0].topped_up,"6.91"));
    assert(!strcmp(p.balances[1].total,"2.31"));
    assert(!quota_is_stale(&p,p.updated_at+900) && quota_is_stale(&p,p.updated_at+901));
    quota_provider_t cached=p;
    replace(root,"status",cJSON_CreateString("error"));
    assert(quota_parse_tokenhub(root,&p)==QUOTA_COLLECTOR && !memcmp(&p,&cached,sizeof(p)));
    replace(root,"status",cJSON_CreateString("ok"));
    cJSON *data=cJSON_GetObjectItem(root,"data");
    replace(data,"estimated_remaining_yuan",cJSON_CreateString("-0.01"));
    assert(quota_parse_tokenhub(root,&p)==QUOTA_OK && !strcmp(p.balances[0].total,"-0.01"));
    const char *bad[]={"NaN","100.001","1e3",".00","-1.00","1.00extra"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        replace(data,"month_spent_yuan",cJSON_CreateString(bad[i]));
        assert(quota_parse_tokenhub(root,&p)==QUOTA_INVALID);
    }
    replace(data,"month_spent_yuan",cJSON_CreateString("0.00"));
    replace(data,"allocation_yuan",cJSON_CreateNull());
    replace(data,"estimated_remaining_yuan",cJSON_CreateNull());
    assert(quota_parse_tokenhub(root,&p)==QUOTA_OK && !strcmp(p.balances[0].total,"--"));
    replace(data,"currency",cJSON_CreateString("USD"));
    assert(quota_parse_tokenhub(root,&p)==QUOTA_INVALID);
    replace(data,"currency",cJSON_CreateString("CNY"));
    replace(root,"collected_at",cJSON_CreateString("2026-02-30T00:00:00Z"));
    assert(quota_parse_tokenhub(root,&p)==QUOTA_INVALID);
    cJSON_Delete(root);
    quota_snapshot_t snapshot={.codex_count=2};
    snapshot.providers[QUOTA_TOKENHUB]=cached;
    quota_selection_t selection={.index=QUOTA_TOKENHUB};quota_view_t view;
    quota_select(&snapshot,&selection,&view);
    assert(view.count==5 && view.index==QUOTA_TOKENHUB && !strcmp(view.provider.balances[0].total,"993.09"));
    snapshot.codex_count=3;quota_select(&snapshot,&selection,&view);
    assert(view.count==6 && view.index==QUOTA_TOKENHUB);
    quota_config_t cfg={.cos_bucket="bucket-1250000000",.cos_region="ap-shanghai",.cos_prefix="codex-usage",
                         .cos_secret_id="AKIDEXAMPLE",.cos_secret_key="test-secret"};
    char url[512],auth[704];
    assert(quota_cos_sign_tokenhub(&cfg,1800000000,hash_fixture,url,sizeof(url),auth,sizeof(auth)) && stage==3);
    assert(!strcmp(url,"https://bucket-1250000000.cos.ap-shanghai.myqcloud.com/codex-usage/tokenhub.json"));
    assert(strstr(auth,"q-signature=1bde0a614eb42d503fb471fb6e42314db2d13836"));
    assert(!quota_cos_sign_tokenhub(&cfg,0,hash_fixture,url,sizeof(url),auth,sizeof(auth)));
    puts("TokenHub: decimal amounts, unlimited allocation, stale source time, errors, page navigation and COS signature: PASS");
    return 0;
}
