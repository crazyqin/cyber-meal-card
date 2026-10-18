#include "quota_ui.h"
#include "quota_presentation.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

LV_FONT_DECLARE(quota_font_14);
LV_FONT_DECLARE(quota_font_16);
extern const uint32_t quota_font_codepoints[];
extern const unsigned quota_font_codepoint_count;

static const uint32_t BG = 0x0B1421, PANEL = 0x142338, INK = 0xEFF5FF;
static const uint32_t MUTED = 0x9BAEC7, ACCENT = 0x78B7FF, WARNING = 0xFFBF80;
static const uint32_t EDGE = 0x26394F;
static lv_font_t s_number_font, s_medium_font, s_large_font;
static lv_obj_t *s_screen, *s_body, *s_battery, *s_battery_icon, *s_battery_fill;
static lv_obj_t *s_network, *s_network_dot, *s_hint_panel, *s_page_panel;
static lv_obj_t *s_title, *s_subtitle, *s_status, *s_status_dot, *s_updated, *s_page, *s_hint;
static lv_obj_t *s_row[3], *s_name[3], *s_value[3], *s_detail[3], *s_bar[3];
static lv_obj_t *s_settings_panel, *s_settings_row[5], *s_settings_name[5], *s_settings_value[5];
static lv_obj_t *s_settings_note, *s_editor_value, *s_editor_bar, *s_editor_help;

static uint32_t provider_color(const quota_provider_t *provider) {
    /* Bright title accents remain readable on the shared navy background. */
    switch (provider->kind) {
        case QUOTA_CODEX: return 0x4FD1A5;
        case QUOTA_CLINE: return 0xF5C16C;
        case QUOTA_DEEPSEEK: return 0x7E9BFF;
        case QUOTA_TOKENHUB: return 0xC29CFF;
        case QUOTA_POINTS: return !strcmp(provider->source, "teleagent") ? 0x58C8E8 : INK;
        default: return INK;
    }
}
static lv_obj_t *text(lv_obj_t *parent, int x, int y, int width, const char *value,
                      const lv_font_t *font, uint32_t color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_obj_set_pos(label, x, y);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(label, width, font->line_height);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, value);
    return label;
}
static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}
static void visible(lv_obj_t *obj, bool show) {
    if (show) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}
static int text_width(const char *value, const lv_font_t *font) {
    lv_point_t size;
    lv_text_get_size(&size, value, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}
static void number(lv_obj_t *label, const char *value, int width, bool primary) {
    const lv_font_t *font = primary ? &s_large_font : &s_number_font;
    if (primary && text_width(value, font) > width) font = &s_medium_font;
    if (text_width(value, font) > width) font = &s_number_font;
    if (text_width(value, font) > width) font = &quota_font_16;
    if (text_width(value, font) > width) font = &quota_font_14;
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_width(label, width);
    lv_obj_set_height(label, font->line_height);
    lv_label_set_text(label, value);
}
lv_obj_t *quota_ui_create(void) {
    /* Currency symbols and the unlimited label use the explicit CJK fallback. */
    s_number_font = lv_font_montserrat_20;
    s_number_font.fallback = &quota_font_16;
    s_medium_font = lv_font_montserrat_28;
    s_medium_font.fallback = &quota_font_16;
    s_large_font = lv_font_montserrat_36;
    s_large_font.fallback = &quota_font_16;
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, 240, 320);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    s_network_dot = box(s_screen, 17, 16, 6, 6, MUTED, LV_RADIUS_CIRCLE);
    s_network = text(s_screen, 29, 8, 130, "连接中", &quota_font_14, MUTED);
    s_battery = text(s_screen, 164, 10, 38, "--", &lv_font_montserrat_14, MUTED);
    lv_obj_set_style_text_align(s_battery, LV_TEXT_ALIGN_RIGHT, 0);
    s_battery_icon = box(s_screen, 208, 13, 17, 10, EDGE, 3);
    box(s_battery_icon, 1, 1, 15, 8, BG, 2);
    box(s_screen, 225, 16, 2, 4, MUTED, 1);
    s_battery_fill = box(s_battery_icon, 3, 3, 11, 4, MUTED, 1);
    s_title = text(s_screen, 16, 36, 208, "订阅用量", &s_number_font, INK);
    s_subtitle = text(s_screen, 144, 40, 80, "", &quota_font_14, MUTED);
    lv_obj_set_style_text_align(s_subtitle, LV_TEXT_ALIGN_RIGHT, 0);
    s_body = box(s_screen, 12, 72, 216, 194, BG, 0);
    for (int i = 0; i < 3; i++) {
        bool primary = i == 0;
        s_row[i] = box(s_body, 0, primary ? 0 : 96 + (i - 1) * 52, 216, primary ? 90 : 46, PANEL, 9);
        lv_obj_set_style_bg_opa(s_row[i], primary ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        if (!primary) {
            lv_obj_set_style_border_width(s_row[i], 1, 0);
            lv_obj_set_style_border_color(s_row[i], lv_color_hex(EDGE), 0);
        }
        s_name[i] = text(s_row[i], primary ? 4 : 10, 0, primary ? 208 : 88, "--", &quota_font_14, primary ? MUTED : INK);
        s_value[i] = text(s_row[i], primary ? 4 : 102, 0, primary ? 208 : 104, "--", &s_number_font, INK);
        lv_label_set_long_mode(s_value[i], LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_align(s_value[i], primary ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_RIGHT, 0);
        s_detail[i] = text(s_row[i], primary ? 4 : 10, primary ? 61 : 22, primary ? 208 : 196, "", &quota_font_14, MUTED);
        s_bar[i] = lv_bar_create(s_row[i]);
        lv_obj_remove_style_all(s_bar[i]);
        lv_obj_set_pos(s_bar[i], primary ? 4 : 10, primary ? 86 : 42);
        lv_obj_set_size(s_bar[i], primary ? 208 : 196, primary ? 3 : 2);
        lv_bar_set_range(s_bar[i], 0, 100);
        lv_obj_set_style_bg_color(s_bar[i], lv_color_hex(EDGE), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_bar[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_bar[i], LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(s_bar[i], LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_radius(s_bar[i], LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    }
    s_status_dot = box(s_screen, 17, 282, 5, 5, MUTED, LV_RADIUS_CIRCLE);
    s_status = text(s_screen, 29, 273, 195, "等待同步", &quota_font_14, MUTED);
    s_updated = text(s_screen, 16, 297, 150, "采集 --", &quota_font_14, MUTED);
    s_page_panel = box(s_screen, 176, 297, 52, 20, PANEL, 6);
    s_page = text(s_page_panel, 2, 2, 48, "1/1", &lv_font_montserrat_14, MUTED);
    lv_obj_set_style_text_align(s_page, LV_TEXT_ALIGN_CENTER, 0);
    s_hint_panel = box(s_screen, 12, 76, 216, 190, PANEL, 9);
    s_hint = text(s_hint_panel, 12, 14, 192, "", &quota_font_14, INK);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_height(s_hint, 164);
    visible(s_hint_panel, false);
    s_editor_value = text(s_hint_panel, 12, 45, 192, "", &s_large_font, INK);
    lv_label_set_long_mode(s_editor_value, LV_LABEL_LONG_MODE_CLIP);
    s_editor_bar = lv_bar_create(s_hint_panel);
    lv_obj_remove_style_all(s_editor_bar);
    lv_obj_set_pos(s_editor_bar, 12, 99); lv_obj_set_size(s_editor_bar, 192, 5);
    lv_bar_set_range(s_editor_bar, 0, 100);
    lv_obj_set_style_bg_color(s_editor_bar, lv_color_hex(EDGE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_editor_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_editor_bar, lv_color_hex(ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_editor_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_editor_bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(s_editor_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    s_editor_help = text(s_hint_panel, 12, 121, 192, "", &quota_font_14, MUTED);
    lv_label_set_long_mode(s_editor_help, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_height(s_editor_help, 60);
    s_settings_panel = box(s_screen, 12, 72, 216, 194, BG, 0);
    for (int i=0; i<5; i++) {
        s_settings_row[i] = box(s_settings_panel, 0, i*38, 216, 34, PANEL, 8);
        s_settings_name[i] = text(s_settings_row[i], 12, 7, 112, "", &quota_font_14, INK);
        s_settings_value[i] = text(s_settings_row[i], 130, 7, 74, "", &quota_font_14, MUTED);
        lv_obj_set_style_text_align(s_settings_value[i], LV_TEXT_ALIGN_RIGHT, 0);
    }
    s_settings_note = text(s_settings_panel, 4, 132, 208, "", &quota_font_14, MUTED);
    lv_label_set_long_mode(s_settings_note, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_height(s_settings_note, 60);
    visible(s_settings_panel, false);
    lv_screen_load(s_screen);
    return s_screen;
}
unsigned quota_ui_missing_glyphs(void) {
    unsigned missing = 0;
    const lv_font_t *fonts[] = {&quota_font_14, &quota_font_16};
    for (unsigned f = 0; f < 2; f++) for (unsigned i = 0; i < quota_font_codepoint_count; i++) {
        lv_font_glyph_dsc_t glyph;
        if (!lv_font_get_glyph_dsc(fonts[f], &glyph, quota_font_codepoints[i], 0) || glyph.is_placeholder) missing++;
    }
    return missing;
}
static const char *status_text(quota_status_t status) {
    switch (status) {
        case QUOTA_OK: return "已同步";
        case QUOTA_AUTH: return "认证失败 · 检查配置";
        case QUOTA_NETWORK: return "同步失败 · 保留旧数据";
        case QUOTA_UNSUPPORTED: return "未订阅或接口不可用";
        case QUOTA_THROTTLED: return "请求受限，稍后重试";
        case QUOTA_INVALID: return "采集数据格式不支持";
        case QUOTA_STORAGE: return "保存失败，请重试";
        case QUOTA_BLOCKED: return "访问被拒，请检查网络";
        case QUOTA_COLLECTOR: return "采集失败 · 保留旧数据";
        case QUOTA_ACCOUNT_MISSING: return "未找到账号数据";
        case QUOTA_EMPTY: return "暂无采集账号";
        case QUOTA_LIMIT: return "账号数量已达上限";
        default: return "等待配置";
    }
}
void quota_ui_render(const quota_view_t *view, const quota_connection_t *net,
                     int battery, bool automatic, int64_t now) {
    visible(s_settings_panel, false);
    visible(s_editor_value, false); visible(s_editor_bar, false); visible(s_editor_help, false);
    lv_obj_set_height(s_hint, 164);
    lv_obj_set_pos(s_status, 29, 273); lv_obj_set_width(s_status, 195);
    visible(s_status_dot, true); lv_obj_set_width(s_updated, 150);
    const quota_provider_t *p = &view->provider;
    uint32_t accent = net->setup ? ACCENT : provider_color(p);
    quota_presentation_t content;
    quota_present_provider(p, now, &content);
    const char *title = net->setup ? "设备设置" : content.title;
    const char *metadata = net->setup ? "" : content.subtitle;
    int metadata_width = text_width(metadata, &quota_font_14);
    if (metadata_width > 80) metadata_width = 80;
    int title_width = metadata_width ? 208 - metadata_width - 12 : 208;
    lv_label_set_text(s_title, title);
    lv_obj_set_width(s_title, title_width);
    lv_obj_set_style_text_font(s_title, text_width(title, &s_number_font) > title_width ? &quota_font_16 : &s_number_font, 0);
    lv_obj_set_style_text_color(s_title, lv_color_hex(net->setup ? INK : provider_color(p)), 0);
    lv_label_set_text(s_subtitle, metadata);
    visible(s_subtitle, metadata_width > 0);
    lv_obj_set_pos(s_subtitle, 224 - metadata_width, 40);
    lv_obj_set_width(s_subtitle, metadata_width ? metadata_width : 1);
    if (battery < 0) lv_label_set_text(s_battery, "--");
    else lv_label_set_text_fmt(s_battery, "%d%%", battery);
    uint32_t battery_color = battery >= 0 && battery <= 20 ? WARNING : MUTED;
    lv_obj_set_style_text_color(s_battery, lv_color_hex(battery_color), 0);
    lv_obj_set_style_bg_color(s_battery_icon, lv_color_hex(battery_color), 0);
    lv_obj_set_style_bg_color(s_battery_fill, lv_color_hex(battery_color), 0);
    visible(s_battery_fill, battery > 0);
    int battery_width = battery > 100 ? 11 : battery > 0 ? (battery * 11 + 99) / 100 : 1;
    lv_obj_set_width(s_battery_fill, battery_width);
    lv_label_set_text(s_network, net->setup ? "设置中" : net->connecting ? "连接 Wi-Fi" : !net->wifi ? "Wi-Fi 未连接" : !net->clock_ready ? "校时中" : "Wi-Fi 已连接");
    if (!net->setup && !net->connecting && net->wifi && net->clock_ready) {
        if (net->syncing) lv_label_set_text(s_network, "Wi-Fi · 同步中");
        else if (net->sync_wait_seconds) lv_label_set_text_fmt(s_network, "等待 %d 秒同步", net->sync_wait_seconds);
        else if (automatic) lv_label_set_text(s_network, "Wi-Fi · 轮播");
    }
    lv_obj_set_style_bg_color(s_network_dot, lv_color_hex(net->wifi ? ACCENT : MUTED), 0);
    visible(s_body, !net->setup && p->updated_at && content.count > 0);
    visible(s_hint_panel, net->setup || !p->updated_at || !content.count);
    if (net->setup) {
        lv_label_set_text_fmt(s_hint, "%s\n%s\n密码 %s\n\n192.168.4.1\n长按确定返回设置", net->config_failed ? "连接失败，请检查配置" : "连接热点后打开网址", net->ap_name, net->ap_password);
    } else if (!p->updated_at || !content.count) {
        const char *hint = p->status == QUOTA_EMPTY ? "暂无采集账号\n\n请启动电脑采集器\n确认 COS 上传成功\n确定键重新同步" :
                           p->status == QUOTA_UNCONFIGURED ? "等待采集数据\n\n确定键连接并同步\n长按确定打开设置" :
                           "暂无可显示的数据\n\n请检查采集器和网络\n确定键重新同步\n长按确定打开设置";
        if (p->status == QUOTA_UNCONFIGURED && net->cos_missing) hint = "尚未配置 COS\n\n长按确定打开设置\n配置后自动同步";
        lv_label_set_text(s_hint, hint);
    }
    /* Every account uses one open primary metric and two fixed detail slots. */
    for (int i = 0; i < 3; i++) {
        const quota_metric_t *row = &content.rows[i];
        bool primary = i == 0, provided = i < content.count;
        lv_obj_set_y(s_name[i], primary ? 0 : row->detail[0] ? 1 : 12);
        lv_obj_set_style_text_color(s_name[i], lv_color_hex(primary || !provided ? MUTED : INK), 0);
        lv_label_set_text(s_name[i], provided ? row->name : "暂无数据");
        lv_obj_set_y(s_value[i], primary ? 20 : row->detail[0] ? 0 : 10);
        number(s_value[i], provided ? row->value : "--", primary ? 208 : 104, primary);
        lv_obj_set_style_text_color(s_value[i], lv_color_hex(!provided ? MUTED : row->warning ? WARNING : INK), 0);
        lv_label_set_text(s_detail[i], row->detail);
        visible(s_detail[i], row->detail[0]);
        visible(s_bar[i], row->progress);
        lv_obj_set_style_bg_color(s_bar[i], lv_color_hex(row->warning ? WARNING : accent), LV_PART_INDICATOR);
        lv_bar_set_value(s_bar[i], row->remaining, LV_ANIM_OFF);
    }
    bool stale = quota_is_stale(p, now);
    bool warning = stale || view->discovery_limited || view->discovery_status != QUOTA_OK || !net->wifi ||
        (p->kind == QUOTA_DEEPSEEK && !p->available);
    const char *status = p->status == QUOTA_OK && stale ? "采集已过期 · 保留旧数据" :
        p->kind == QUOTA_DEEPSEEK && p->status == QUOTA_OK && !p->available ? "API 余额不可用" : status_text(p->status);
    if (!p->updated_at && p->status == QUOTA_COLLECTOR) status = "采集失败 · 检查采集器";
    if (!p->updated_at && p->status == QUOTA_NETWORK) status = "同步失败 · 检查网络";
    if (view->discovery_limited) status = "账号数量已达上限";
    else if (view->discovery_status != QUOTA_OK && p->status == QUOTA_OK && !stale) status = "部分采集器同步失败";
    if (net->setup) status = net->save_state == -1 ? "Wi-Fi 错误，配置未保存" : net->save_state == -2 ? "保存失败，请重试" : "确定键连接并同步";
    else if (!net->wifi && p->status == QUOTA_OK && !stale) status = "离线缓存 · 等待联网";
    lv_label_set_text(s_status, status);
    lv_obj_set_style_bg_color(s_status_dot, lv_color_hex(warning ? WARNING : ACCENT), 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(warning ? WARNING : MUTED), 0);
    char age[64]; quota_collection_age_label(p->updated_at, now, age, sizeof(age));
    lv_label_set_text(s_updated, net->setup ? "" : age);
    visible(s_page_panel, !net->setup);
    lv_label_set_text_fmt(s_page, "%d/%d", view->index + 1, view->count);
    /* Rotation is a quiet footer cue and does not alter the account subtitle. */
    lv_obj_set_style_text_color(s_page, lv_color_hex(automatic ? accent : MUTED), 0);
}

void quota_ui_render_settings(const quota_view_t *view, const quota_connection_t *net,
                              int battery, int64_t now, const quota_preferences_t *prefs,
                              const quota_settings_t *settings, bool audio_failed) {
    quota_ui_render(view, net, battery, prefs->automatic, now);
    if (settings->page == QUOTA_SETTINGS_CLOSED) return;
    lv_label_set_text(s_title, settings->page == QUOTA_SETTINGS_ROOT ? "设备设置" :
                      settings->page == QUOTA_SETTINGS_WIFI ? "Wi-Fi 设置" : quota_setting_name(settings->selected));
    lv_obj_set_width(s_title, 208); lv_obj_set_style_text_font(s_title, &quota_font_16, 0);
    lv_obj_set_style_text_color(s_title, lv_color_hex(INK), 0);
    visible(s_subtitle, false); visible(s_body, false); visible(s_page_panel, false);
    if (settings->page == QUOTA_SETTINGS_WIFI && net->setup) return;
    visible(s_hint_panel, settings->page == QUOTA_SETTINGS_EDIT);
    visible(s_settings_panel, settings->page != QUOTA_SETTINGS_EDIT);
    visible(s_status_dot, false);
    lv_obj_set_pos(s_status, 16, 273); lv_obj_set_width(s_status, 208);
    lv_obj_set_width(s_updated, 208);
    lv_obj_set_style_text_color(s_status, lv_color_hex(MUTED), 0);
    lv_label_set_text(s_updated, "长按确定返回");
    if (settings->page == QUOTA_SETTINGS_EDIT) {
        char value[32]; quota_setting_value(&settings->draft, settings->selected, value, sizeof(value));
        lv_label_set_text(s_hint, settings->selected == QUOTA_SETTING_VOLUME ? "输出音量" :
                          settings->selected == QUOTA_SETTING_BRIGHTNESS ? "屏幕亮度" :
                          settings->selected == QUOTA_SETTING_DIM ? "闲置后调暗" : "账号自动切换");
        lv_obj_set_height(s_hint, 22);
        number(s_editor_value, value, 192, true);
        visible(s_editor_value, true); visible(s_editor_help, true);
        bool slider = settings->selected == QUOTA_SETTING_VOLUME || settings->selected == QUOTA_SETTING_BRIGHTNESS;
        visible(s_editor_bar, slider);
        lv_bar_set_value(s_editor_bar, settings->selected == QUOTA_SETTING_VOLUME ? settings->draft.volume : settings->draft.brightness, LV_ANIM_OFF);
        const char *help = settings->selected == QUOTA_SETTING_VOLUME ? "调节时播放短音\n静音时不播放提示" :
            settings->selected == QUOTA_SETTING_BRIGHTNESS ? "亮度立即预览\n最低 10%，避免黑屏" :
            settings->selected == QUOTA_SETTING_DIM ? "闲置后调至 5% 亮度\n按任意键恢复亮度" :
            "每 10 秒切换账号\n手动操作后暂停 30 秒";
        lv_label_set_text(s_editor_help, help);
        lv_label_set_text(s_status, "上下调节 · 确定保存");
        lv_label_set_text(s_updated, "长按确定取消修改");
        if (settings->save_failed || (audio_failed && settings->selected == QUOTA_SETTING_VOLUME)) {
            lv_label_set_text(s_status, settings->save_failed ? "保存失败 · 确定重试" : "试听失败 · 可继续保存");
            lv_obj_set_style_text_color(s_status, lv_color_hex(WARNING), 0);
        }
        return;
    }
    bool wifi = settings->page == QUOTA_SETTINGS_WIFI;
    for (int i=0; i<5; i++) {
        visible(s_settings_row[i], !wifi || i<2);
        lv_obj_set_pos(s_settings_row[i], 0, i*(wifi ? 64 : 38));
        lv_obj_set_height(s_settings_row[i], wifi ? 52 : 34);
        lv_obj_set_y(s_settings_name[i], wifi ? 16 : 7);
        lv_obj_set_y(s_settings_value[i], wifi ? 16 : 7);
        bool selected = i == (int)(wifi ? settings->wifi_selected : (unsigned)settings->selected);
        lv_obj_set_style_bg_color(s_settings_row[i], lv_color_hex(selected ? 0x263F5C : PANEL), 0);
        lv_obj_set_style_text_color(s_settings_name[i], lv_color_hex(selected ? ACCENT : INK), 0);
        char value[32]; quota_setting_value(prefs, (quota_setting_item_t)i, value, sizeof(value));
        lv_label_set_text(s_settings_name[i], wifi ? (i ? "手机配网" : "连接并同步") : quota_setting_name((quota_setting_item_t)i));
        lv_label_set_text(s_settings_value[i], wifi ? "确定" : i ? value : net->wifi ? "已连接" : "未连接");
    }
    visible(s_settings_note, wifi);
    lv_label_set_text(s_settings_note, "配网仅支持 2.4 GHz\n手机连接热点后输入密码");
    lv_label_set_text(s_status, wifi ? settings->ap_requested ? "正在打开热点" : "上下选择 · 确定执行" : settings->saved ? "设置已保存" : "上下选择 · 确定进入");
    if (wifi && net->setup_failed) {
        lv_label_set_text(s_status, "热点启动失败 · 请重试");
        lv_obj_set_style_text_color(s_status, lv_color_hex(WARNING), 0);
    }
}
