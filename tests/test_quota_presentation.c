#include "quota_presentation.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const int64_t NOW = 1792155600;
static void money(const char *currency, const char *amount, const char *expected) {
    char out[48]; quota_money_label(currency, amount, out, sizeof(out));
    if (strcmp(out, expected)) fprintf(stderr, "%s %s: got %s, expected %s\n", currency, amount, out, expected);
    assert(!strcmp(out, expected));
}
int main(void) {
    money("CNY", "12.345", "¥12.35"); money("CNY", "0", "¥0.00");
    money("USD", "0009.995", "$10.00"); money("CNY", "0.000015", "<¥0.01");
    money("CNY", "-0.001", ">-¥0.01"); money("CNY", "-12.999", "-¥13.00");
    money("CNY", "-0.00", "¥0.00"); money("CNY", "--", "--");
    money("CNY", "1..0", "--"); money("CNY", "1234567890.123", "¥1.23e9");
    money("USD", "999999999999.99", "$1.00e12");
    money("USD", "999999999.999", "$1.00e9");
    money("CNY", "12345678901234567890123", "¥1.23e22");
    money("CNY", "-999999999.99", "-¥1.00e9");
    quota_provider_t p = {.kind=QUOTA_CLINE, .updated_at=NOW, .window_count=3};
    strcpy(p.label,"ClinePass"); strcpy(p.source,"clinepass"); strcpy(p.account,"clinepass"); strcpy(p.plan,"ClinePass");
    p.windows[0] = (quota_window_t){.minutes=43200, .remaining=0, .resets_at=NOW+86400};
    p.windows[1] = (quota_window_t){.minutes=300, .remaining=87, .resets_at=NOW+3600};
    p.windows[2] = (quota_window_t){.minutes=10080, .remaining=99, .resets_at=NOW+86400};
    quota_presentation_t out; quota_present_provider(&p,NOW,&out);
    assert(!strcmp(out.title,"ClinePass") && !strstr(out.subtitle,"Cline"));
    assert(out.count==3 && !strcmp(out.rows[0].name,"5 小时剩余") && !strcmp(out.rows[0].value,"87%"));
    assert(!strcmp(out.rows[1].name,"每周剩余") && !strcmp(out.rows[2].name,"每月剩余"));
    assert(!strcmp(out.rows[0].detail,"1小时后重置"));
    assert(!strcmp(out.rows[2].detail,"1天后重置"));
    assert(out.rows[0].progress && out.rows[2].warning);
    strcpy(p.account,"work"); strcpy(p.plan,"Pro");
    quota_present_provider(&p,NOW,&out); assert(!strcmp(out.subtitle,"work · Pro"));
    strcpy(p.plan,"work"); quota_present_provider(&p,NOW,&out); assert(!strcmp(out.subtitle,"work"));

    memset(&p,0,sizeof(p)); p.kind=QUOTA_DEEPSEEK; p.updated_at=NOW; p.balance_count=1;
    strcpy(p.label,"DeepSeek"); strcpy(p.account,"deepseek"); strcpy(p.plan,"API");
    p.balances[0] = (quota_balance_t){.currency="CNY", .total="12.345", .granted="2.345", .topped_up="10"};
    quota_present_provider(&p,NOW,&out);
    assert(out.count==3 && !strcmp(out.subtitle,"余额明细"));
    assert(!strcmp(out.rows[0].name,"账户余额") && !strcmp(out.rows[0].value,"¥12.35"));
    assert(!strcmp(out.rows[1].name,"充值余额") && !strcmp(out.rows[1].value,"¥10.00"));
    assert(!strcmp(out.rows[2].name,"赠送余额") && !strcmp(out.rows[2].value,"¥2.35"));
    p.balance_count=2; p.balances[1] = (quota_balance_t){.currency="USD", .total="3", .granted="0", .topped_up="3"};
    quota_present_provider(&p,NOW,&out);
    assert(out.count==2 && !strcmp(out.rows[1].value,"$3.00"));
    assert(strstr(out.rows[0].detail,"充值 ¥10.00") && strstr(out.rows[1].detail,"赠送 $0.00"));

    p.kind=QUOTA_TOKENHUB; strcpy(p.label,"TokenHub"); strcpy(p.account,"tokenhub"); strcpy(p.plan,"CNY / estimated");
    p.balances[0] = (quota_balance_t){.currency="CNY", .total="80.5", .granted="100", .topped_up="19.5"};
    p.balances[1] = (quota_balance_t){.currency="CNY", .total="0"};
    quota_present_provider(&p,NOW,&out);
    assert(out.count==3 && !strstr(out.subtitle,"CNY") && !strstr(out.subtitle,"TokenHub"));
    assert(!strcmp(out.rows[0].value,"¥80.50") && strstr(out.rows[0].detail,"估算"));
    assert(!strcmp(out.rows[1].name,"本月消费") && !strcmp(out.rows[1].value,"¥19.50"));
    assert(!strcmp(out.rows[2].name,"今日消费") && !strcmp(out.rows[2].value,"¥0.00") && !out.rows[2].detail[0]);
    strcpy(p.balances[0].total,"-1.25"); quota_present_provider(&p,NOW,&out);
    assert(out.rows[0].warning && !strcmp(out.rows[0].value,"-¥1.25"));
    strcpy(p.balances[0].granted,"不限额"); strcpy(p.balances[0].total,"--");
    quota_present_provider(&p,NOW,&out); assert(!strcmp(out.rows[0].value,"不限额") && !out.rows[0].warning);

    memset(&p,0,sizeof(p)); p.kind=QUOTA_CODEX; p.updated_at=NOW; p.reset_count=-1;
    strcpy(p.source,"chatgpt"); strcpy(p.label,"ChatGPT"); strcpy(p.account,"chatgpt"); strcpy(p.plan,"Plus");
    p.window_count=2; p.windows[0]=(quota_window_t){.minutes=300,.remaining=100};
    p.windows[1]=(quota_window_t){.minutes=10080,.remaining=75};
    quota_present_provider(&p,NOW,&out);
    assert(!strcmp(out.subtitle,"Plus") && out.count==3 && !strcmp(out.rows[2].value,"--"));
    assert(!strcmp(out.rows[0].detail,"重置时间未提供"));
    /* A newer grant can expire later than another currently available credit. */
    p.reset_count=3; p.latest_reset_expires_at=NOW+86400*7; p.next_reset_expires_at=NOW+86400*2;
    quota_present_provider(&p,NOW,&out);
    assert(!strcmp(out.rows[2].detail,"最近到期 10/18 21:00"));
    /* Missing earliest expiry must not fall back to the latest grant's expiry. */
    p.next_reset_expires_at=0;
    quota_present_provider(&p,NOW,&out); assert(!strcmp(out.rows[2].detail,"到期时间未提供"));
    p.reset_count=0; p.next_reset_expires_at=NOW+86400*2;
    quota_present_provider(&p,NOW,&out);
    assert(!strcmp(out.rows[2].value,"0 次") && !strcmp(out.rows[2].detail,"暂无可用重置"));
    p.updated_at=0; quota_present_provider(&p,NOW,&out); assert(out.count==0);
    memset(&p,0,sizeof(p)); p.kind=QUOTA_POINTS; p.updated_at=NOW;
    strcpy(p.label,"TeleAgent"); strcpy(p.source,"teleagent"); strcpy(p.account,"teleagent");
    p.points=(quota_points_t){.balance="12345.67",.spent_today="12.34",.added_today="0"};
    quota_present_provider(&p,NOW,&out);
    assert(out.count==3 && !strcmp(out.subtitle,"积分明细") && !strcmp(out.title,"TeleAgent"));
    assert(!strcmp(out.rows[0].name,"积分余额") && !strcmp(out.rows[0].value,"12345.67"));
    assert(!strcmp(out.rows[1].name,"今日消耗") && !strcmp(out.rows[1].value,"12.34"));
    assert(!strcmp(out.rows[2].name,"今日新增") && !strcmp(out.rows[2].value,"0.00"));
    assert(!strcmp(out.rows[0].detail,"积分") && !out.rows[0].progress);
    char date[24]; quota_date_label(NOW,date,sizeof(date)); assert(!strcmp(date,"10/16 21:00"));
    puts("Quota presentation: PASS (name deduplication, currencies, precision, windows, reset credits)");
}
