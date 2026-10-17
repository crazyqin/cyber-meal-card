#include "quota_ui.h"
#include "quota_presentation.h"
#include <stdio.h>
#include <time.h>

LV_FONT_DECLARE(quota_font_14);
LV_FONT_DECLARE(quota_font_16);
extern const uint32_t quota_font_codepoints[];
extern const unsigned quota_font_codepoint_count;

static const uint32_t BG = 0x0B1421, PANEL = 0x15253A, INK = 0xEAF3FF;
static const uint32_t MUTED = 0x9BB1CC, ACCENT = 0x78B7FF, WARNING = 0xFFBF80;
static lv_font_t s_number_font;
static lv_obj_t *s_screen, *s_body, *s_battery, *s_network, *s_network_dot;
static lv_obj_t *s_title, *s_subtitle, *s_status, *s_status_dot, *s_updated, *s_page, *s_hint;
static lv_obj_t *s_row[3], *s_name[3], *s_value[3], *s_detail[3], *s_bar[3];

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
static void number(lv_obj_t *label, const char *value) {
    const lv_font_t *font = &s_number_font;
    if (text_width(value, font) > 104) font = &quota_font_16;
    if (text_width(value, font) > 104) font = &quota_font_14;
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_height(label, font->line_height);
    lv_label_set_text(label, value);
}
lv_obj_t *quota_ui_create(void) {
    /* Currency symbols and the unlimited label use the explicit CJK fallback. */
    s_number_font = lv_font_montserrat_20;
    s_number_font.fallback = &quota_font_16;
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, 240, 320);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    s_network_dot = box(s_screen, 17, 17, 5, 5, MUTED, LV_RADIUS_CIRCLE);
    s_network = text(s_screen, 29, 8, 130, "连接中", &quota_font_14, MUTED);
    s_battery = text(s_screen, 179, 10, 45, "--", &lv_font_montserrat_14, MUTED);
    lv_obj_set_style_text_align(s_battery, LV_TEXT_ALIGN_RIGHT, 0);
    box(s_screen, 16, 34, 208, 1, 0x2A3D58, 0);
    s_title = text(s_screen, 16, 44, 208, "订阅用量", &s_number_font, INK);
    s_subtitle = text(s_screen, 16, 71, 208, "等待同步", &quota_font_14, MUTED);
    s_body = box(s_screen, 12, 100, 216, 164, BG, 0);
    for (int i = 0; i < 3; i++) {
        s_row[i] = box(s_body, 0, i * 56, 216, 52, PANEL, 7);
        s_name[i] = text(s_row[i], 10, 4, 88, "--", &quota_font_16, INK);
        s_value[i] = text(s_row[i], 102, 4, 104, "--", &s_number_font, INK);
        lv_label_set_long_mode(s_value[i], LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_align(s_value[i], LV_TEXT_ALIGN_RIGHT, 0);
        s_detail[i] = text(s_row[i], 10, 28, 196, "", &quota_font_14, MUTED);
        s_bar[i] = lv_bar_create(s_row[i]);
        lv_obj_remove_style_all(s_bar[i]);
        lv_obj_set_pos(s_bar[i], 10, 49); lv_obj_set_size(s_bar[i], 196, 2);
        lv_bar_set_range(s_bar[i], 0, 100);
        lv_obj_set_style_bg_color(s_bar[i], lv_color_hex(0x2E4561), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_bar[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_bar[i], LV_OPA_COVER, LV_PART_INDICATOR);
    }
    s_status_dot = box(s_screen, 17, 278, 5, 5, MUTED, LV_RADIUS_CIRCLE);
    s_status = text(s_screen, 29, 269, 195, "等待同步", &quota_font_14, MUTED);
    s_updated = text(s_screen, 16, 296, 164, "采集 --", &quota_font_14, MUTED);
    s_page = text(s_screen, 171, 296, 53, "1/1", &lv_font_montserrat_14, MUTED);
    lv_obj_set_style_text_align(s_page, LV_TEXT_ALIGN_RIGHT, 0);
    s_hint = text(s_screen, 16, 106, 208, "", &quota_font_16, INK);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_height(s_hint, 158);
    visible(s_hint, false);
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
        case QUOTA_AUTH: return "认证失败，请检查配置";
        case QUOTA_NETWORK: return "同步失败，保留缓存";
        case QUOTA_UNSUPPORTED: return "未订阅或接口不可用";
        case QUOTA_THROTTLED: return "请求受限，稍后重试";
        case QUOTA_INVALID: return "采集数据格式不支持";
        case QUOTA_STORAGE: return "保存失败，请重试";
        case QUOTA_BLOCKED: return "访问被拒，请检查网络";
        case QUOTA_COLLECTOR: return "采集失败，请检查采集器";
        case QUOTA_ACCOUNT_MISSING: return "未找到账号数据";
        case QUOTA_EMPTY: return "暂无采集账号";
        case QUOTA_LIMIT: return "账号数量已达上限";
        default: return "等待配置";
    }
}
void quota_ui_render(const quota_view_t *view, const quota_connection_t *net,
                     int battery, bool automatic, int64_t now) {
    const quota_provider_t *p = &view->provider;
    quota_presentation_t content;
    quota_present_provider(p, now, &content);
    lv_label_set_text(s_title, net->setup ? "设备设置" : content.title);
    lv_obj_set_style_text_font(s_title, text_width(net->setup ? "设备设置" : content.title, &s_number_font) > 208 ? &quota_font_16 : &s_number_font, 0);
    lv_label_set_text(s_subtitle, net->setup ? "手机连接设备热点" : content.subtitle);
    if (battery < 0) lv_label_set_text(s_battery, "--");
    else lv_label_set_text_fmt(s_battery, "%d%%", battery);
    lv_label_set_text(s_network, net->setup ? "设置中" : net->connecting ? "连接中" : !net->wifi ? "离线" : !net->clock_ready ? "校时中" : "在线");
    if (automatic && !net->setup && !net->connecting && net->clock_ready)
        lv_label_set_text(s_network, net->wifi ? "在线 · 轮播" : "离线 · 轮播");
    lv_obj_set_style_bg_color(s_network_dot, lv_color_hex(net->wifi ? ACCENT : MUTED), 0);
    visible(s_body, !net->setup && p->updated_at && content.count > 0);
    visible(s_hint, net->setup || !p->updated_at || !content.count);
    if (net->setup) {
        lv_label_set_text_fmt(s_hint, "%s\n%s\n密码 %s\n\n192.168.4.1\n长按确定退出设置", net->config_failed ? "连接失败，请检查配置" : "连接热点后打开网址", net->ap_name, net->ap_password);
    } else if (!p->updated_at || !content.count) {
        const char *hint = p->status == QUOTA_EMPTY ? "暂无采集账号\n\n请启动电脑采集器\n确认 COS 上传成功\n确定键重新同步" :
                           p->status == QUOTA_UNCONFIGURED ? "等待采集数据\n\n确定键立即同步\n长按下键连接 Wi-Fi\n长按确定打开设置" :
                           "暂无可显示的数据\n\n请检查采集器和网络\n确定键重新同步\n长按确定打开设置";
        if (p->status == QUOTA_UNCONFIGURED && net->cos_missing) hint = "尚未配置 COS\n\n长按确定打开设置\n配置后自动同步";
        lv_label_set_text(s_hint, hint);
    }
    for (int i = 0; i < 3; i++) {
        const quota_metric_t *row = &content.rows[i];
        visible(s_row[i], i < content.count);
        if (i >= content.count) continue;
        lv_label_set_text(s_name[i], row->name);
        number(s_value[i], row->value);
        lv_obj_set_style_text_color(s_value[i], lv_color_hex(row->warning ? WARNING : INK), 0);
        lv_label_set_text(s_detail[i], row->detail);
        visible(s_detail[i], row->detail[0]);
        /* Balance-only rows are centered instead of leaving empty helper lines. */
        lv_obj_set_y(s_name[i], row->detail[0] ? 4 : 15);
        lv_obj_set_y(s_value[i], row->detail[0] ? 4 : 14);
        visible(s_bar[i], row->progress);
        lv_obj_set_style_bg_color(s_bar[i], lv_color_hex(row->warning ? WARNING : ACCENT), LV_PART_INDICATOR);
        lv_bar_set_value(s_bar[i], row->remaining, LV_ANIM_OFF);
    }
    bool stale = quota_is_stale(p, now);
    bool warning = stale || view->discovery_limited || view->discovery_status != QUOTA_OK || !net->wifi ||
        (p->kind == QUOTA_DEEPSEEK && !p->available);
    const char *status = p->status == QUOTA_OK && stale ? "采集已过期，请检查采集器" :
        p->kind == QUOTA_DEEPSEEK && p->status == QUOTA_OK && !p->available ? "API 余额不可用" : status_text(p->status);
    if (view->discovery_limited) status = "账号数量已达上限";
    else if (view->discovery_status != QUOTA_OK && p->status == QUOTA_OK) status = "部分采集器同步失败";
    if (net->setup) status = net->save_state == -1 ? "Wi-Fi 错误，配置未保存" : net->save_state == -2 ? "保存失败，请重试" : "确定同步 · 长按下键联网";
    else if (!net->wifi && !net->connecting) status = "Wi-Fi 未连接";
    if (net->connecting) status = "正在连接 Wi-Fi...";
    else if (net->syncing) status = "正在同步...";
    else if (net->sync_wait_seconds) {
        lv_label_set_text_fmt(s_status, "%d 秒后同步", net->sync_wait_seconds);
        status = NULL;
    }
    if (status) lv_label_set_text(s_status, status);
    lv_obj_set_style_bg_color(s_status_dot, lv_color_hex(warning ? WARNING : ACCENT), 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(warning ? WARNING : MUTED), 0);
    if (p->updated_at > 1700000000 && !net->setup) {
        char date[24]; quota_date_label(p->updated_at, date, sizeof(date));
        lv_label_set_text_fmt(s_updated, "采集 %s", date);
    } else lv_label_set_text(s_updated, net->setup ? "" : "采集 --");
    visible(s_page, !net->setup);
    lv_label_set_text_fmt(s_page, "%d/%d", view->index + 1, view->count);
    /* Rotation is a quiet footer cue and does not alter the account subtitle. */
    lv_obj_set_style_text_color(s_page, lv_color_hex(automatic ? ACCENT : MUTED), 0);
}
