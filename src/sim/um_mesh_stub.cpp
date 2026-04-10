// Stub mesh screen for the simulator.
// The real um_mesh.cpp pulls in RadioLib, UniversalMesh, WiFi, etc.
// This placeholder lets the nav router compile and shows a placeholder tile.

#include <Arduino.h>
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <lvgl.h>
#include "um_nav.h"
#include "um_shared.h"

volatile bool       um_otaRequested     = false;
volatile uint32_t   um_sleep_timeout_ms = 60000;
volatile uint32_t   um_dim_timeout_ms   = 30000;
volatile uint8_t    um_dim_brightness   = 20;
volatile um_theme_t um_active_theme     = UM_THEME_DARK;

static lv_obj_t *mesh_root = NULL;

static void mesh_back_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) um_nav_back();
}

void um_mesh_create()
{
    mesh_root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(mesh_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(mesh_root, um_col_bg(), LV_PART_MAIN);
    lv_obj_set_style_border_width(mesh_root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(mesh_root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(mesh_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(mesh_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(mesh_root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(mesh_root, 12, LV_PART_MAIN);

    lv_obj_t *ico = lv_label_create(mesh_root);
    lv_label_set_text(ico, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_40, LV_PART_MAIN);
    lv_obj_set_style_text_color(ico, um_col_cyan_bright(), LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(mesh_root);
    lv_label_set_text(title, "ESP-NOW Mesh");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, um_col_text(), LV_PART_MAIN);

    lv_obj_t *sub = lv_label_create(mesh_root);
    lv_label_set_text(sub, "Simulator stub");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(sub, um_col_text_inactive(), LV_PART_MAIN);

    lv_obj_t *back_btn = lv_btn_create(mesh_root);
    lv_obj_set_width(back_btn, 160);
    lv_obj_set_style_bg_color(back_btn, um_col_surface(), LV_PART_MAIN);
    lv_obj_set_style_border_color(back_btn, um_col_border(), LV_PART_MAIN);
    lv_obj_set_style_border_width(back_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(back_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(back_btn, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) { um_nav_back(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(back_btn, mesh_back_cb, LV_EVENT_KEY, NULL);
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

bool um_mesh_has_coordinator()
{
    return false; // no real mesh in the simulator
}

void um_mesh_destroy()
{
    if (!mesh_root) return;
    lv_group_t *g = lv_group_get_default();
    if (g) lv_group_remove_all_objs(g);
    lv_obj_del(mesh_root);
    mesh_root = NULL;
}
