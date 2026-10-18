#include "quota_settings.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    quota_preferences_t prefs, loaded;
    quota_preferences_default(&prefs);
    assert(prefs.volume == 40 && prefs.brightness == 75 && prefs.dim_seconds == 45 && !prefs.automatic);
    assert(quota_preferences_valid(&prefs));
    assert(quota_preferences_load(&prefs, sizeof(prefs), &loaded));
    assert(!memcmp(&prefs, &loaded, sizeof(prefs)));
    assert(!quota_preferences_load(&prefs, sizeof(prefs)-1, &loaded));
    assert(loaded.brightness == 75);
    quota_preferences_t bad=prefs; bad.brightness=0;
    assert(!quota_preferences_load(&bad, sizeof(bad), &loaded));
    bad=prefs; bad.volume=101; assert(!quota_preferences_valid(&bad));
    bad=prefs; bad.dim_seconds=46; assert(!quota_preferences_valid(&bad));
    bad=prefs; bad.version=2; assert(!quota_preferences_valid(&bad));
    bad=prefs; bad.automatic=2; assert(!quota_preferences_valid(&bad));
    assert(!quota_preferences_dim_due(&prefs, 44999999));
    assert(quota_preferences_dim_due(&prefs, 45000000));
    prefs.dim_seconds=0; assert(!quota_preferences_dim_due(&prefs, INT64_MAX));
    quota_preferences_default(&prefs);
    quota_settings_t s;
    quota_settings_open(&s);
    quota_settings_key(&s,&prefs,QUOTA_SETTING_UP,false);
    assert(s.selected == QUOTA_SETTING_ROTATION);
    quota_settings_key(&s,&prefs,QUOTA_SETTING_DOWN,false);
    assert(s.selected == QUOTA_SETTING_WIFI);
    quota_settings_key(&s,&prefs,QUOTA_SETTING_OK,false);
    assert(s.page == QUOTA_SETTINGS_WIFI);
    assert(quota_settings_key(&s,&prefs,QUOTA_SETTING_OK,false) == QUOTA_SETTING_CONNECT);
    quota_settings_key(&s,&prefs,QUOTA_SETTING_DOWN,false);
    assert(quota_settings_key(&s,&prefs,QUOTA_SETTING_OK,false) == QUOTA_SETTING_OPEN_AP);
    assert(quota_settings_key(&s,&prefs,QUOTA_SETTING_OK,false) == QUOTA_SETTING_NONE);
    assert(quota_settings_key(&s,&prefs,QUOTA_SETTING_BACK,false) == QUOTA_SETTING_CLOSE_AP);
    assert(s.page == QUOTA_SETTINGS_WIFI && !s.ap_requested);
    assert(quota_settings_key(&s,&prefs,QUOTA_SETTING_OK,true) == QUOTA_SETTING_CONNECT);
    assert(quota_settings_key(&s,&prefs,QUOTA_SETTING_BACK,true) == QUOTA_SETTING_CLOSE_AP);
    quota_settings_key(&s,&prefs,QUOTA_SETTING_BACK,false);
    assert(s.page == QUOTA_SETTINGS_ROOT);
    quota_settings_key(&s,&prefs,QUOTA_SETTING_DOWN,false);
    quota_settings_key(&s,&prefs,QUOTA_SETTING_OK,false);
    assert(s.selected == QUOTA_SETTING_VOLUME && s.page == QUOTA_SETTINGS_EDIT);
    for (int i=0;i<30;i++) quota_settings_key(&s,&prefs,QUOTA_SETTING_UP,false);
    assert(s.draft.volume == 100 && prefs.volume == 40);
    for (int i=0;i<30;i++) quota_settings_key(&s,&prefs,QUOTA_SETTING_DOWN,false);
    assert(s.draft.volume == 0);
    assert(quota_settings_key(&s,&prefs,QUOTA_SETTING_OK,false) == QUOTA_SETTING_SAVE);
    quota_settings_saved(&s,false);
    assert(s.page == QUOTA_SETTINGS_EDIT && s.save_failed && prefs.volume == 40);
    assert(quota_settings_key(&s,&prefs,QUOTA_SETTING_OK,false) == QUOTA_SETTING_SAVE);
    prefs=s.draft; quota_settings_saved(&s,true);
    assert(s.page == QUOTA_SETTINGS_ROOT && s.saved && !prefs.volume);
    quota_settings_key(&s,&prefs,QUOTA_SETTING_DOWN,false);
    quota_settings_key(&s,&prefs,QUOTA_SETTING_OK,false);
    for (int i=0;i<30;i++) quota_settings_key(&s,&prefs,QUOTA_SETTING_DOWN,false);
    assert(s.draft.brightness == 10);
    assert(quota_settings_key(&s,&prefs,QUOTA_SETTING_BACK,false) == QUOTA_SETTING_CANCEL);
    assert(prefs.brightness == 75 && s.page == QUOTA_SETTINGS_ROOT);
    quota_settings_key(&s,&prefs,QUOTA_SETTING_DOWN,false);
    quota_settings_key(&s,&prefs,QUOTA_SETTING_OK,false);
    for (int i=0;i<7;i++) { quota_settings_key(&s,&prefs,QUOTA_SETTING_UP,false); assert(quota_preferences_valid(&s.draft)); }
    assert(s.draft.dim_seconds == prefs.dim_seconds);
    quota_settings_key(&s,&prefs,QUOTA_SETTING_BACK,false);
    quota_settings_key(&s,&prefs,QUOTA_SETTING_DOWN,false);
    quota_settings_key(&s,&prefs,QUOTA_SETTING_OK,false);
    quota_settings_key(&s,&prefs,QUOTA_SETTING_UP,false);
    assert(s.draft.automatic && !prefs.automatic);
    quota_settings_key(&s,&prefs,QUOTA_SETTING_BACK,false);
    quota_settings_key(&s,&prefs,QUOTA_SETTING_BACK,false);
    assert(s.page == QUOTA_SETTINGS_CLOSED);
    puts("Device settings: defaults, storage validation, navigation, bounds, cancel and save retry: PASS");
}
