#include <Arduino.h>
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <lvgl.h>
#include "um_nav.h"
#include "config.h"
#include "um_theme.h"

static lv_obj_t *help_root = NULL;

static void help_back_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) um_nav_back();
}

void um_help_create()
{
    // ---- Root — non-scrollable column (matches messages/nfc pattern) ----
    help_root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(help_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(help_root, um_col_bg(), LV_PART_MAIN);
    lv_obj_set_style_border_width(help_root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(help_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(help_root, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(help_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(help_root, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(help_root, LV_OBJ_FLAG_SCROLLABLE);

    // ---- Top bar (same as messages/nfc) ----
    lv_obj_t *hdr = lv_obj_create(help_root);
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
    lv_label_set_text(title, LV_SYMBOL_WARNING "  Help & About");
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
    lv_obj_add_event_cb(home_btn, [](lv_event_t *) { um_nav_back(); },
                        LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(home_btn, help_back_cb, LV_EVENT_KEY, NULL);
    lv_obj_t *home_lbl = lv_label_create(home_btn);
    lv_label_set_text(home_lbl, LV_SYMBOL_HOME);
    lv_obj_set_style_text_color(home_lbl, um_col_cyan(), LV_PART_MAIN);
    lv_obj_center(home_lbl);

    // ---- Scrollable content area ----
    lv_obj_t *scroll = lv_obj_create(help_root);
    lv_obj_set_width(scroll, lv_pct(100));
    lv_obj_set_flex_grow(scroll, 1);
    lv_obj_set_style_bg_color(scroll, um_col_bg(), LV_PART_MAIN);
    lv_obj_set_style_border_width(scroll, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(scroll, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scroll, 16, LV_PART_MAIN);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scroll, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(scroll, 8, LV_PART_MAIN);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_add_event_cb(scroll, [](lv_event_t *e) {
        uint32_t k = lv_event_get_key(e);
        if (k == LV_KEY_ESC || k == LV_KEY_BACKSPACE) um_nav_back();
    }, LV_EVENT_KEY, NULL);

    // Divider
    lv_obj_t *div = lv_obj_create(scroll);
    lv_obj_set_size(div, lv_pct(100), 1);
    lv_obj_set_style_bg_color(div, um_col_divider(), LV_PART_MAIN);
    lv_obj_set_style_border_width(div, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(div, 0, LV_PART_MAIN);

    // Help text
    static const char *help_text =
        LV_SYMBOL_RIGHT "  Rotary: turn to navigate, press to select\n"
        LV_SYMBOL_KEYBOARD "  Keyboard: type commands or messages\n"
        LV_SYMBOL_WIFI "  Mesh: scans ch1-13 for coordinator\n"
        LV_SYMBOL_POWER "  Sleep: tap power icon in the main menu\n\n"
        "Mesh Nodename: " NODE_NAME "\n"
        "UniversalMesh Pager " UM_PAGER_FW_VERSION " by PD2EMC (c) 2026\n"
        "UniversalMesh " UM_FW_VERSION "  by PD8JO (c) 2026\n";

    lv_obj_t *body = lv_label_create(scroll);
    lv_label_set_text(body, help_text);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(body, um_col_text_dim(), LV_PART_MAIN);
    lv_obj_set_width(body, lv_pct(100));
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);

    // ---- Focus group ----
    lv_group_t *g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, home_btn);
        lv_group_add_obj(g, scroll);
        lv_group_focus_obj(scroll);
    }
}

void um_help_destroy()
{
    if (!help_root) return;
    lv_group_t *g = lv_group_get_default();
    if (g) lv_group_remove_all_objs(g);
    lv_obj_del(help_root);
    help_root = NULL;
}
