#include <Arduino.h>
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <lvgl.h>
#include "um_nav.h"
#include "um_logo.h"

// -------------------------------------------------------
// Boot/welcome splash — shown for 3 s then goes to menu
// -------------------------------------------------------
static lv_obj_t  *welcome_root  = NULL;
static lv_timer_t *welcome_timer = NULL;

// LVGL image descriptor pointing at the in-flash PNG bytes
static const lv_image_dsc_t um_logo_dsc = {
    .header = {
        .magic     = LV_IMAGE_HEADER_MAGIC,
        .cf        = LV_COLOR_FORMAT_RAW,
        .flags     = 0,
        .w         = 512,
        .h         = 512,
        .stride    = 0,
    },
    .data_size = UM_LOGO_PNG_LEN,
    .data      = UM_LOGO_PNG,
};

static void welcome_done_cb(lv_timer_t *t)
{
    welcome_timer = NULL;
    um_nav_go(UM_SCREEN_MENU);
}

void um_welcome_create()
{
    welcome_root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(welcome_root, lv_pct(100), lv_pct(100));
    lv_obj_center(welcome_root);
    lv_obj_set_style_bg_color(welcome_root, lv_color_make(4, 6, 10), LV_PART_MAIN);
    lv_obj_set_style_border_width(welcome_root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(welcome_root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(welcome_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(welcome_root, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(welcome_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(welcome_root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(welcome_root, 6, LV_PART_MAIN);

    // PWA icon — scale 40/256 ≈ 80px from the 512px source.
    // Size must be set explicitly so the flex layout doesn't see 512×512.
    lv_obj_t *img = lv_image_create(welcome_root);
    lv_image_set_src(img, &um_logo_dsc);
    lv_image_set_scale(img, 40);
    lv_obj_set_size(img, 80, 80);
    // Additive blend: black (0,0,0) pixels add nothing to the background,
    // making the logo's black fill transparent on any dark background.
    lv_obj_set_style_blend_mode(img, LV_BLEND_MODE_ADDITIVE, LV_PART_MAIN);

    // Title
    lv_obj_t *title = lv_label_create(welcome_root);
    lv_label_set_text(title, "UniversalMesh");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_make(0, 210, 255), LV_PART_MAIN);

    // Accent bar
    lv_obj_t *bar = lv_obj_create(welcome_root);
    lv_obj_set_size(bar, 180, 2);
    lv_obj_set_style_bg_color(bar, lv_color_make(0, 80, 120), LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 1, LV_PART_MAIN);

    // Subtitle
    lv_obj_t *sub = lv_label_create(welcome_root);
    lv_label_set_text(sub, "Mesh Networking with ESP");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(sub, lv_color_make(140, 140, 155), LV_PART_MAIN);

    // Node name
    lv_obj_t *node = lv_label_create(welcome_root);
    lv_label_set_text(node, NODE_NAME);
    lv_obj_set_style_text_font(node, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(node, lv_color_make(100, 100, 115), LV_PART_MAIN);

    welcome_timer = lv_timer_create(welcome_done_cb, 3000, NULL);
    lv_timer_set_repeat_count(welcome_timer, 1);
}

void um_welcome_destroy()
{
    if (welcome_timer) { lv_timer_del(welcome_timer); welcome_timer = NULL; }
    if (welcome_root)  { lv_obj_del(welcome_root);    welcome_root  = NULL; }
}
