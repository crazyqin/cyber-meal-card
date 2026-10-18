#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "quota_net.h"
#include "quota_model.h"
#include "quota_ui.h"
#include "quota_device.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct { bsp_btn_t button; bsp_btn_ev_t event; } quota_key_t;
static QueueHandle_t s_keys;
static int s_provider;
static int s_page_count = 1;
static quota_selection_t s_selection;
static quota_preferences_t s_preferences;
static quota_settings_t s_settings;
static uint8_t current_brightness(void) {
    return s_settings.page == QUOTA_SETTINGS_EDIT && s_settings.selected == QUOTA_SETTING_BRIGHTNESS ?
        s_settings.draft.brightness : s_preferences.brightness;
}
static void settings_key(quota_setting_key_t key, const quota_connection_t *connection) {
    quota_setting_action_t action = quota_settings_key(&s_settings, &s_preferences, key, connection->setup);
    if (action == QUOTA_SETTING_PREVIEW) {
        if (s_settings.selected == QUOTA_SETTING_BRIGHTNESS) bsp_display_backlight(s_settings.draft.brightness);
        if (s_settings.selected == QUOTA_SETTING_VOLUME) quota_device_preview_volume(s_settings.draft.volume);
    } else if (action == QUOTA_SETTING_SAVE) {
        bool stopped = quota_device_stop_preview();
        bool saved = stopped && (!memcmp(&s_preferences, &s_settings.draft, sizeof(s_preferences)) || quota_device_save(&s_settings.draft));
        if (saved) s_preferences = s_settings.draft;
        quota_settings_saved(&s_settings, saved);
    } else if (action == QUOTA_SETTING_CANCEL) {
        bsp_display_backlight(s_preferences.brightness);
        quota_device_stop_preview();
    } else if (action == QUOTA_SETTING_CONNECT) quota_net_refresh();
    else if (action == QUOTA_SETTING_OPEN_AP) quota_net_set_setup(true);
    else if (action == QUOTA_SETTING_CLOSE_AP) quota_net_set_setup(false);
}
static void on_key(bsp_btn_t button, bsp_btn_ev_t event, void *user) {
    (void)user;
    if (event != BSP_BTN_CLICK && event != BSP_BTN_LONG) return;
    quota_key_t key = {.button = button, .event = event};
    xQueueSend(s_keys, &key, 0);
}
void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init()); /* Never erase existing settings on failure. */
    quota_device_load(&s_preferences);
    ESP_ERROR_CHECK(bsp_display_init());
    configASSERT(bsp_lvgl_init());
    s_keys = xQueueCreate(8, sizeof(quota_key_t));
    configASSERT(s_keys);
    bool battery_ok = bsp_battery_init() == ESP_OK;
    quota_net_start();
    if (bsp_lvgl_lock(1000)) {
        quota_ui_create();
        unsigned missing = quota_ui_missing_glyphs();
        if (missing) ESP_LOGE("quota", "Missing UI font glyphs: %u", missing);
        bsp_lvgl_unlock();
    }
    else return;
    ESP_ERROR_CHECK(bsp_button_init(on_key, NULL));
    bsp_display_backlight(s_preferences.brightness);
    int64_t activity = esp_timer_get_time(), rendered = 0, rotated = activity, battery_at = 0;
    int64_t hold_until = 0;
    bool dimmed = false;
    int battery = -1;
    for (;;) {
        quota_key_t key;
        quota_view_t view;
        quota_connection_t connection;
        quota_net_read(&s_selection, &view, &connection);
        if (connection.setup || connection.setup_failed) s_settings.ap_requested = false;
        int64_t now = esp_timer_get_time();
        if (xQueueReceive(s_keys, &key, pdMS_TO_TICKS(100)) == pdTRUE) {
            now = esp_timer_get_time();
            activity = now;
            if (dimmed) { dimmed = false; bsp_display_backlight(current_brightness()); hold_until = now + 30000000; }
            else if (s_settings.page != QUOTA_SETTINGS_CLOSED) {
                if (key.event == BSP_BTN_LONG && key.button == BSP_BTN_OK) settings_key(QUOTA_SETTING_BACK, &connection);
                else if (key.event == BSP_BTN_CLICK) settings_key(key.button == BSP_BTN_OK ? QUOTA_SETTING_OK :
                    key.button == BSP_BTN_UP ? QUOTA_SETTING_UP : QUOTA_SETTING_DOWN, &connection);
                hold_until = now + 30000000;
            }
            else if (key.event == BSP_BTN_LONG && key.button == BSP_BTN_OK) {
                quota_settings_open(&s_settings);
                if (connection.setup) quota_net_set_setup(false);
            }
            else if (key.event == BSP_BTN_LONG && key.button == BSP_BTN_UP) {
                quota_preferences_t next = s_preferences;
                next.automatic = !next.automatic;
                if (quota_device_save(&next)) s_preferences = next;
                else {
                    quota_settings_open(&s_settings); s_settings.page = QUOTA_SETTINGS_EDIT;
                    s_settings.selected = QUOTA_SETTING_ROTATION; s_settings.draft = next; s_settings.save_failed = true;
                }
                hold_until = 0;
            }
            else if (key.event == BSP_BTN_CLICK) {
                if (key.button == BSP_BTN_OK) quota_net_refresh();
                else {
                    s_selection.index = quota_next_provider(s_provider, key.button == BSP_BTN_UP ? -1 : 1, s_page_count);
                    s_selection.collector[0] = 0;
                    hold_until = now + 30000000;
                }
            }
            rendered = 0; rotated = now;
        }
        if (!dimmed && quota_preferences_dim_due(&s_preferences, now - activity)) { dimmed = true; bsp_display_backlight(5); }
        if (now - battery_at >= 30000000 || !battery_at) { battery = battery_ok ? bsp_battery_soc() : -1; battery_at = now; }
        if (now - rendered < 1000000 && rendered) continue;
        quota_net_read(&s_selection, &view, &connection);
        if (s_preferences.automatic && s_settings.page == QUOTA_SETTINGS_CLOSED && !connection.setup && quota_rotation_due(now, rotated, hold_until)) {
            s_selection.index = quota_next_provider(view.index, 1, view.count);
            s_selection.collector[0] = 0; rotated = now;
            quota_net_read(&s_selection, &view, &connection);
        }
        s_provider = view.index;
        s_page_count = view.count;
        if (bsp_lvgl_lock(1000)) {
            quota_ui_render_settings(&view, &connection, battery, time(NULL), &s_preferences, &s_settings, quota_device_audio_failed());
            bsp_lvgl_unlock();
        }
        rendered = now;
        if (now < 3000000) ESP_LOGI("quota", "heap=%lu largest=%lu", (unsigned long)esp_get_free_heap_size(), (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }
}
