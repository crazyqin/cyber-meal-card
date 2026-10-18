#pragma once
#include "lvgl.h"
#include "quota_net.h"
#include "quota_settings.h"

/* The caller holds the BSP LVGL lock. Also used by the native screen renderer. */
lv_obj_t *quota_ui_create(void);
unsigned quota_ui_missing_glyphs(void);
void quota_ui_render(const quota_view_t *view, const quota_connection_t *net,
                     int battery, bool automatic, int64_t now);
void quota_ui_render_settings(const quota_view_t *view, const quota_connection_t *net,
                              int battery, int64_t now, const quota_preferences_t *prefs,
                              const quota_settings_t *settings, bool audio_failed);
