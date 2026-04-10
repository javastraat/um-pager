#include <Arduino.h>
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <lvgl.h>
#include "um_nav.h"
#include "um_shared.h"

static lv_obj_t *settings_root = NULL;

// -------------------------------------------------------
// Helpers
// -------------------------------------------------------
static lv_obj_t *make_section_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_width(lbl, lv_pct(100));
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_make(0, 160, 200), LV_PART_MAIN);
    lv_obj_set_style_pad_top(lbl, 6, LV_PART_MAIN);
    return lbl;
}

static lv_obj_t *make_divider(lv_obj_t *parent)
{
    lv_obj_t *div = lv_obj_create(parent);
    lv_obj_set_size(div, lv_pct(100), 1);
    lv_obj_set_style_bg_color(div, lv_color_make(30, 35, 45), LV_PART_MAIN);
    lv_obj_set_style_border_width(div, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(div, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(div, 0, LV_PART_MAIN);
    return div;
}

// Row: [symbol]  [label]                [slider ====]  [xx%]
// Returns the slider widget.
static lv_obj_t *make_slider_row(lv_obj_t *parent,
                                  const char *symbol,
                                  const char *label_text,
                                  int val_min, int val_max, int val_cur,
                                  lv_event_cb_t on_change)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, 8, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *sym = lv_label_create(row);
    lv_label_set_text(sym, symbol);
    lv_obj_set_style_text_font(sym, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(sym, lv_color_make(200, 160, 0), LV_PART_MAIN);
    lv_obj_set_width(sym, 20);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_make(200, 200, 210), LV_PART_MAIN);
    lv_obj_set_flex_grow(lbl, 1);

    lv_obj_t *slider = lv_slider_create(row);
    lv_obj_set_width(slider, 110);
    lv_slider_set_range(slider, val_min, val_max);
    lv_slider_set_value(slider, val_cur, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_make(30, 35, 50), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_make(200, 160, 0), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_make(230, 200, 60), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 4, LV_PART_KNOB);

    // value label — stored in user_data so the callback can update it
    lv_obj_t *val_lbl = lv_label_create(row);
    lv_obj_set_width(val_lbl, 46);
    lv_obj_set_style_text_font(val_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(val_lbl, lv_color_make(140, 140, 155), LV_PART_MAIN);
    lv_obj_set_style_text_align(val_lbl, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    int pct = (val_max > val_min) ? ((val_cur - val_min) * 100 / (val_max - val_min)) : 0;
    lv_label_set_text_fmt(val_lbl, "%d%%", pct);

    lv_obj_set_user_data(slider, val_lbl);
    lv_obj_add_event_cb(slider, on_change, LV_EVENT_VALUE_CHANGED, NULL);

    return slider;
}

// Info row: [symbol]  [key]         [value right-aligned]
static void make_info_row(lv_obj_t *parent, const char *symbol, const char *key, const char *value)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(row, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(row, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, 8, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *sym = lv_label_create(row);
    lv_label_set_text(sym, symbol);
    lv_obj_set_style_text_font(sym, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(sym, lv_color_make(100, 100, 120), LV_PART_MAIN);
    lv_obj_set_width(sym, 20);

    lv_obj_t *key_lbl = lv_label_create(row);
    lv_label_set_text(key_lbl, key);
    lv_obj_set_style_text_font(key_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(key_lbl, lv_color_make(180, 180, 195), LV_PART_MAIN);
    lv_obj_set_flex_grow(key_lbl, 1);

    lv_obj_t *val_lbl = lv_label_create(row);
    lv_label_set_text(val_lbl, value);
    lv_obj_set_style_text_font(val_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(val_lbl, lv_color_make(100, 100, 120), LV_PART_MAIN);
    lv_obj_set_style_text_align(val_lbl, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_width(val_lbl, lv_pct(50));
    lv_label_set_long_mode(val_lbl, LV_LABEL_LONG_SCROLL);
}

// -------------------------------------------------------
// Slider callbacks
// -------------------------------------------------------
static void disp_brightness_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    instance.setBrightness((uint8_t)val);
    lv_obj_t *lbl = (lv_obj_t *)lv_obj_get_user_data(slider);
    if (lbl) lv_label_set_text_fmt(lbl, "%d%%", val * 100 / 255);
}

static void kb_brightness_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    instance.kb.setBrightness((uint8_t)val);
    lv_obj_t *lbl = (lv_obj_t *)lv_obj_get_user_data(slider);
    if (lbl) lv_label_set_text_fmt(lbl, "%d%%", val * 100 / 255);
}

static void sleep_timeout_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int val = lv_slider_get_value(slider); // seconds
    um_sleep_timeout_ms = (uint32_t)(val * 1000);
    lv_obj_t *lbl = (lv_obj_t *)lv_obj_get_user_data(slider);
    if (!lbl) return;
    if (val == 0) lv_label_set_text(lbl, "Never");
    else          lv_label_set_text_fmt(lbl, "%ds", val);
}

// -------------------------------------------------------
// Key handler (ESC/Backspace → back)
// -------------------------------------------------------
static void settings_key_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) um_nav_back();
}

// -------------------------------------------------------
// Build screen
// -------------------------------------------------------
void um_settings_create()
{
    // ---- Root (scrollable column) ----
    settings_root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(settings_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(settings_root, lv_color_make(4, 6, 10), LV_PART_MAIN);
    lv_obj_set_style_border_width(settings_root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(settings_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(settings_root, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(settings_root, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(settings_root, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(settings_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(settings_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(settings_root, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(settings_root, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(settings_root, settings_key_cb, LV_EVENT_KEY, NULL);

    // ---- Header ----
    lv_obj_t *hdr = lv_obj_create(settings_root);
    lv_obj_set_width(hdr, lv_pct(100));
    lv_obj_set_height(hdr, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(hdr, 8, LV_PART_MAIN);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *ico = lv_label_create(hdr);
    lv_label_set_text(ico, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_22, LV_PART_MAIN);
    lv_obj_set_style_text_color(ico, lv_color_make(200, 160, 0), LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_make(220, 220, 230), LV_PART_MAIN);

    make_divider(settings_root);

    // ---- Section: Display ----
    make_section_label(settings_root, LV_SYMBOL_IMAGE "  DISPLAY");

    lv_obj_t *disp_slider = make_slider_row(
        settings_root,
        LV_SYMBOL_IMAGE, "Brightness",
        0, 255, (int)instance.getBrightness(),
        disp_brightness_cb
    );

    lv_obj_t *kb_slider = make_slider_row(
        settings_root,
        LV_SYMBOL_KEYBOARD, "Keyboard backlight",
        0, 255, (int)instance.kb.getBrightness(),
        kb_brightness_cb
    );

    int init_timeout_s = (int)(um_sleep_timeout_ms / 1000);
    lv_obj_t *timeout_slider = make_slider_row(
        settings_root,
        LV_SYMBOL_POWER, "Sleep timeout",
        0, 180, init_timeout_s,
        sleep_timeout_cb
    );
    // Override the "XX%" label that make_slider_row set
    lv_obj_t *timeout_lbl = (lv_obj_t *)lv_obj_get_user_data(timeout_slider);
    if (timeout_lbl) {
        if (init_timeout_s == 0) lv_label_set_text(timeout_lbl, "Never");
        else                     lv_label_set_text_fmt(timeout_lbl, "%ds", init_timeout_s);
    }

    make_divider(settings_root);

    // ---- Section: System ----
    make_section_label(settings_root, LV_SYMBOL_EYE_OPEN "  SYSTEM");

    char lvgl_ver[16];
    snprintf(lvgl_ver, sizeof(lvgl_ver), "v%d.%d.%d",
             lv_version_major(), lv_version_minor(), lv_version_patch());

    make_info_row(settings_root, LV_SYMBOL_WIFI,     "Node",    NODE_NAME);
    make_info_row(settings_root, LV_SYMBOL_EYE_OPEN, "LVGL",   lvgl_ver);
    make_info_row(settings_root, LV_SYMBOL_EYE_OPEN, "Built",  __DATE__ " " __TIME__);

    make_divider(settings_root);

    // ---- Section: Firmware ----
    make_section_label(settings_root, LV_SYMBOL_DOWNLOAD "  FIRMWARE");

    lv_obj_t *ota_btn = lv_btn_create(settings_root);
    lv_obj_set_width(ota_btn, lv_pct(100));
    lv_obj_set_height(ota_btn, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(ota_btn, lv_color_make(15, 25, 40), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ota_btn, lv_color_make(0, 60, 100),
                              (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_border_color(ota_btn, lv_color_make(0, 100, 160), LV_PART_MAIN);
    lv_obj_set_style_border_width(ota_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(ota_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(ota_btn, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ota_btn, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(ota_btn, [](lv_event_t *e) {
        um_otaRequested = true;
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ota_btn, settings_key_cb, LV_EVENT_KEY, NULL);

    lv_obj_t *ota_lbl = lv_label_create(ota_btn);
    lv_label_set_text(ota_lbl, LV_SYMBOL_DOWNLOAD "  OTA Update via WiFi");
    lv_obj_set_style_text_font(ota_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(ota_lbl, lv_color_make(0, 180, 255), LV_PART_MAIN);

    lv_obj_t *ota_hint = lv_label_create(settings_root);
    lv_label_set_text(ota_hint, "Connects to WiFi and waits for a PlatformIO OTA upload.");
    lv_obj_set_width(ota_hint, lv_pct(100));
    lv_label_set_long_mode(ota_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(ota_hint, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_set_style_text_color(ota_hint, lv_color_make(70, 75, 90), LV_PART_MAIN);

    make_divider(settings_root);

    // ---- Back button ----
    lv_obj_t *back_btn = lv_btn_create(settings_root);
    lv_obj_set_width(back_btn, 160);
    lv_obj_set_height(back_btn, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(back_btn, lv_color_make(20, 20, 28), LV_PART_MAIN);
    lv_obj_set_style_border_color(back_btn, lv_color_make(60, 60, 80), LV_PART_MAIN);
    lv_obj_set_style_border_width(back_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(back_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(back_btn, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) { um_nav_back(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(back_btn, settings_key_cb, LV_EVENT_KEY, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT "  Back");
    lv_obj_set_style_text_color(back_lbl, lv_color_make(160, 160, 170), LV_PART_MAIN);
    lv_obj_center(back_lbl);

    // ---- Focus group ----
    lv_group_t *g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, settings_root);   // catches ESC at root level
        lv_group_add_obj(g, disp_slider);
        lv_group_add_obj(g, kb_slider);
        lv_group_add_obj(g, timeout_slider);
        lv_group_add_obj(g, ota_btn);
        lv_group_add_obj(g, back_btn);
        lv_group_focus_obj(disp_slider);
    }
}

void um_settings_destroy()
{
    if (!settings_root) return;
    lv_group_t *g = lv_group_get_default();
    if (g) lv_group_remove_all_objs(g);
    lv_obj_del(settings_root);
    settings_root = NULL;
}
