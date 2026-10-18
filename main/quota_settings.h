#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Separate NVS record: existing Wi-Fi/COS configuration remains compatible. */
typedef struct {
    uint32_t version;
    uint16_t dim_seconds;
    uint8_t volume, brightness, automatic, reserved[3];
} quota_preferences_t;

typedef enum { QUOTA_SETTINGS_CLOSED, QUOTA_SETTINGS_ROOT, QUOTA_SETTINGS_WIFI, QUOTA_SETTINGS_EDIT } quota_settings_page_t;
typedef enum { QUOTA_SETTING_WIFI, QUOTA_SETTING_VOLUME, QUOTA_SETTING_BRIGHTNESS,
               QUOTA_SETTING_DIM, QUOTA_SETTING_ROTATION, QUOTA_SETTING_COUNT } quota_setting_item_t;
typedef enum { QUOTA_SETTING_UP, QUOTA_SETTING_DOWN, QUOTA_SETTING_OK, QUOTA_SETTING_BACK } quota_setting_key_t;
typedef enum { QUOTA_SETTING_NONE, QUOTA_SETTING_PREVIEW, QUOTA_SETTING_SAVE,
               QUOTA_SETTING_CANCEL, QUOTA_SETTING_CONNECT, QUOTA_SETTING_OPEN_AP,
               QUOTA_SETTING_CLOSE_AP } quota_setting_action_t;
typedef struct {
    quota_settings_page_t page;
    quota_setting_item_t selected;
    unsigned wifi_selected;
    quota_preferences_t draft;
    bool save_failed, saved, ap_requested;
} quota_settings_t;

void quota_preferences_default(quota_preferences_t *out);
bool quota_preferences_valid(const quota_preferences_t *prefs);
bool quota_preferences_load(const void *blob, size_t size, quota_preferences_t *out);
bool quota_preferences_dim_due(const quota_preferences_t *prefs, int64_t idle_us);
void quota_settings_open(quota_settings_t *settings);
quota_setting_action_t quota_settings_key(quota_settings_t *settings, const quota_preferences_t *prefs,
                                         quota_setting_key_t key, bool ap_active);
void quota_settings_saved(quota_settings_t *settings, bool success);
const char *quota_setting_name(quota_setting_item_t item);
void quota_setting_value(const quota_preferences_t *prefs, quota_setting_item_t item, char *out, size_t size);
