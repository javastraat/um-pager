#include <Arduino.h>
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <lvgl.h>
#include "um_nav.h"
#include "um_theme.h"

static lv_obj_t *messages_root = NULL;

static void messages_back_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) um_nav_back();
}

void um_messages_create()
{
    messages_root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(messages_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(messages_root, um_col_bg(), LV_PART_MAIN);
    lv_obj_set_style_border_width(messages_root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(messages_root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(messages_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(messages_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(messages_root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(messages_root, 12, LV_PART_MAIN);

    lv_obj_t *ico = lv_label_create(messages_root);
    lv_label_set_text(ico, LV_SYMBOL_ENVELOPE);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_40, LV_PART_MAIN);
    lv_obj_set_style_text_color(ico, um_col_green_bright(), LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(messages_root);
    lv_label_set_text(title, "Messages");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, um_col_text(), LV_PART_MAIN);

    lv_obj_t *sub = lv_label_create(messages_root);
    lv_label_set_text(sub, "Coming soon");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(sub, um_col_text_inactive(), LV_PART_MAIN);

    // Back button
    lv_obj_t *back_btn = lv_btn_create(messages_root);
    lv_obj_set_width(back_btn, 160);
    lv_obj_set_style_bg_color(back_btn, um_col_surface(), LV_PART_MAIN);
    lv_obj_set_style_border_color(back_btn, um_col_border(), LV_PART_MAIN);
    lv_obj_set_style_border_width(back_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(back_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(back_btn, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) { um_nav_back(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(back_btn, messages_back_cb, LV_EVENT_KEY, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT "  Back");
    lv_obj_set_style_text_color(back_lbl, um_col_text_dim(), LV_PART_MAIN);
    lv_obj_center(back_lbl);

    lv_group_t *g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, back_btn);
        lv_group_focus_obj(back_btn);
    }
}

void um_messages_destroy()
{
    if (!messages_root) return;
    lv_group_t *g = lv_group_get_default();
    if (g) lv_group_remove_all_objs(g);
    lv_obj_del(messages_root);
    messages_root = NULL;
}
