#include <Arduino.h>
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <lvgl.h>
#include "um_nav.h"

static lv_obj_t *settings_root = NULL;

static void settings_back_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) um_nav_back();
}

void um_settings_create()
{
    settings_root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(settings_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(settings_root, lv_color_make(4, 6, 10), LV_PART_MAIN);
    lv_obj_set_style_border_width(settings_root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(settings_root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(settings_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(settings_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(settings_root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(settings_root, 12, LV_PART_MAIN);

    lv_obj_t *ico = lv_label_create(settings_root);
    lv_label_set_text(ico, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_40, LV_PART_MAIN);
    lv_obj_set_style_text_color(ico, lv_color_make(200, 160, 0), LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(settings_root);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_make(220, 220, 230), LV_PART_MAIN);

    lv_obj_t *sub = lv_label_create(settings_root);
    lv_label_set_text(sub, "Coming soon");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(sub, lv_color_make(60, 60, 70), LV_PART_MAIN);

    // Back button
    lv_obj_t *back_btn = lv_btn_create(settings_root);
    lv_obj_set_width(back_btn, 160);
    lv_obj_set_style_bg_color(back_btn, lv_color_make(20, 20, 28), LV_PART_MAIN);
    lv_obj_set_style_border_color(back_btn, lv_color_make(60, 60, 80), LV_PART_MAIN);
    lv_obj_set_style_border_width(back_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(back_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(back_btn, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) { um_nav_back(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(back_btn, settings_back_cb, LV_EVENT_KEY, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT "  Back");
    lv_obj_set_style_text_color(back_lbl, lv_color_make(160, 160, 170), LV_PART_MAIN);
    lv_obj_center(back_lbl);

    lv_group_t *g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, back_btn);
        lv_group_focus_obj(back_btn);
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
