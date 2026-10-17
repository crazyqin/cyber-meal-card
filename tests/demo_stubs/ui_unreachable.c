/* COFF linkers resolve references in unused demo UI sections before GC.
 * Keep these symbols typed and fail if a lifecycle test actually enters UI. */
#include "demo_test_stubs.h"
#include "ui_pixel.h"
#include <stdlib.h>
#define STOP() abort()
int esp_sleep_get_wakeup_cause(void) { STOP(); }
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *n) { (void)n; STOP(); }
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *n, wifi_ap_record_t *r) { (void)n; (void)r; STOP(); }
uint32_t lv_color_hex(uint32_t c) { (void)c; STOP(); }
lv_obj_t *lv_obj_create(lv_obj_t *p) { (void)p; STOP(); }
lv_obj_t *lv_label_create(lv_obj_t *p) { (void)p; STOP(); }
void lv_obj_set_size(lv_obj_t *o, int w, int h) { (void)o; (void)w; (void)h; STOP(); }
void lv_obj_set_width(lv_obj_t *o, int w) { (void)o; (void)w; STOP(); }
void lv_obj_set_style_radius(lv_obj_t *o, int r, int s) { (void)o; (void)r; (void)s; STOP(); }
void lv_obj_set_style_bg_color(lv_obj_t *o, uint32_t c, int s) { (void)o; (void)c; (void)s; STOP(); }
void lv_obj_set_style_border_width(lv_obj_t *o, int w, int s) { (void)o; (void)w; (void)s; STOP(); }
void lv_obj_set_style_text_color(lv_obj_t *o, uint32_t c, int s) { (void)o; (void)c; (void)s; STOP(); }
void lv_obj_set_style_text_align(lv_obj_t *o, int a, int s) { (void)o; (void)a; (void)s; STOP(); }
void lv_obj_set_style_text_font(lv_obj_t *o, const lv_font_t *f, int s) { (void)o; (void)f; (void)s; STOP(); }
void lv_obj_center(lv_obj_t *o) { (void)o; STOP(); }
void lv_obj_align(lv_obj_t *o, int a, int x, int y) { (void)o; (void)a; (void)x; (void)y; STOP(); }
void lv_obj_delete(lv_obj_t *o) { (void)o; STOP(); }
void lv_label_set_text_fmt(lv_obj_t *o, const char *f, ...) { (void)o; (void)f; STOP(); }
void lv_label_set_long_mode(lv_obj_t *o, int m) { (void)o; (void)m; STOP(); }
void lv_screen_load(lv_obj_t *o) { (void)o; STOP(); }
lv_timer_t *lv_timer_create(void (*c)(lv_timer_t *), unsigned p, void *d) { (void)c; (void)p; (void)d; STOP(); }
void lv_timer_delete(lv_timer_t *t) { (void)t; STOP(); }
lv_obj_t *ui_pixel_screen_create(const char *t) { (void)t; STOP(); }
lv_obj_t *ui_pixel_panel_create(lv_obj_t *p, int x, int y, int w, int h, uint32_t c) { (void)p; (void)x; (void)y; (void)w; (void)h; (void)c; STOP(); }
lv_obj_t *ui_pixel_label(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t c) { (void)p; (void)t; (void)f; (void)c; STOP(); }
lv_obj_t *ui_pixel_mascot_create(lv_obj_t *p, int x, int y) { (void)p; (void)x; (void)y; STOP(); }
