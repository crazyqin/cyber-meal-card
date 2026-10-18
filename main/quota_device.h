#pragma once
#include "quota_settings.h"

/* Application-task calls only; never call NVS or audio from a key/LVGL callback. */
void quota_device_load(quota_preferences_t *prefs);
bool quota_device_save(const quota_preferences_t *prefs);
void quota_device_preview_volume(uint8_t volume);
/* Wait at most one second for audio to stop before a Flash write. */
bool quota_device_stop_preview(void);
bool quota_device_audio_failed(void);
