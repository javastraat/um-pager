#include <Arduino.h>
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <lvgl.h>
#include "um_nav.h"
#include "um_shared.h"
#include "config.h"
#ifndef SIM_BUILD
#include <Preferences.h>
#endif

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
    lv_obj_set_style_text_color(lbl, um_col_cyan(), LV_PART_MAIN);
    lv_obj_set_style_pad_top(lbl, 6, LV_PART_MAIN);
    return lbl;
}

static lv_obj_t *make_divider(lv_obj_t *parent)
{
    lv_obj_t *div = lv_obj_create(parent);
    lv_obj_set_size(div, lv_pct(100), 1);
    lv_obj_set_style_bg_color(div, um_col_divider(), LV_PART_MAIN);
    lv_obj_set_style_border_width(div, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(div, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(div, 0, LV_PART_MAIN);
    return div;
}

// Row: [symbol]  [label]          [slider ====]  [val]
// Returns the slider. val_lbl pointer stored in slider user_data.
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
    lv_obj_set_style_text_color(sym, um_col_yellow(), LV_PART_MAIN);
    lv_obj_set_width(sym, 20);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, um_col_text_sub(), LV_PART_MAIN);
    lv_obj_set_flex_grow(lbl, 1);

    lv_obj_t *slider = lv_slider_create(row);
    lv_obj_add_flag(slider, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_width(slider, 110);
    lv_slider_set_range(slider, val_min, val_max);
    lv_slider_set_value(slider, val_cur, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, um_col_surface_deep(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, um_col_yellow(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, UM_COL(230,200,60, 200,158,0), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 4, LV_PART_KNOB);
    // Editing state: brighter knob + outline so the user can see edit mode is active
    lv_obj_set_style_bg_color(slider, lv_color_make(255, 230, 0),
                              (lv_style_selector_t)((int)LV_STATE_EDITED | (int)LV_PART_KNOB));
    lv_obj_set_style_pad_all(slider, 7,
                              (lv_style_selector_t)((int)LV_STATE_EDITED | (int)LV_PART_KNOB));
    lv_obj_set_style_outline_color(slider, lv_color_make(255, 220, 0),
                              (lv_style_selector_t)((int)LV_STATE_EDITED | (int)LV_PART_MAIN));
    lv_obj_set_style_outline_width(slider, 2,
                              (lv_style_selector_t)((int)LV_STATE_EDITED | (int)LV_PART_MAIN));

    lv_obj_t *val_lbl = lv_label_create(row);
    lv_obj_set_width(val_lbl, 46);
    lv_obj_set_style_text_font(val_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(val_lbl, um_col_text_hint(), LV_PART_MAIN);
    lv_obj_set_style_text_align(val_lbl, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    int pct = (val_max > val_min) ? ((val_cur - val_min) * 100 / (val_max - val_min)) : 0;
    lv_label_set_text_fmt(val_lbl, "%d%%", pct);

    lv_obj_set_user_data(slider, val_lbl);
    lv_obj_add_event_cb(slider, on_change, LV_EVENT_VALUE_CHANGED, NULL);

    return slider;
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

// Timeout callbacks — label shows "Never" at 0, else "Xs"
static void dim_timeout_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    um_dim_timeout_ms = (uint32_t)(val * 1000);
    lv_obj_t *lbl = (lv_obj_t *)lv_obj_get_user_data(slider);
    if (!lbl) return;
    if (val == 0) lv_label_set_text(lbl, "Never");
    else          lv_label_set_text_fmt(lbl, "%ds", val);
}

static void dim_brightness_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    um_dim_brightness = (uint8_t)val;
    lv_obj_t *lbl = (lv_obj_t *)lv_obj_get_user_data(slider);
    if (!lbl) return;
    if (val == 0) lv_label_set_text(lbl, "Off");
    else          lv_label_set_text_fmt(lbl, "%d%%", val * 100 / 255);
}

static void sleep_timeout_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    um_sleep_timeout_ms = (uint32_t)(val * 1000);
    lv_obj_t *lbl = (lv_obj_t *)lv_obj_get_user_data(slider);
    if (!lbl) return;
    if (val == 0) lv_label_set_text(lbl, "Never");
    else          lv_label_set_text_fmt(lbl, "%ds", val);
}

// -------------------------------------------------------
// Key handlers
// -------------------------------------------------------
static void settings_key_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) {
        um_settings_save();
        um_nav_back();
    }
}

// LilyGoLib registers the encoder as a keypad indev, so LVGL never
// automatically toggles the group's edit mode. We manage it manually:
//   ENTER          → enter / exit edit mode (LV_STATE_EDITED)
//   LEFT/RIGHT while editing → change slider value, consume the event
//   ESC/BACKSPACE while editing → exit edit mode (consume event)
//   ESC/BACKSPACE while not editing → go back
static const int SLIDER_STEP = UM_SETTINGS_SLIDER_STEP;

static void slider_key_cb(lv_event_t *e)
{
    lv_obj_t *slider  = (lv_obj_t *)lv_event_get_target(e);
    uint32_t  key     = lv_event_get_key(e);
    bool      editing = lv_obj_has_state(slider, LV_STATE_EDITED);

    if (key == LV_KEY_ENTER) {
        if (editing) lv_obj_clear_state(slider, LV_STATE_EDITED);
        else         lv_obj_add_state(slider,   LV_STATE_EDITED);
        lv_event_stop_processing(e);
        return;
    }

    if (editing) {
        int delta = 0;
        if      (key == LV_KEY_RIGHT || key == LV_KEY_UP)   delta = +SLIDER_STEP;
        else if (key == LV_KEY_LEFT  || key == LV_KEY_DOWN) delta = -SLIDER_STEP;
        else if (key == LV_KEY_ESC   || key == LV_KEY_BACKSPACE) {
            lv_obj_clear_state(slider, LV_STATE_EDITED);
            lv_event_stop_processing(e);
            return;
        }

        if (delta != 0) {
            int32_t minv = lv_slider_get_min_value(slider);
            int32_t maxv = lv_slider_get_max_value(slider);
            int32_t val  = lv_slider_get_value(slider) + delta;
            if (val < minv) val = minv;
            if (val > maxv) val = maxv;
            lv_slider_set_value(slider, val, LV_ANIM_OFF);
            lv_obj_send_event(slider, LV_EVENT_VALUE_CHANGED, NULL);
            lv_event_stop_processing(e);
        }
        return;
    }

    // Not editing — ESC/Backspace → back
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) {
        um_settings_save();
        um_nav_back();
    }
}

// Helper: fix a timeout slider label after make_slider_row initialises it as "%"
static void fix_timeout_label(lv_obj_t *slider, int val_s)
{
    lv_obj_t *lbl = (lv_obj_t *)lv_obj_get_user_data(slider);
    if (!lbl) return;
    if (val_s == 0) lv_label_set_text(lbl, "Never");
    else            lv_label_set_text_fmt(lbl, "%ds", val_s);
}

// -------------------------------------------------------
// NVS persistence
// -------------------------------------------------------
void um_settings_load()
{
#ifndef SIM_BUILD
    Preferences p;
    p.begin("um", false); // read-write so the namespace is created on first boot
    instance.setBrightness(p.getUChar("disp_br",  DEVICE_MAX_BRIGHTNESS_LEVEL));
    instance.kb.setBrightness(p.getUChar("kb_br", UM_DEFAULT_KB_BRIGHTNESS));
    um_dim_timeout_ms   = p.getUInt ("dim_to",   UM_DEFAULT_DIM_TIMEOUT_MS);
    um_dim_brightness   = p.getUChar("dim_br",   UM_DEFAULT_DIM_BRIGHTNESS);
    um_sleep_timeout_ms = p.getUInt ("sleep_to", UM_DEFAULT_SLEEP_TIMEOUT_MS);
    um_active_theme     = (um_theme_t)p.getUChar("theme",  UM_THEME_DARK);
    p.end();
#endif
}

void um_settings_save()
{
#ifndef SIM_BUILD
    Preferences p;
    p.begin("um", false); // read-write
    p.putUChar("disp_br",  instance.getBrightness());
    p.putUChar("kb_br",    instance.kb.getBrightness());
    p.putUInt ("dim_to",   (uint32_t)um_dim_timeout_ms);
    p.putUChar("dim_br",   (uint8_t)um_dim_brightness);
    p.putUInt ("sleep_to", (uint32_t)um_sleep_timeout_ms);
    p.putUChar("theme",    (uint8_t)um_active_theme);
    p.end();
#endif
}

// -------------------------------------------------------
// Build screen
// -------------------------------------------------------
void um_settings_create()
{
    // ---- Root — non-scrollable column (matches messages/nfc pattern) ----
    settings_root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(settings_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(settings_root, um_col_bg(), LV_PART_MAIN);
    lv_obj_set_style_border_width(settings_root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(settings_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(settings_root, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(settings_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(settings_root, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(settings_root, LV_OBJ_FLAG_SCROLLABLE);

    // ---- Top bar ----
    lv_obj_t *hdr = lv_obj_create(settings_root);
    lv_obj_set_width(hdr, lv_pct(100));
    lv_obj_set_height(hdr, 36);
    lv_obj_set_style_bg_color(hdr, um_col_surface(), LV_PART_MAIN);
    lv_obj_set_style_border_width(hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(hdr, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(hdr, 6, LV_PART_MAIN);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, UM_SYMBOL_SETTINGS "  Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, um_col_text(), LV_PART_MAIN);
    lv_obj_set_flex_grow(title, 1);

    // Home button (top-right)
    lv_obj_t *home_btn = lv_btn_create(hdr);
    lv_obj_set_size(home_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(home_btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(home_btn, um_col_focus_cyan(),
        (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_bg_opa(home_btn, LV_OPA_COVER,
        (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_border_width(home_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(home_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(home_btn, 2, LV_PART_MAIN);
    lv_obj_add_event_cb(home_btn, [](lv_event_t *) { um_settings_save(); um_nav_back(); },
                        LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(home_btn, settings_key_cb, LV_EVENT_KEY, NULL);
    lv_obj_t *home_lbl = lv_label_create(home_btn);
    lv_label_set_text(home_lbl, UM_SYMBOL_HOME);
    lv_obj_set_style_text_color(home_lbl, um_col_cyan(), LV_PART_MAIN);
    lv_obj_center(home_lbl);

    // ---- Scrollable content area ----
    lv_obj_t *scroll = lv_obj_create(settings_root);
    lv_obj_set_width(scroll, lv_pct(100));
    lv_obj_set_flex_grow(scroll, 1);
    lv_obj_set_style_bg_color(scroll, um_col_bg(), LV_PART_MAIN);
    lv_obj_set_style_border_width(scroll, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(scroll, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(scroll, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(scroll, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(scroll, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scroll, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_ACTIVE);

    lv_group_t *g = lv_group_get_default();

    // ---- Section: Appearance ----
    // Section label is first focusable item — navigating back up to it
    // scrolls all the way to the top (same fix as info/nfc screens).
    lv_obj_t *appear_lbl = make_section_label(scroll, LV_SYMBOL_EYE_OPEN "  APPEARANCE");
    lv_obj_add_flag(appear_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(appear_lbl, um_col_focus_cyan(),
        (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_bg_opa(appear_lbl, LV_OPA_COVER,
        (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_add_event_cb(appear_lbl, [](lv_event_t *ev) {
        lv_obj_scroll_to_view(lv_event_get_target_obj(ev), LV_ANIM_ON);
    }, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(appear_lbl, settings_key_cb, LV_EVENT_KEY, NULL);
    if (g) lv_group_add_obj(g, appear_lbl);

    {
        lv_obj_t *row = lv_obj_create(scroll);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 4, LV_PART_MAIN);
        lv_obj_set_style_pad_column(row, 8, LV_PART_MAIN);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *ico = lv_label_create(row);
        lv_label_set_text(ico, LV_SYMBOL_IMAGE);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(ico, um_col_yellow(), LV_PART_MAIN);
        lv_obj_set_width(ico, 20);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, "Dark mode");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, um_col_text_sub(), LV_PART_MAIN);
        lv_obj_set_flex_grow(lbl, 1);

        lv_obj_t *sw = lv_switch_create(row);
        if (um_active_theme == UM_THEME_DARK) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_flag(sw, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_set_style_bg_color(sw, um_col_divider(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(sw, um_col_cyan(),
                                  (lv_style_selector_t)((int)LV_STATE_CHECKED | (int)LV_PART_MAIN));
        lv_obj_set_style_bg_color(sw, um_col_text_hint(), LV_PART_KNOB);
        lv_obj_set_style_bg_color(sw, um_col_text(),
                                  (lv_style_selector_t)((int)LV_STATE_CHECKED | (int)LV_PART_KNOB));
        lv_obj_add_event_cb(sw, [](lv_event_t *e) {
            lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
            um_active_theme = lv_obj_has_state(sw, LV_STATE_CHECKED)
                              ? UM_THEME_DARK : UM_THEME_LIGHT;
            um_settings_save();
            um_nav_go(UM_SCREEN_MENU);
        }, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_add_event_cb(sw, settings_key_cb, LV_EVENT_KEY, NULL);
        if (g) lv_group_add_obj(g, sw);
    }

    make_divider(scroll);

    // ---- Section: Display ----
    make_section_label(scroll, LV_SYMBOL_IMAGE "  DISPLAY");

    lv_obj_t *disp_slider = make_slider_row(
        scroll,
        LV_SYMBOL_IMAGE, "Brightness",
        0, 255, (int)instance.getBrightness(),
        disp_brightness_cb
    );

    lv_obj_t *kb_slider = make_slider_row(
        scroll,
        LV_SYMBOL_KEYBOARD, "Keyboard backlight",
        0, 255, (int)instance.kb.getBrightness(),
        kb_brightness_cb
    );

    make_divider(scroll);

    // ---- Section: Power saving ----
    make_section_label(scroll, LV_SYMBOL_POWER "  POWER SAVING");

    int init_dim_s   = (int)(um_dim_timeout_ms / 1000);
    int init_dim_val = (int)um_dim_brightness;
    int init_sleep_s = (int)(um_sleep_timeout_ms / 1000);

    lv_obj_t *dim_timeout_slider = make_slider_row(
        scroll,
        LV_SYMBOL_IMAGE, "Dim after",
        0, UM_SETTINGS_TIMEOUT_MAX_S, init_dim_s,
        dim_timeout_cb
    );
    fix_timeout_label(dim_timeout_slider, init_dim_s);

    lv_obj_t *dim_brightness_slider = make_slider_row(
        scroll,
        LV_SYMBOL_TINT, "Dim level",
        0, 255, init_dim_val,
        dim_brightness_cb
    );
    {
        lv_obj_t *lbl = (lv_obj_t *)lv_obj_get_user_data(dim_brightness_slider);
        if (lbl) {
            if (init_dim_val == 0) lv_label_set_text(lbl, "Off");
            else lv_label_set_text_fmt(lbl, "%d%%", init_dim_val * 100 / 255);
        }
    }

    lv_obj_t *sleep_timeout_slider = make_slider_row(
        scroll,
        LV_SYMBOL_POWER, "Sleep after",
        0, UM_SETTINGS_TIMEOUT_MAX_S, init_sleep_s,
        sleep_timeout_cb
    );
    fix_timeout_label(sleep_timeout_slider, init_sleep_s);

    // ---- Focus group ----
    // Order: appear_lbl (top anchor) → sw → sliders → home_btn
    if (g) {
        lv_obj_add_event_cb(disp_slider,           slider_key_cb, LV_EVENT_KEY, NULL);
        lv_obj_add_event_cb(kb_slider,             slider_key_cb, LV_EVENT_KEY, NULL);
        lv_obj_add_event_cb(dim_timeout_slider,    slider_key_cb, LV_EVENT_KEY, NULL);
        lv_obj_add_event_cb(dim_brightness_slider, slider_key_cb, LV_EVENT_KEY, NULL);
        lv_obj_add_event_cb(sleep_timeout_slider,  slider_key_cb, LV_EVENT_KEY, NULL);

        lv_group_add_obj(g, disp_slider);
        lv_group_add_obj(g, kb_slider);
        lv_group_add_obj(g, dim_timeout_slider);
        lv_group_add_obj(g, dim_brightness_slider);
        lv_group_add_obj(g, sleep_timeout_slider);
        lv_group_add_obj(g, home_btn);
        lv_group_focus_obj(appear_lbl);
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
