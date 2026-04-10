#include <Arduino.h>
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <lvgl.h>
#include "um_nav.h"
#include "um_shared.h"   // pulls in um_theme.h

static lv_obj_t *info_root = NULL;

// -------------------------------------------------------
// Helpers (local to this screen)
// -------------------------------------------------------
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
    lv_obj_set_style_text_color(sym, um_col_text_hint(), LV_PART_MAIN);
    lv_obj_set_width(sym, 20);

    lv_obj_t *key_lbl = lv_label_create(row);
    lv_label_set_text(key_lbl, key);
    lv_obj_set_style_text_font(key_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(key_lbl, um_col_text_sub(), LV_PART_MAIN);
    lv_obj_set_flex_grow(key_lbl, 1);

    lv_obj_t *val_lbl = lv_label_create(row);
    lv_label_set_text(val_lbl, value);
    lv_obj_set_style_text_font(val_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(val_lbl, um_col_text_hint(), LV_PART_MAIN);
    lv_obj_set_style_text_align(val_lbl, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_width(val_lbl, lv_pct(50));
    lv_label_set_long_mode(val_lbl, LV_LABEL_LONG_SCROLL);
}

// -------------------------------------------------------
// Key handler
// -------------------------------------------------------
static void info_key_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) um_nav_back();
}

// -------------------------------------------------------
// Build screen
// -------------------------------------------------------
void um_info_create()
{
    // ---- Root (scrollable column) ----
    info_root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(info_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(info_root, um_col_bg(), LV_PART_MAIN);
    lv_obj_set_style_border_width(info_root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(info_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(info_root, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(info_root, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(info_root, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(info_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(info_root, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(info_root, LV_SCROLLBAR_MODE_OFF);

    // ---- Header ----
    lv_obj_t *hdr = lv_obj_create(info_root);
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
    lv_label_set_text(ico, LV_SYMBOL_LIST);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_22, LV_PART_MAIN);
    lv_obj_set_style_text_color(ico, um_col_purple(), LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "System Info");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, um_col_text(), LV_PART_MAIN);

    make_divider(info_root);

    // ---- Section: Device ----
    make_section_label(info_root, LV_SYMBOL_EYE_OPEN "  DEVICE");

    char lvgl_ver[16];
    snprintf(lvgl_ver, sizeof(lvgl_ver), "v%d.%d.%d",
             lv_version_major(), lv_version_minor(), lv_version_patch());

    make_info_row(info_root, LV_SYMBOL_WIFI,     "Node",    NODE_NAME);
    make_info_row(info_root, LV_SYMBOL_EYE_OPEN, "LVGL",   lvgl_ver);
    make_info_row(info_root, LV_SYMBOL_EYE_OPEN, "Built",  __DATE__ " " __TIME__);

    make_divider(info_root);

    // ---- Section: Firmware update ----
    make_section_label(info_root, LV_SYMBOL_DOWNLOAD "  FIRMWARE");

    // -- Button: OTA via WiFi (existing) --
    lv_obj_t *ota_btn = lv_btn_create(info_root);
    lv_obj_set_width(ota_btn, lv_pct(100));
    lv_obj_set_height(ota_btn, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(ota_btn, um_col_surface(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ota_btn, um_col_focus_cyan(),
                              (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_border_color(ota_btn, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_border_width(ota_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(ota_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(ota_btn, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ota_btn, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(ota_btn, [](lv_event_t *) {
        um_otaRequested = true;
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ota_btn, info_key_cb, LV_EVENT_KEY, NULL);
    lv_obj_t *ota_lbl = lv_label_create(ota_btn);
    lv_label_set_text(ota_lbl, LV_SYMBOL_WIFI "  OTA Update via WiFi");
    lv_obj_set_style_text_font(ota_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(ota_lbl, um_col_cyan_bright(), LV_PART_MAIN);

    lv_obj_t *ota_hint = lv_label_create(info_root);
    lv_label_set_text(ota_hint, "Connects to WiFi and waits for a PlatformIO OTA upload.");
    lv_obj_set_width(ota_hint, lv_pct(100));
    lv_label_set_long_mode(ota_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(ota_hint, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_set_style_text_color(ota_hint, um_col_text_hint(), LV_PART_MAIN);

    // -- Button: Download latest from GitHub --
    lv_obj_t *dl_btn = lv_btn_create(info_root);
    lv_obj_set_width(dl_btn, lv_pct(100));
    lv_obj_set_height(dl_btn, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(dl_btn, um_col_surface(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(dl_btn, um_col_focus_green(),
                              (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_border_color(dl_btn, um_col_green(), LV_PART_MAIN);
    lv_obj_set_style_border_width(dl_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(dl_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(dl_btn, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dl_btn, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(dl_btn, [](lv_event_t *) {
        // Build the progress overlay
        lv_obj_t *ov = lv_obj_create(lv_scr_act());
        lv_obj_set_size(ov, lv_pct(100), lv_pct(100));
        lv_obj_set_style_bg_color(ov, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ov, LV_OPA_90, LV_PART_MAIN);
        lv_obj_set_style_border_width(ov, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(ov, 16, LV_PART_MAIN);
        lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(ov, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(ov, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(ov, 12, LV_PART_MAIN);

        // Title
        lv_obj_t *title = lv_label_create(ov);
        lv_label_set_text(title, LV_SYMBOL_DOWNLOAD "  Downloading firmware...");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(title, um_col_cyan_bright(), LV_PART_MAIN);

        // Progress bar
        lv_obj_t *bar = lv_bar_create(ov);
        lv_obj_set_width(bar, lv_pct(100));
        lv_obj_set_height(bar, 14);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, um_col_surface(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, um_col_green_bright(), LV_PART_INDICATOR);
        lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);

        // Status label
        lv_obj_t *status_lbl = lv_label_create(ov);
        lv_label_set_text(status_lbl, "Preparing...");
        lv_obj_set_width(status_lbl, lv_pct(100));
        lv_label_set_long_mode(status_lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
        lv_obj_set_style_text_color(status_lbl, um_col_text_hint(), LV_PART_MAIN);

        // Close / Restart button (hidden until done or error)
        lv_obj_t *close_btn = lv_btn_create(ov);
        lv_obj_set_width(close_btn, 200);
        lv_obj_set_height(close_btn, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(close_btn, um_col_surface(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(close_btn, um_col_focus_cyan(),
            (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
        lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER,
            (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
        lv_obj_set_style_border_color(close_btn, um_col_border(), LV_PART_MAIN);
        lv_obj_set_style_border_width(close_btn, 1, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(close_btn, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(close_btn, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_ver(close_btn, 6, LV_PART_MAIN);
        lv_obj_add_flag(close_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t *close_lbl = lv_label_create(close_btn);
        lv_label_set_text(close_lbl, LV_SYMBOL_POWER "  Restart");
        lv_obj_set_style_text_font(close_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(close_lbl, um_col_text_dim(), LV_PART_MAIN);
        lv_obj_center(close_lbl);
        lv_obj_add_event_cb(close_btn, [](lv_event_t *) {
#ifndef SIM_BUILD
            ESP.restart();
#endif
        }, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(close_btn, [](lv_event_t *e) {
            uint32_t k = lv_event_get_key(e);
#ifndef SIM_BUILD
            if (k == LV_KEY_ENTER) ESP.restart();
#else
            (void)k;
#endif
        }, LV_EVENT_KEY, NULL);

        // Hand widgets to main loop and raise flag
        um_fw_widgets.bar        = bar;
        um_fw_widgets.status_lbl = status_lbl;
        um_fw_widgets.close_btn  = close_btn;

        // Do NOT manipulate the focus group here — dl_btn is still the
        // indev's active object.  Removing it from the group sets
        // dl_btn->group_p = NULL; when lv_timer_handler() later processes
        // the button-release it dereferences that NULL and crashes.
        // Group swap happens in startFwDownload() after the first
        // lv_timer_handler() call has safely cleared the indev state.
        um_fwDownloadRequested = true;
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(dl_btn, [](lv_event_t *e) {
        uint32_t k = lv_event_get_key(e);
        if (k == LV_KEY_ESC || k == LV_KEY_BACKSPACE) um_nav_back();
    }, LV_EVENT_KEY, NULL);

    lv_obj_t *dl_lbl = lv_label_create(dl_btn);
    lv_label_set_text(dl_lbl, LV_SYMBOL_DOWNLOAD "  Download latest from GitHub");
    lv_obj_set_style_text_font(dl_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(dl_lbl, um_col_green_bright(), LV_PART_MAIN);

    lv_obj_t *dl_hint = lv_label_create(info_root);
    lv_label_set_text(dl_hint, "Downloads firmware.bin to SD card (" UM_SD_DIR_OTA ").\n"
                               "After download, use OTA Update to flash it wirelessly.");
    lv_obj_set_width(dl_hint, lv_pct(100));
    lv_label_set_long_mode(dl_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(dl_hint, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_set_style_text_color(dl_hint, um_col_text_hint(), LV_PART_MAIN);

    make_divider(info_root);

    // ---- Back button ----
    lv_obj_t *back_btn = lv_btn_create(info_root);
    lv_obj_set_width(back_btn, 160);
    lv_obj_set_height(back_btn, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(back_btn, um_col_surface(), LV_PART_MAIN);
    lv_obj_set_style_border_color(back_btn, um_col_border(), LV_PART_MAIN);
    lv_obj_set_style_border_width(back_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(back_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(back_btn, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) { um_nav_back(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(back_btn, info_key_cb, LV_EVENT_KEY, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT "  Back");
    lv_obj_set_style_text_color(back_lbl, um_col_text_dim(), LV_PART_MAIN);
    lv_obj_center(back_lbl);

    // ---- Focus group ----
    lv_group_t *g = lv_group_get_default();
    if (g) {
        lv_obj_add_event_cb(ota_btn, info_key_cb, LV_EVENT_KEY, NULL);
        lv_group_add_obj(g, ota_btn);
        lv_group_add_obj(g, dl_btn);
        lv_group_add_obj(g, back_btn);
        lv_group_focus_obj(ota_btn);
    }
}

void um_info_destroy()
{
    if (!info_root) return;
    lv_group_t *g = lv_group_get_default();
    if (g) lv_group_remove_all_objs(g);
    lv_obj_del(info_root);
    info_root = NULL;
}
