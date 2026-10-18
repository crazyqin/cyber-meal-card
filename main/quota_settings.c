#include "quota_settings.h"
#include <stdio.h>
#include <string.h>

static const uint16_t s_dim_options[] = {0, 15, 30, 45, 60, 120, 300};
void quota_preferences_default(quota_preferences_t *out) {
    *out = (quota_preferences_t){.version=1, .dim_seconds=45, .volume=40, .brightness=75};
}
bool quota_preferences_valid(const quota_preferences_t *p) {
    if (p->version != 1 || p->volume > 100 || p->brightness < 10 || p->brightness > 100 ||
        p->automatic > 1 || p->reserved[0] || p->reserved[1] || p->reserved[2]) return false;
    for (unsigned i=0; i<sizeof(s_dim_options)/sizeof(s_dim_options[0]); i++)
        if (p->dim_seconds == s_dim_options[i]) return true;
    return false;
}
bool quota_preferences_load(const void *blob, size_t size, quota_preferences_t *out) {
    quota_preferences_default(out);
    if (!blob || size != sizeof(*out)) return false;
    quota_preferences_t stored;
    memcpy(&stored, blob, sizeof(stored));
    if (!quota_preferences_valid(&stored)) return false;
    *out = stored;
    return true;
}
bool quota_preferences_dim_due(const quota_preferences_t *p, int64_t idle_us) {
    return p->dim_seconds && idle_us >= (int64_t)p->dim_seconds * 1000000;
}
void quota_settings_open(quota_settings_t *s) {
    *s = (quota_settings_t){.page=QUOTA_SETTINGS_ROOT};
}
static uint8_t adjust(uint8_t value, int direction, int minimum) {
    int next = value + direction * 5;
    return next < minimum ? minimum : next > 100 ? 100 : next;
}
quota_setting_action_t quota_settings_key(quota_settings_t *s, const quota_preferences_t *p,
                                         quota_setting_key_t key, bool ap_active) {
    s->saved = false;
    s->save_failed = false;
    if (s->page == QUOTA_SETTINGS_ROOT) {
        if (key == QUOTA_SETTING_BACK) s->page = QUOTA_SETTINGS_CLOSED;
        else if (key == QUOTA_SETTING_UP) s->selected = (s->selected + QUOTA_SETTING_COUNT - 1) % QUOTA_SETTING_COUNT;
        else if (key == QUOTA_SETTING_DOWN) s->selected = (s->selected + 1) % QUOTA_SETTING_COUNT;
        else if (key == QUOTA_SETTING_OK) {
            s->draft = *p;
            s->page = s->selected == QUOTA_SETTING_WIFI ? QUOTA_SETTINGS_WIFI : QUOTA_SETTINGS_EDIT;
            s->wifi_selected = 0;
        }
    } else if (s->page == QUOTA_SETTINGS_WIFI) {
        if (key == QUOTA_SETTING_BACK) {
            if (ap_active || s->ap_requested) { s->ap_requested = false; return QUOTA_SETTING_CLOSE_AP; }
            s->page = QUOTA_SETTINGS_ROOT;
        } else if (key == QUOTA_SETTING_OK) {
            if (ap_active || s->wifi_selected == 0) return QUOTA_SETTING_CONNECT;
            if (!s->ap_requested) { s->ap_requested = true; return QUOTA_SETTING_OPEN_AP; }
        } else if (!ap_active && !s->ap_requested) s->wifi_selected ^= 1;
    } else if (s->page == QUOTA_SETTINGS_EDIT) {
        if (key == QUOTA_SETTING_BACK) { s->page = QUOTA_SETTINGS_ROOT; return QUOTA_SETTING_CANCEL; }
        if (key == QUOTA_SETTING_OK) return QUOTA_SETTING_SAVE;
        int direction = key == QUOTA_SETTING_UP ? 1 : -1;
        if (s->selected == QUOTA_SETTING_VOLUME) s->draft.volume = adjust(s->draft.volume, direction, 0);
        else if (s->selected == QUOTA_SETTING_BRIGHTNESS) s->draft.brightness = adjust(s->draft.brightness, direction, 10);
        else if (s->selected == QUOTA_SETTING_ROTATION) s->draft.automatic ^= 1;
        else if (s->selected == QUOTA_SETTING_DIM) {
            for (int i=0; i<7; i++) if (s->draft.dim_seconds == s_dim_options[i]) {
                s->draft.dim_seconds = s_dim_options[(i + direction + 7) % 7];
                break;
            }
        }
        return QUOTA_SETTING_PREVIEW;
    }
    return QUOTA_SETTING_NONE;
}
void quota_settings_saved(quota_settings_t *s, bool success) {
    s->save_failed = !success;
    s->saved = success;
    if (success) s->page = QUOTA_SETTINGS_ROOT;
}
const char *quota_setting_name(quota_setting_item_t item) {
    static const char *names[] = {"Wi-Fi 设置", "音量", "背光亮度", "自动调暗", "自动轮播"};
    return (unsigned)item < QUOTA_SETTING_COUNT ? names[item] : "设备设置";
}
void quota_setting_value(const quota_preferences_t *p, quota_setting_item_t item, char *out, size_t size) {
    if (item == QUOTA_SETTING_VOLUME) {
        if (!p->volume) snprintf(out, size, "静音");
        else snprintf(out, size, "%u%%", p->volume);
    } else if (item == QUOTA_SETTING_BRIGHTNESS) snprintf(out, size, "%u%%", p->brightness);
    else if (item == QUOTA_SETTING_DIM) {
        if (!p->dim_seconds) snprintf(out, size, "常亮");
        else snprintf(out, size, "%u 秒", p->dim_seconds);
    } else if (item == QUOTA_SETTING_ROTATION) snprintf(out, size, "%s", p->automatic ? "开启" : "关闭");
    else if (size) out[0] = 0;
}
