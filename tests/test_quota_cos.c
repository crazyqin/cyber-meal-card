#include "quota_cos.h"
#include "quota_parse.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int stage;
static bool list_test;
/* Independently calculated SHA1/HMAC fixtures (Python hashlib/hmac and the
 * Tencent SDK). Assert every message/key passed to the production signer. */
static bool hash_fixture(const char *key, size_t key_len, const char *message, size_t length, char hex[41]) {
    assert(length == strlen(message));
    if (stage == 0) {
        assert(!key && key_len == 0);
        assert(!strcmp(message, list_test ? "get\n/\ndelimiter=%2F&marker=codex-usage%2Fdevice-a%2F&max-keys=16&prefix=codex-usage%2F\nhost=bucket-1250000000.cos.ap-shanghai.myqcloud.com\n" : "get\n/codex-usage/device-a/latest.json\n\nhost=bucket-1250000000.cos.ap-shanghai.myqcloud.com\n"));
        strcpy(hex, list_test ? "02010dcfe431a942c9916922af29e5d80f7808d8" : "22372be9cb7c92f5839d7bf46ec23d13281e6a21");
    } else if (stage == 1) {
        assert(key_len == 11 && !strncmp(key, "test-secret", key_len));
        assert(!strcmp(message, "1799999940;1800000300"));
        strcpy(hex, "cda3ba383c6a84c9a61d9011cf4e436d861c20ca");
    } else {
        assert(stage == 2 && key_len == 40 && !strncmp(key, "cda3ba383c6a84c9a61d9011cf4e436d861c20ca", key_len));
        assert(!strcmp(message, list_test ? "sha1\n1799999940;1800000300\n02010dcfe431a942c9916922af29e5d80f7808d8\n" : "sha1\n1799999940;1800000300\n22372be9cb7c92f5839d7bf46ec23d13281e6a21\n"));
        strcpy(hex, list_test ? "f595d9a48458308d7790eccc2a225f8a9358ca1a" : "108b6e8e7b9c7174d7d2c362e51bf4ef5ef36330");
    }
    stage++;
    return true;
}
static cJSON *document(void) {
    /* The weekly reset uses a synthetic 2030-01-08 UTC date. */
    return cJSON_Parse("{\"schema_version\":1,\"collector_id\":\"device-a\",\"collected_at\":\"2026-10-15T08:22:45Z\",\"status\":\"partial\",\"accounts\":[{\"name\":\"other\",\"status\":\"error\"},{\"name\":\"alice\",\"status\":\"ok\",\"collected_at\":\"2026-10-15T08:22:44.123456Z\",\"plan_type\":\"plus\",\"five_hour\":{\"used_percent\":21,\"remaining_percent\":79,\"window_minutes\":300,\"resets_at_unix\":1792063238},\"weekly\":{\"used_percent\":3,\"remaining_percent\":97,\"window_minutes\":10080,\"resets_at_unix\":1894060800}}]}");
}
static void replace(cJSON *o, const char *key, cJSON *value) { assert(cJSON_ReplaceItemInObjectCaseSensitive(o, key, value)); }
int main(void) {
    quota_config_t config = {.version=3, .generation=123, .ssid="network", .password="password",
        .cline="cline-key", .deepseek="deep-key", .cos_bucket="bucket-1250000000", .cos_region="ap-shanghai",
        .cos_prefix="codex-usage", .cos_secret_id="AKIDEXAMPLE", .cos_secret_key="test-secret",
        .codex={{.collector="device-a",.account="alice"},{.collector="device-b",.account="alice"}}};
    char url[512], auth[640];
    assert(quota_cos_sign_object(&config,"device-a",1800000000,hash_fixture,url,sizeof(url),auth,sizeof(auth)) && stage == 3);
    assert(!strcmp(url,"https://bucket-1250000000.cos.ap-shanghai.myqcloud.com/codex-usage/device-a/latest.json"));
    assert(!strcmp(auth,"q-sign-algorithm=sha1&q-ak=AKIDEXAMPLE&q-sign-time=1799999940;1800000300&q-key-time=1799999940;1800000300&q-header-list=host&q-url-param-list=&q-signature=108b6e8e7b9c7174d7d2c362e51bf4ef5ef36330"));
    assert(!quota_cos_sign_object(&config,"../x",1800000000,hash_fixture,url,sizeof(url),auth,sizeof(auth)));
    assert(!quota_cos_sign_object(&config,"device-a",0,hash_fixture,url,sizeof(url),auth,sizeof(auth)));
    assert(!quota_cos_sign_object(&config,"device-a",1800000000,hash_fixture,url,5,auth,sizeof(auth)));
    stage=0; list_test=true;
    assert(quota_cos_sign_list(&config,"codex-usage/device-a/",1800000000,hash_fixture,url,sizeof(url),auth,sizeof(auth)) && stage==3);
    assert(!strcmp(url,"https://bucket-1250000000.cos.ap-shanghai.myqcloud.com/?delimiter=%2F&marker=codex-usage%2Fdevice-a%2F&max-keys=16&prefix=codex-usage%2F"));
    assert(strstr(auth,"q-url-param-list=delimiter;marker;max-keys;prefix&q-signature=f595d9a48458308d7790eccc2a225f8a9358ca1a"));
    quota_provider_t p = {0};
    cJSON *root = document(); assert(root);
    assert(quota_parse_codex(root,"device-a","alice",&p) == QUOTA_OK);
    assert(p.window_count == 2 && p.windows[0].remaining == 79 && p.windows[1].remaining == 97);
    assert(p.windows[0].resets_at == 1792063238 && p.updated_at == 1792052564);
    assert(!strcmp(p.account,"alice") && !strcmp(p.plan,"plus"));
    assert(!quota_is_stale(&p,p.updated_at+900));
    assert(quota_is_stale(&p,p.updated_at+901)); /* HTTP 200 must not reset source age. */
    assert(quota_parse_codex(root,"device-b","alice",&p) == QUOTA_INVALID);
    assert(quota_parse_codex(root,"device-a","absent",&p) == QUOTA_ACCOUNT_MISSING);
    assert(quota_parse_codex(root,"device-a","other",&p) == QUOTA_COLLECTOR);
    cJSON *account=cJSON_GetArrayItem(cJSON_GetObjectItem(root,"accounts"),1);
    cJSON *five=cJSON_GetObjectItem(account,"five_hour");
    replace(five,"remaining_percent",cJSON_CreateNumber(100));
    assert(quota_parse_codex(root,"device-a","alice",&p) == QUOTA_INVALID);
    replace(five,"used_percent",cJSON_CreateNumber(0));
    replace(five,"resets_at_unix",cJSON_CreateNull());
    assert(quota_parse_codex(root,"device-a","alice",&p) == QUOTA_OK && p.windows[0].resets_at == 0);
    replace(five,"used_percent",cJSON_CreateNumber(110));
    replace(five,"remaining_percent",cJSON_CreateNumber(0));
    assert(quota_parse_codex(root,"device-a","alice",&p) == QUOTA_OK && p.windows[0].remaining == 0);
    replace(five,"window_minutes",cJSON_CreateNumber(10080));
    assert(quota_parse_codex(root,"device-a","alice",&p) == QUOTA_INVALID);
    replace(account,"five_hour",cJSON_CreateNull());
    assert(quota_parse_codex(root,"device-a","alice",&p) == QUOTA_OK && p.window_count == 1 && p.windows[0].minutes == 10080);
    replace(account,"weekly",cJSON_CreateNull());
    assert(quota_parse_codex(root,"device-a","alice",&p) == QUOTA_UNSUPPORTED);
    replace(account,"status",cJSON_CreateString("error"));
    p.updated_at=12345;
    assert(quota_parse_codex(root,"device-a","alice",&p) == QUOTA_COLLECTOR && p.updated_at == 12345);
    cJSON_Delete(root);
    root=document(); account=cJSON_GetArrayItem(cJSON_GetObjectItem(root,"accounts"),1);
    replace(account,"collected_at",cJSON_CreateString("2026-02-30T08:22:44Z"));
    assert(quota_parse_codex(root,"device-a","alice",&p) == QUOTA_INVALID);
    replace(account,"collected_at",cJSON_CreateString("2026-10-15T08:23:44Z"));
    assert(quota_parse_codex(root,"device-a","alice",&p) == QUOTA_INVALID);
    replace(account,"collected_at",cJSON_CreateString("2026-10-15T08:22:44+08:00"));
    assert(quota_parse_codex(root,"device-a","alice",&p) == QUOTA_INVALID);
    cJSON_Delete(root);
    root=document();
    cJSON_AddItemToArray(cJSON_GetObjectItem(root,"accounts"),cJSON_Duplicate(cJSON_GetArrayItem(cJSON_GetObjectItem(root,"accounts"),1),true));
    assert(quota_parse_codex(root,"device-a","alice",&p) == QUOTA_INVALID);
    cJSON_Delete(root);
    quota_config_t candidate;
    root=cJSON_Parse("{\"cos_token\":\"\"}");
    assert(quota_config_prepare(root,&config,&candidate) && candidate.version == 3 && candidate.generation == 123);
    assert(quota_cos_configured(&candidate));
    assert(!strcmp(candidate.cline,"cline-key") && !strcmp(candidate.password,"password"));
    cJSON_Delete(root);
    const char *bad[]={"{\"cos_region\":\"ap-shanghai.evil\"}","{\"cos_bucket\":\"bucket/evil\"}","{\"cos_prefix\":\"x/../y\"}","{\"cos_secret_id\":\"a&b\"}","{\"cos_token\":\"bad\\r\\nheader\"}"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        root=cJSON_Parse(bad[i]); assert(root); assert(!quota_config_prepare(root,&config,&candidate)); cJSON_Delete(root);
    }
    /* Exact legacy struct layout, independent of the new type. */
    struct {uint32_t version,generation;char ssid[33],password[65],cline[256],deepseek[256];} old={2,42,"legacy-wifi","old-password","old-cline","old-deepseek"};
    assert(quota_config_load(&old,sizeof(old),&candidate));
    assert(candidate.version == 3 && candidate.generation == 42 && !strcmp(candidate.ssid,"legacy-wifi"));
    assert(!strcmp(candidate.cline,"old-cline") && !strcmp(candidate.deepseek,"old-deepseek") && !quota_cos_configured(&candidate));
    assert(quota_config_load(&config,sizeof(config),&candidate));
    quota_config_t defaults=config;
    strcpy(defaults.cos_secret_key,"builtin-key");
    candidate.cos_secret_id[0]=0;
    assert(quota_cos_missing(&candidate)==4);
    assert(quota_config_apply_cos_defaults(&candidate,&defaults));
    assert(quota_cos_configured(&candidate)&&!strcmp(candidate.cos_secret_key,"builtin-key"));
    assert(!strcmp(candidate.ssid,"network")&&!strcmp(candidate.cline,"cline-key"));
    assert(!quota_config_apply_cos_defaults(&candidate,&config)); /* Preserve a complete override. */
    root=quota_config_export(&candidate);assert(root);
    assert(cJSON_GetObjectItemCaseSensitive(root,"backup_schema")->valueint==1);
    quota_config_t restored={0};
    assert(quota_config_prepare(root,&restored,&restored));
    assert(!strcmp(restored.password,candidate.password)&&!strcmp(restored.cos_secret_key,"builtin-key"));
    assert(!cJSON_GetObjectItemCaseSensitive(root,"generation")&&!cJSON_GetObjectItemCaseSensitive(root,"codex_1_account"));
    cJSON_Delete(root);
    assert(!quota_config_load(&old,sizeof(old)-1,&candidate) && !candidate.ssid[0]);
    /* Built-in Wi-Fi fills a blank device, preserving saved choices and COS. */
    quota_config_t fresh={.generation=17};
    assert(quota_config_apply_cos_defaults(&fresh,&defaults));
    assert(quota_config_apply_wifi_defaults(&fresh,&defaults));
    assert(!strcmp(fresh.ssid,defaults.ssid)&&!strcmp(fresh.password,defaults.password));
    assert(fresh.version==QUOTA_CONFIG_VERSION && fresh.generation==17 && quota_cos_configured(&fresh));
    strcpy(defaults.ssid,"different-network");
    assert(!quota_config_apply_wifi_defaults(&fresh,&defaults));
    assert(!strcmp(fresh.ssid,"network"));
    fresh.ssid[0]=0;
    strcpy(defaults.password,"short");
    assert(!quota_config_apply_wifi_defaults(&fresh,&defaults)&&!fresh.ssid[0]);
    defaults.password[0]=0;
    assert(quota_config_apply_wifi_defaults(&fresh,&defaults)&&!fresh.password[0]);
    fresh.ssid[0]=0;
    memset(defaults.ssid,'x',sizeof(defaults.ssid));
    assert(!quota_config_apply_wifi_defaults(&fresh,&defaults));
    config.version=99; assert(!quota_config_load(&config,sizeof(config),&candidate)); config.version=3;
    memset(config.cos_secret_key,'x',sizeof(config.cos_secret_key));
    assert(!quota_config_load(&config,sizeof(config),&candidate));
    puts("COS signing, two-account selection, source freshness and config migration: PASS");
    return 0;
}
