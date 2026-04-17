#include "um_toast.h"
#include <string.h>
#include <lvgl.h>
#include "../um_theme.h"
#include "../config.h"
#include "../um_shared.h"

#ifndef SIM_BUILD
#include <Arduino.h>
#endif

// How long (ms) the toast stays visible
#define UM_TOAST_DURATION_MS  4000
// Max chars for icon+text combined
#define UM_TOAST_TEXT_LEN     80

// -------------------------------------------------------
// State (LVGL-task-only after async dispatch)
// -------------------------------------------------------
static lv_obj_t  *toast_obj   = NULL;
static lv_timer_t *toast_timer = NULL;

// Pending payload queued from mesh task
static char       toast_pending_icon[8]            = {};
static char       toast_pending_text[UM_TOAST_TEXT_LEN] = {};
static volatile bool toast_pending = false;

// -------------------------------------------------------
// Dismiss timer callback
// -------------------------------------------------------
static void toast_dismiss_cb(lv_timer_t *t)
{
    (void)t;
    if (toast_obj) {
        lv_obj_del(toast_obj);
        toast_obj = NULL;
    }
    if (toast_timer) {
        lv_timer_del(toast_timer);
        toast_timer = NULL;
    }
}

// -------------------------------------------------------
// LVGL-task: build the toast widget
// -------------------------------------------------------
static void toast_create_cb(void * /*user_data*/)
{
    // Dismiss any existing toast
    toast_dismiss_cb(NULL);

    // Banner container — pinned to bottom of screen
    toast_obj = lv_obj_create(lv_layer_top());
    lv_obj_set_width(toast_obj, lv_pct(90));
    lv_obj_set_height(toast_obj, LV_SIZE_CONTENT);
    lv_obj_align(toast_obj, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_bg_color(toast_obj, um_col_surface(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(toast_obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(toast_obj, um_col_green(), LV_PART_MAIN);
    lv_obj_set_style_border_width(toast_obj, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(toast_obj, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(toast_obj, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(toast_obj, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(toast_obj, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(toast_obj, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(toast_obj, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_column(toast_obj, 8, LV_PART_MAIN);
    lv_obj_clear_flag(toast_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(toast_obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toast_obj, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Icon
    lv_obj_t *ico = lv_label_create(toast_obj);
    lv_label_set_text(ico, toast_pending_icon);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(ico, um_col_green_bright(), LV_PART_MAIN);

    // Text
    lv_obj_t *lbl = lv_label_create(toast_obj);
    lv_label_set_text(lbl, toast_pending_text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, um_col_text(), LV_PART_MAIN);
    lv_obj_set_flex_grow(lbl, 1);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_SCROLL);

    toast_pending = false;

    // Auto-dismiss timer
    toast_timer = lv_timer_create(toast_dismiss_cb, UM_TOAST_DURATION_MS, NULL);
    lv_timer_set_repeat_count(toast_timer, 1);
}

// -------------------------------------------------------
// Public API — safe to call from any task
// -------------------------------------------------------
void um_toast_show(const char *icon, const char *text)
{
    strncpy(toast_pending_icon, icon ? icon : UM_SYMBOL_ENVELOPE,
            sizeof(toast_pending_icon) - 1);
    strncpy(toast_pending_text, text ? text : "",
            sizeof(toast_pending_text) - 1);
    toast_pending = true;
    lv_async_call(toast_create_cb, NULL);
}
