/* Exercise the real LVGL built-in formatter in the firmware's no-float mode. */
#include "src/lv_conf_internal.h"
#include "src/stdlib/lv_sprintf.h"
#include "quota_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

_Static_assert(LV_USE_FLOAT == 0, "Regression must cover the no-float profile");
_Static_assert(LV_USE_STDLIB_SPRINTF == LV_STDLIB_BUILTIN, "Use the built-in formatter");

/* The isolated formatter only needs this string primitive, not an LVGL
 * display, memory pool or OS port. */
size_t lv_strnlen(const char *text, size_t maximum) {
    size_t n = 0;
    while (n < maximum && text[n]) n++;
    return n;
}

int main(void) {
    char old[16], percent[16], display[64];
    lv_snprintf(old, sizeof(old), "%.0f%%", 87.0);
    assert(!strcmp(old, "f%")); /* Reproduces the user's photographed defect. */
    const float values[] = {0, 87.4f, 87.5f, 100};
    const char *expected[] = {"0%", "87%", "88%", "100%"};
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        quota_percent_label(values[i], percent, sizeof(percent));
        lv_snprintf(display, sizeof(display), "%s", percent);
        assert(!strcmp(display, expected[i]));
    }
    quota_window_t window = {.minutes = 300, .remaining = 100};
    quota_window_detail_label(&window, 1800000000, display, sizeof(display));
    assert(!strcmp(display, "尚未开始计时"));
    puts("LVGL no-float percentage regression: PASS (old f% reproduced, fixed labels verified)");
    return 0;
}
