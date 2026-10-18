/* Real LVGL widgets, fonts and RGB565 drawing; the data below is synthetic. */
#include "quota_ui.h"
#include "src/misc/lv_text_private.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Report failures to the terminal, including on Windows without an assert UI. */
#undef assert
#define assert(condition) do { if (!(condition)) { fprintf(stderr,"Check failed at %s:%d: %s\n",__FILE__,__LINE__,#condition); exit(2); } } while (0)

static uint16_t s_pixels[240 * 320], s_buffer[240 * 20];
static unsigned s_labels;
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
    const uint16_t *source = (uint16_t *)pixels;
    for (int y = area->y1; y <= area->y2; y++) for (int x = area->x1; x <= area->x2; x++)
        s_pixels[y * 240 + x] = *source++;
    lv_display_flush_ready(display);
}
static void check(lv_obj_t *obj, bool hidden) {
    hidden = hidden || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
    if (!hidden && lv_obj_get_parent(obj)) {
        /* Progress tracks and battery fills must stay inside their cards too. */
        lv_area_t area, parent;
        lv_obj_get_coords(obj, &area);
        lv_obj_get_coords(lv_obj_get_parent(obj), &parent);
        assert(area.x1 >= 0 && area.y1 >= 0 && area.x2 < 240 && area.y2 < 320);
        assert(area.x1 >= parent.x1 && area.y1 >= parent.y1 && area.x2 <= parent.x2 && area.y2 <= parent.y2);
    }
    if (lv_obj_check_type(obj, &lv_label_class) && !hidden) {
        s_labels++;
        const lv_font_t *font = lv_obj_get_style_text_font(obj, LV_PART_MAIN);
        const char *text = lv_label_get_text(obj);
        uint32_t index = 0;
        while (text[index]) {
            uint32_t cp = lv_text_encoded_next(text, &index);
            if (cp == '\n') continue;
            lv_font_glyph_dsc_t glyph;
            if (!lv_font_get_glyph_dsc(font, &glyph, cp, 0) || glyph.is_placeholder) {
                fprintf(stderr,"Missing label glyph U+%04x: %s\n",(unsigned)cp,text); abort();
            }
        }
        lv_label_long_mode_t mode = lv_label_get_long_mode(obj);
        assert(mode != LV_LABEL_LONG_MODE_SCROLL && mode != LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        if (mode == LV_LABEL_LONG_MODE_CLIP) {
            lv_point_t size; lv_text_get_size(&size,text,font,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
            assert(size.x <= lv_obj_get_width(obj));
        } else if (mode == LV_LABEL_LONG_MODE_WRAP) {
            lv_point_t size; lv_text_get_size(&size,text,font,0,0,lv_obj_get_width(obj),LV_TEXT_FLAG_NONE);
            assert(size.y <= lv_obj_get_height(obj));
        }
    }
    for (unsigned i=0; i<lv_obj_get_child_count(obj); i++) check(lv_obj_get_child(obj,(int)i),hidden);
}
static lv_obj_t *label_with_text(lv_obj_t *obj, const char *value) {
    if (lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN)) return NULL;
    if (lv_obj_check_type(obj,&lv_label_class) && !strcmp(lv_label_get_text(obj),value)) return obj;
    for (unsigned i=0;i<lv_obj_get_child_count(obj);i++) {
        lv_obj_t *found=label_with_text(lv_obj_get_child(obj,(int)i),value);
        if (found) return found;
    }
    return NULL;
}
static void capture(lv_obj_t *screen, const char *folder, const char *name) {
    printf("Rendering %s\n",name);
    lv_obj_update_layout(screen);
    check(screen,false);
    lv_refr_now(NULL);
    char path[1024]; snprintf(path,sizeof(path),"%s/%s.ppm",folder,name);
    FILE *file=fopen(path,"wb"); assert(file);
    fprintf(file,"P6\n240 320\n255\n");
    for (unsigned i=0;i<240*320;i++) {
        uint16_t p=s_pixels[i];
        unsigned char rgb[3]={(unsigned char)(((p>>11)&31)*255/31), (unsigned char)(((p>>5)&63)*255/63), (unsigned char)((p&31)*255/31)};
        assert(fwrite(rgb,1,3,file)==3);
    }
    fclose(file);
}
static void save(lv_obj_t *screen, const char *folder, const char *name, quota_view_t *view,
                 quota_connection_t *net, int64_t now) {
    quota_ui_render(view,net,78,false,now);
    capture(screen,folder,name);
}
int main(int argc,char **argv) {
    setbuf(stdout,NULL); setbuf(stderr,NULL);
    assert(argc==2);
    lv_init();
    lv_display_t *display=lv_display_create(240,320); assert(display);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,s_buffer,NULL,sizeof(s_buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    lv_obj_t *screen=quota_ui_create(); assert(screen);
    assert(quota_ui_missing_glyphs()==0);
    const int64_t now=1792155600;
    quota_view_t view={.index=0,.count=6,.discovery_status=QUOTA_OK};
    quota_connection_t net={.wifi=true,.clock_ready=true};
    quota_provider_t *p=&view.provider;
    p->status=QUOTA_OK; p->updated_at=now; p->kind=QUOTA_CLINE; p->window_count=3;
    strcpy(p->label,"ClinePass"); strcpy(p->account,"clinepass"); strcpy(p->plan,"ClinePass");
    p->windows[0]=(quota_window_t){.minutes=300,.remaining=87,.resets_at=now+7200};
    p->windows[1]=(quota_window_t){.minutes=10080,.remaining=64,.resets_at=now+86400*3+3600};
    p->windows[2]=(quota_window_t){.minutes=43200,.remaining=91,.resets_at=now+86400*12};
    save(screen,argv[1],"clinepass",&view,&net,now);
    p->window_count=2;
    save(screen,argv[1],"two-windows",&view,&net,now);
    assert(label_with_text(screen,"暂无数据"));
    assert(!label_with_text(screen,"91%")); /* Missing slots must not retain another page's value. */
    p->kind=QUOTA_DEEPSEEK; p->window_count=0; p->balance_count=1; p->available=true; view.index=1;
    strcpy(p->label,"DeepSeek"); strcpy(p->account,"deepseek"); strcpy(p->plan,"API");
    p->balances[0]=(quota_balance_t){.currency="CNY",.total="126.38",.topped_up="120",.granted="6.38"};
    save(screen,argv[1],"deepseek",&view,&net,now);
    lv_obj_t *primary=label_with_text(screen,"¥126.38"); assert(primary);
    assert(lv_obj_get_style_text_font(primary,LV_PART_MAIN)->line_height==lv_font_montserrat_36.line_height);
    p->balance_count=2; p->balances[1]=(quota_balance_t){.currency="USD",.total="12.02",.topped_up="10",.granted="2.02"};
    save(screen,argv[1],"dual-currency",&view,&net,now);
    p->available=false;
    save(screen,argv[1],"dual-unavailable",&view,&net,now);
    assert(label_with_text(screen,"不可用"));
    p->available=true;
    p->kind=QUOTA_TOKENHUB; view.index=2;
    strcpy(p->label,"TokenHub"); strcpy(p->account,"tokenhub"); strcpy(p->plan,"CNY / estimated");
    p->balances[0]=(quota_balance_t){.currency="CNY",.total="82.64",.granted="100",.topped_up="17.36"};
    p->balances[1]=(quota_balance_t){.currency="CNY",.total="1.28"};
    save(screen,argv[1],"tokenhub",&view,&net,now);
    strcpy(p->balances[0].granted,"不限额"); strcpy(p->balances[0].total,"--");
    save(screen,argv[1],"unlimited",&view,&net,now);
    p->kind=QUOTA_CODEX; p->balance_count=0; p->window_count=2; view.index=3;
    strcpy(p->label,"ChatGPT"); strcpy(p->source,"chatgpt"); strcpy(p->account,"personal"); strcpy(p->plan,"plus");
    p->windows[0]=(quota_window_t){.minutes=300,.remaining=73,.resets_at=now+7200};
    p->windows[1]=(quota_window_t){.minutes=10080,.remaining=12,.resets_at=now+86400*2};
    p->reset_count=3; p->latest_reset_expires_at=now+86400*7;
    p->next_reset_expires_at=now+86400*2;
    save(screen,argv[1],"chatgpt",&view,&net,now);
    assert(label_with_text(screen,"ChatGPT Plus"));
    assert(!label_with_text(screen,"Plus"));
    assert(!label_with_text(screen,"personal"));
    strcpy(p->plan,"business"); view.index=5;
    save(screen,argv[1],"chatgpt-team",&view,&net,now);
    assert(label_with_text(screen,"ChatGPT Team"));
    assert(!label_with_text(screen,"Team"));
    strcpy(p->plan,"plus"); view.index=3;
    p->reset_count=0;
    save(screen,argv[1],"zero-reset",&view,&net,now);
    p->reset_count=-1; p->next_reset_expires_at=0;
    save(screen,argv[1],"unknown-reset",&view,&net,now);
    p->reset_count=3; p->next_reset_expires_at=now+3600;
    save(screen,argv[1],"expiring-reset",&view,&net,now);
    save(screen,argv[1],"expired-reset",&view,&net,now+3600);
    quota_provider_t chatgpt=*p;
    memset(p,0,sizeof(*p)); p->kind=QUOTA_POINTS; p->updated_at=now; p->status=QUOTA_OK; view.index=4;
    strcpy(p->label,"TeleAgent"); strcpy(p->source,"teleagent"); strcpy(p->account,"teleagent");
    p->points=(quota_points_t){.balance="12345.67",.spent_today="12.34",.added_today="100"};
    save(screen,argv[1],"teleagent",&view,&net,now);
    assert(!label_with_text(screen,"积分"));
    assert(label_with_text(screen,"积分余额"));
    strcpy(p->points.spent_today,"0"); strcpy(p->points.added_today,"0");
    save(screen,argv[1],"teleagent-zero",&view,&net,now);
    *p=chatgpt; view.index=3;
    save(screen,argv[1],"stale",&view,&net,now+901);
    net.wifi=false; save(screen,argv[1],"offline",&view,&net,now);
    net.connecting=true;
    save(screen,argv[1],"connecting",&view,&net,now);
    assert(label_with_text(screen,"连接 Wi-Fi"));
    net.connecting=false;
    p->status=QUOTA_NETWORK;
    save(screen,argv[1],"connect-failed",&view,&net,now);
    assert(label_with_text(screen,"同步失败 · 保留旧数据"));
    assert(label_with_text(screen,"73%"));
    p->status=QUOTA_OK;
    net.wifi=true; net.syncing=true; save(screen,argv[1],"syncing",&view,&net,now);
    p->status=QUOTA_COLLECTOR;
    save(screen,argv[1],"failed-syncing",&view,&net,now);
    assert(label_with_text(screen,"采集失败 · 保留旧数据"));
    assert(label_with_text(screen,"Wi-Fi · 同步中"));
    net.syncing=false; net.sync_wait_seconds=7;
    save(screen,argv[1],"failed-waiting",&view,&net,now);
    assert(label_with_text(screen,"采集失败 · 保留旧数据"));
    assert(label_with_text(screen,"等待 7 秒同步"));
    net.sync_wait_seconds=0; p->status=QUOTA_OK;
    net.syncing=false; net.setup=true;
    strcpy(net.ap_name,"AI-Passport-1234"); strcpy(net.ap_password,"sample1234");
    save(screen,argv[1],"setup",&view,&net,now);
    net.config_failed=true; net.save_state=-1;
    save(screen,argv[1],"setup-failed",&view,&net,now);
    net.config_failed=false; net.save_state=0;
    net.setup=false; p->updated_at=0; p->status=QUOTA_EMPTY;
    save(screen,argv[1],"empty",&view,&net,now);
    p->status=QUOTA_UNCONFIGURED; net.cos_missing=1;
    save(screen,argv[1],"unconfigured",&view,&net,now);
    p->status=QUOTA_OK; p->updated_at=now; p->kind=QUOTA_DEEPSEEK; p->window_count=0;
    p->balance_count=1; p->available=true; net.cos_missing=0;
    strcpy(p->label,"DeepSeek"); strcpy(p->plan,"API");
    strcpy(p->account,"a-very-long-but-valid-custom-account-name-that-must-stay-on-one-row");
    p->balances[0]=(quota_balance_t){.currency="CNY",.total="12345678901234567890123",.topped_up="99999999.99",.granted="0.00001"};
    save(screen,argv[1],"long-values",&view,&net,now);
    strcpy(p->balances[0].total,"-999999999.99");
    save(screen,argv[1],"negative-large",&view,&net,now);
    /* Repeated navigation must not accumulate widgets, animations or heap use. */
    const int batteries[]={-1,0,1,20,21,78,100,105};
    for (int i=0;i<100;i++) {
        net.syncing=(i%3)==0; net.wifi=(i%5)!=0;
        quota_ui_render(&view,&net,batteries[i%8],i%2,now);
        lv_obj_update_layout(screen);
        check(screen,false);
        if (batteries[i%8] == 100) {
            lv_obj_t *battery=label_with_text(screen,"100%"); assert(battery);
            lv_point_t size;
            lv_text_get_size(&size,"100%",&lv_font_montserrat_14,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
            assert(size.x <= lv_obj_get_width(battery));
        }
        lv_refr_now(NULL);
    }
    quota_preferences_t prefs; quota_preferences_default(&prefs);
    quota_settings_t settings; quota_settings_open(&settings);
    net=(quota_connection_t){.wifi=true,.clock_ready=true}; *p=chatgpt;
    for (int i=0;i<QUOTA_SETTING_COUNT;i++) {
        settings.selected=(quota_setting_item_t)i;
        quota_ui_render_settings(&view,&net,78,now,&prefs,&settings,false);
        char name[40]; snprintf(name,sizeof(name),"settings-menu-%d",i);
        capture(screen,argv[1],name);
    }
    settings.page=QUOTA_SETTINGS_WIFI; settings.wifi_selected=1;
    quota_ui_render_settings(&view,&net,78,now,&prefs,&settings,false);
    capture(screen,argv[1],"settings-wifi");
    settings.ap_requested=true;
    quota_ui_render_settings(&view,&net,78,now,&prefs,&settings,false);
    capture(screen,argv[1],"settings-wifi-opening");
    settings.ap_requested=false; net.setup_failed=true;
    quota_ui_render_settings(&view,&net,78,now,&prefs,&settings,false);
    capture(screen,argv[1],"settings-wifi-failed");
    net.setup_failed=false; net.setup=true;
    strcpy(net.ap_name,"Passport-Quota-1234"); strcpy(net.ap_password,"123456789ABC");
    quota_ui_render_settings(&view,&net,78,now,&prefs,&settings,false);
    capture(screen,argv[1],"settings-wifi-hotspot");
    assert(label_with_text(screen,"Wi-Fi 设置"));
    net.setup=false; settings.page=QUOTA_SETTINGS_EDIT; settings.draft=prefs;
    for (int i=1;i<QUOTA_SETTING_COUNT;i++) {
        settings.selected=(quota_setting_item_t)i;
        quota_ui_render_settings(&view,&net,78,now,&prefs,&settings,false);
        char name[40]; snprintf(name,sizeof(name),"settings-edit-%d",i);
        capture(screen,argv[1],name);
    }
    settings.selected=QUOTA_SETTING_VOLUME; settings.draft.volume=0;
    quota_ui_render_settings(&view,&net,10,now,&prefs,&settings,false);
    capture(screen,argv[1],"settings-mute");
    settings.save_failed=true;
    quota_ui_render_settings(&view,&net,78,now,&prefs,&settings,false);
    capture(screen,argv[1],"settings-save-failed");
    assert(label_with_text(screen,"保存失败 · 确定重试"));
    settings.save_failed=false;
    quota_ui_render_settings(&view,&net,78,now,&prefs,&settings,true);
    capture(screen,argv[1],"settings-audio-failed");
    settings.selected=QUOTA_SETTING_DIM; settings.draft.dim_seconds=0;
    quota_ui_render_settings(&view,&net,78,now,&prefs,&settings,false);
    capture(screen,argv[1],"settings-always-on");
    settings.draft.dim_seconds=300;
    quota_ui_render_settings(&view,&net,78,now,&prefs,&settings,false);
    capture(screen,argv[1],"settings-dim-max");
    settings.selected=QUOTA_SETTING_ROTATION; settings.draft.automatic=true;
    quota_ui_render_settings(&view,&net,78,now,&prefs,&settings,false);
    capture(screen,argv[1],"settings-rotation-on");
    settings.page=QUOTA_SETTINGS_ROOT; settings.saved=true;
    quota_ui_render_settings(&view,&net,78,now,&prefs,&settings,false);
    capture(screen,argv[1],"settings-saved");
    settings.page=QUOTA_SETTINGS_CLOSED;
    quota_ui_render_settings(&view,&net,78,now,&prefs,&settings,false);
    capture(screen,argv[1],"settings-return");
    assert(label_with_text(screen,"73%"));
    assert(!label_with_text(screen,"上下选择 · 确定进入"));
    /* Exercise the shared panel anchors across every settings/account mode. */
    for (int i=0;i<100;i++) {
        settings.page=(quota_settings_page_t)(i%4);
        settings.selected=(quota_setting_item_t)(1+i%4);
        quota_ui_render_settings(&view,&net,78,now,&prefs,&settings,false);
        lv_obj_update_layout(screen); check(screen,false); lv_refr_now(NULL);
    }
    lv_mem_monitor_t memory; lv_mem_monitor(&memory);
    printf("LVGL quota screens: PASS (%u visible labels checked, pool used %u%%, max %zu bytes)\n",s_labels,memory.used_pct,memory.max_used);
    lv_deinit();
}
