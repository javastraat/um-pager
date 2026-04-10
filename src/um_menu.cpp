#include <Arduino.h>
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <lvgl.h>
#include <time.h>
#include "um_nav.h"
#include "um_shared.h"

// Logo image descriptor defined in um_welcome.cpp
extern const lv_image_dsc_t um_logo_dsc;

// -------------------------------------------------------
// Menu tile definitions
// -------------------------------------------------------
struct MenuTile {
    const char *symbol;       // LVGL built-in symbol glyph
    const char *title;
    const char *subtitle;
    lv_color_t  accent;       // icon + border highlight colour
    UMScreen    target;
};

static const MenuTile TILES[] = {
    {
        LV_SYMBOL_WIFI,"ESP-Now","Mesh Network",
        lv_color_make(0, 200, 255),   UM_SCREEN_MESH
    },
    {
        LV_SYMBOL_GPS,"LoRa","Long-range radio",
        lv_color_make(255, 120, 0),   UM_SCREEN_LORA
    },
    {
        LV_SYMBOL_ENVELOPE,"Messages","Inbox & compose",
        lv_color_make(0, 230, 120),   UM_SCREEN_MESSAGES
    },
    {
        LV_SYMBOL_SETTINGS,"Settings","Device config",
        lv_color_make(200, 160, 0),   UM_SCREEN_SETTINGS
    },
    {
        LV_SYMBOL_WARNING,"Help","About & guide",
        lv_color_make(220, 50, 50),   UM_SCREEN_HELP
    },
    {
        LV_SYMBOL_LIST,"Info","System & OTA",
        lv_color_make(120, 80, 220),  UM_SCREEN_INFO
    },
};
static const int TILE_COUNT = sizeof(TILES) / sizeof(TILES[0]);

// -------------------------------------------------------
// State
// -------------------------------------------------------
static lv_obj_t   *menu_root          = NULL;
static lv_obj_t   *menu_tiles[6]      = {};
static int         menu_focused       = 0;
static lv_obj_t   *menu_time_lbl      = NULL;  // topbar clock
static lv_obj_t   *menu_coord_icon    = NULL;  // topbar coordinator indicator
static lv_timer_t *menu_topbar_timer  = NULL;

// -------------------------------------------------------
// Tile focus / unfocus visuals
// -------------------------------------------------------
static void menu_apply_focus(int idx, bool focused)
{
    lv_obj_t *tile = menu_tiles[idx];
    if (!tile) return;

    lv_color_t accent = TILES[idx].accent;

    if (focused) {
        // Bright border + slightly elevated background
        lv_obj_set_style_border_color(tile, accent, LV_PART_MAIN);
        lv_obj_set_style_border_width(tile, 2, LV_PART_MAIN);
        lv_obj_set_style_bg_color(tile, lv_color_make(18, 28, 38), LV_PART_MAIN);
        lv_obj_set_style_shadow_color(tile, accent, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(tile, 20, LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(tile, LV_OPA_50, LV_PART_MAIN);
    } else {
        lv_obj_set_style_border_color(tile, lv_color_make(40, 40, 50), LV_PART_MAIN);
        lv_obj_set_style_border_width(tile, 1, LV_PART_MAIN);
        lv_obj_set_style_bg_color(tile, lv_color_make(10, 12, 18), LV_PART_MAIN);
        lv_obj_set_style_shadow_width(tile, 0, LV_PART_MAIN);
    }
}

// -------------------------------------------------------
// Input handler — rotary encoder events routed here via
// lv_group so we get LV_EVENT_KEY on the focused tile
// -------------------------------------------------------
static void menu_tile_key_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);

    if (key == LV_KEY_RIGHT || key == LV_KEY_DOWN) {
        menu_apply_focus(menu_focused, false);
        menu_focused = (menu_focused + 1) % TILE_COUNT;
        menu_apply_focus(menu_focused, true);
        lv_group_focus_obj(menu_tiles[menu_focused]);
        lv_obj_scroll_to_view(menu_tiles[menu_focused], LV_ANIM_ON);
        lv_event_stop_processing(e); // prevent LVGL group from also advancing focus
    } else if (key == LV_KEY_LEFT || key == LV_KEY_UP) {
        menu_apply_focus(menu_focused, false);
        menu_focused = (menu_focused - 1 + TILE_COUNT) % TILE_COUNT;
        menu_apply_focus(menu_focused, true);
        lv_group_focus_obj(menu_tiles[menu_focused]);
        lv_obj_scroll_to_view(menu_tiles[menu_focused], LV_ANIM_ON);
        lv_event_stop_processing(e); // prevent LVGL group from also advancing focus
    } else if (key == LV_KEY_ENTER) {
        um_nav_go(TILES[menu_focused].target);
        lv_event_stop_processing(e);
    }
}

static void menu_tile_click_cb(lv_event_t *e)
{
    lv_obj_t *tile = (lv_obj_t *)lv_event_get_target(e);
    for (int i = 0; i < TILE_COUNT; i++) {
        if (menu_tiles[i] == tile) {
            um_nav_go(TILES[i].target);
            return;
        }
    }
}

static void menu_tile_focused_cb(lv_event_t *e)
{
    lv_obj_t *tile = (lv_obj_t *)lv_event_get_target(e);
    for (int i = 0; i < TILE_COUNT; i++) {
        menu_apply_focus(i, menu_tiles[i] == tile);
        if (menu_tiles[i] == tile) menu_focused = i;
    }
    lv_obj_scroll_to_view(tile, LV_ANIM_ON);
}

// Called when the power button in the top bar receives focus —
// clears the tile highlight so the UI doesn't show two things selected.
static void menu_pwr_focused_cb(lv_event_t *e)
{
    for (int i = 0; i < TILE_COUNT; i++)
        menu_apply_focus(i, false);
}

// -------------------------------------------------------
// Topbar update — called by timer every 30 s
// -------------------------------------------------------
static void menu_topbar_update_cb(lv_timer_t *)
{
    // ---- Time ----
    if (menu_time_lbl) {
        struct tm t = {};
#ifndef SIM_BUILD
        // Time will be synced into the RTC via an incoming mesh message
        instance.rtc.getDateTime(&t);
#else
        time_t now = time(NULL);
        t = *localtime(&now);
#endif
        lv_label_set_text_fmt(menu_time_lbl, "%02d:%02d", t.tm_hour, t.tm_min);
    }

    // ---- Coordinator indicator ----
    if (menu_coord_icon) {
        bool connected = um_mesh_has_coordinator();
        lv_obj_set_style_text_color(menu_coord_icon,
            connected ? lv_color_make(0, 160, 255) : lv_color_make(70, 70, 80),
            LV_PART_MAIN);
    }
}

// -------------------------------------------------------
// Build menu
// -------------------------------------------------------
void um_menu_create()
{
    menu_root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(menu_root, lv_pct(100), lv_pct(100));
    lv_obj_center(menu_root);
    lv_obj_set_style_bg_color(menu_root, lv_color_make(4, 6, 10), LV_PART_MAIN);
    lv_obj_set_style_border_width(menu_root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(menu_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(menu_root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(menu_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(menu_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(menu_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // ---- Top bar ----
    lv_obj_t *topbar = lv_obj_create(menu_root);
    lv_obj_set_width(topbar, lv_pct(100));
    lv_obj_set_height(topbar, 28);
    lv_obj_set_style_bg_color(topbar, lv_color_make(4, 6, 10), LV_PART_MAIN);
    lv_obj_set_style_border_width(topbar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(topbar, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(topbar, 4, LV_PART_MAIN);
    lv_obj_clear_flag(topbar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(topbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topbar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Left: app name
    lv_obj_t *app_lbl = lv_label_create(topbar);
    lv_label_set_text(app_lbl, LV_SYMBOL_WIFI "  UniversalMesh");
    lv_obj_set_style_text_color(app_lbl, lv_color_make(0, 180, 220), LV_PART_MAIN);
    lv_obj_set_style_text_font(app_lbl, &lv_font_montserrat_14, LV_PART_MAIN);

    // Center: clock — updated by timer
    menu_time_lbl = lv_label_create(topbar);
    lv_label_set_text(menu_time_lbl, "--:--");
    lv_obj_set_style_text_color(menu_time_lbl, lv_color_make(200, 200, 210), LV_PART_MAIN);
    lv_obj_set_style_text_font(menu_time_lbl, &lv_font_montserrat_14, LV_PART_MAIN);

    // Right container: coordinator icon + power button
    lv_obj_t *right_box = lv_obj_create(topbar);
    lv_obj_set_size(right_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_box, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(right_box, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(right_box, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(right_box, 6, LV_PART_MAIN);
    lv_obj_clear_flag(right_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(right_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_box, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Hostname
    lv_obj_t *node_lbl = lv_label_create(right_box);
    lv_label_set_text(node_lbl, NODE_NAME);
    lv_obj_set_style_text_color(node_lbl, lv_color_make(130, 130, 145), LV_PART_MAIN);
    lv_obj_set_style_text_font(node_lbl, &lv_font_montserrat_12, LV_PART_MAIN);

    // Coordinator indicator: wifi symbol, gray = no coordinator, blue = connected
    menu_coord_icon = lv_label_create(right_box);
    lv_label_set_text(menu_coord_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(menu_coord_icon, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(menu_coord_icon, lv_color_make(70, 70, 80), LV_PART_MAIN);

    // Power / sleep button
    lv_obj_t *pwr_btn = lv_btn_create(right_box);
    lv_obj_set_size(pwr_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(pwr_btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(pwr_btn, lv_color_make(40, 40, 50), LV_PART_MAIN);
    lv_obj_set_style_border_width(pwr_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(pwr_btn, 6, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(pwr_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(pwr_btn, 4, LV_PART_MAIN);
    // Focused state: bright red background + border + glow
    lv_style_selector_t focused = (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN);
    lv_obj_set_style_bg_color(pwr_btn, lv_color_make(160, 20, 20), focused);
    lv_obj_set_style_bg_opa(pwr_btn, LV_OPA_COVER, focused);
    lv_obj_set_style_border_color(pwr_btn, lv_color_make(255, 80, 80), focused);
    lv_obj_set_style_border_width(pwr_btn, 2, focused);
    lv_obj_set_style_shadow_color(pwr_btn, lv_color_make(220, 40, 40), focused);
    lv_obj_set_style_shadow_width(pwr_btn, 16, focused);
    lv_obj_set_style_shadow_opa(pwr_btn, LV_OPA_70, focused);
    lv_obj_add_event_cb(pwr_btn, [](lv_event_t *e) {
        instance.sleep((WakeupSource_t)(WAKEUP_SRC_BOOT_BUTTON | WAKEUP_SRC_ROTARY_BUTTON));
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *pwr_lbl = lv_label_create(pwr_btn);
    lv_label_set_text(pwr_lbl, LV_SYMBOL_POWER);
    lv_obj_set_style_text_color(pwr_lbl, lv_color_make(200, 60, 60), LV_PART_MAIN);
    lv_obj_center(pwr_lbl);
    lv_obj_add_event_cb(pwr_btn, menu_pwr_focused_cb, LV_EVENT_FOCUSED, NULL);
    // Brighten the icon when focused, restore when focus leaves
    lv_obj_add_event_cb(pwr_btn, [](lv_event_t *e) {
        lv_obj_t *lbl = (lv_obj_t *)lv_event_get_user_data(e);
        lv_obj_set_style_text_color(lbl, lv_color_make(255, 220, 220), LV_PART_MAIN);
    }, LV_EVENT_FOCUSED, pwr_lbl);
    lv_obj_add_event_cb(pwr_btn, [](lv_event_t *e) {
        lv_obj_t *lbl = (lv_obj_t *)lv_event_get_user_data(e);
        lv_obj_set_style_text_color(lbl, lv_color_make(200, 60, 60), LV_PART_MAIN);
    }, LV_EVENT_DEFOCUSED, pwr_lbl);

    // ---- Thin accent line under topbar ----
    lv_obj_t *accent_line = lv_obj_create(menu_root);
    lv_obj_set_size(accent_line, lv_pct(100), 1);
    lv_obj_set_style_bg_color(accent_line, lv_color_make(0, 60, 90), LV_PART_MAIN);
    lv_obj_set_style_border_width(accent_line, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(accent_line, 0, LV_PART_MAIN);

    // ---- Tile row ----
    lv_obj_t *tile_row = lv_obj_create(menu_root);
    lv_obj_set_width(tile_row, lv_pct(100));
    lv_obj_set_flex_grow(tile_row, 1);
    lv_obj_set_style_bg_color(tile_row, lv_color_make(4, 6, 10), LV_PART_MAIN);
    lv_obj_set_style_border_width(tile_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tile_row, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_column(tile_row, 10, LV_PART_MAIN);
    lv_obj_set_scroll_dir(tile_row, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(tile_row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_snap_x(tile_row, LV_SCROLL_SNAP_NONE);
    lv_obj_set_flex_flow(tile_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tile_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_group_t *g = lv_group_get_default();
    if (g) lv_group_add_obj(g, pwr_btn);

    // Fixed tile width so exactly 4 tiles are visible at once.
    // tile_row content width = screen_w - 2*pad(10) = screen_w - 20
    // 4 tiles + 3 gaps of 10px each = 3*10 = 30 consumed by gaps
    lv_coord_t screen_w = lv_display_get_horizontal_resolution(lv_display_get_default());
    lv_coord_t tile_w   = (screen_w - 20 - 30) / 4;

    for (int i = 0; i < TILE_COUNT; i++) {
        // Card container
        lv_obj_t *tile = lv_obj_create(tile_row);
        menu_tiles[i] = tile;
        lv_obj_set_width(tile, tile_w);
        lv_obj_set_height(tile, lv_pct(100));
        lv_obj_set_style_bg_color(tile, lv_color_make(10, 12, 18), LV_PART_MAIN);
        lv_obj_set_style_radius(tile, 10, LV_PART_MAIN);
        lv_obj_set_style_border_color(tile, lv_color_make(40, 40, 50), LV_PART_MAIN);
        lv_obj_set_style_border_width(tile, 1, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(tile, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(tile, 8, LV_PART_MAIN);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(tile, 6, LV_PART_MAIN);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);

        // Big symbol / icon — tile 0 uses the logo PNG, others use a symbol glyph
        if (TILES[i].symbol == NULL) {
            lv_obj_t *ico = lv_image_create(tile);
            lv_image_set_src(ico, &um_logo_dsc);
            // Scale 512px source down to ~40px (20/256 * 512 = 40)
            lv_image_set_scale(ico, 20);
            lv_obj_set_size(ico, 40, 40);
            // Additive blend: black pixels become transparent on the dark background
            lv_obj_set_style_blend_mode(ico, LV_BLEND_MODE_ADDITIVE, LV_PART_MAIN);
        } else {
            lv_obj_t *ico = lv_label_create(tile);
            lv_label_set_text(ico, TILES[i].symbol);
            lv_obj_set_style_text_font(ico, &lv_font_montserrat_40, LV_PART_MAIN);
            lv_obj_set_style_text_color(ico, TILES[i].accent, LV_PART_MAIN);
        }

        // Title
        lv_obj_t *title_lbl = lv_label_create(tile);
        lv_label_set_text(title_lbl, TILES[i].title);
        lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_obj_set_style_text_color(title_lbl, lv_color_make(220, 220, 230), LV_PART_MAIN);

        // Subtitle
        lv_obj_t *sub_lbl = lv_label_create(tile);
        lv_label_set_text(sub_lbl, TILES[i].subtitle);
        lv_obj_set_style_text_font(sub_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
        lv_obj_set_style_text_color(sub_lbl, lv_color_make(150, 150, 165), LV_PART_MAIN);
        lv_label_set_long_mode(sub_lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(sub_lbl, lv_pct(100));
        lv_obj_set_style_text_align(sub_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

        // Events
        lv_obj_add_event_cb(tile, menu_tile_click_cb,   LV_EVENT_CLICKED,  NULL);
        lv_obj_add_event_cb(tile, menu_tile_key_cb,     LV_EVENT_KEY,      NULL);
        lv_obj_add_event_cb(tile, menu_tile_focused_cb, LV_EVENT_FOCUSED,  NULL);

        if (g) lv_group_add_obj(g, tile);
    }

    // Focus first tile
    menu_focused = 0;
    menu_apply_focus(0, true);
    if (g && menu_tiles[0]) lv_group_focus_obj(menu_tiles[0]);

    // ---- Bottom hint bar ----
    lv_obj_t *hint_line = lv_obj_create(menu_root);
    lv_obj_set_size(hint_line, lv_pct(100), 1);
    lv_obj_set_style_bg_color(hint_line, lv_color_make(0, 60, 90), LV_PART_MAIN);
    lv_obj_set_style_border_width(hint_line, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(hint_line, 0, LV_PART_MAIN);

    lv_obj_t *hint = lv_obj_create(menu_root);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_height(hint, 18);
    lv_obj_set_style_bg_color(hint, lv_color_make(4, 6, 10), LV_PART_MAIN);
    lv_obj_set_style_border_width(hint, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(hint, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(hint, 2, LV_PART_MAIN);
    lv_obj_clear_flag(hint, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(hint, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hint, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *hint_lbl = lv_label_create(hint);
    lv_label_set_text(hint_lbl, LV_SYMBOL_LEFT " / " LV_SYMBOL_RIGHT " navigate      " LV_SYMBOL_OK " select");
    lv_obj_set_style_text_color(hint_lbl, lv_color_make(130, 130, 145), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint_lbl, &lv_font_montserrat_10, LV_PART_MAIN);

    // ---- Topbar timer: update clock + coordinator icon every 30 s ----
    menu_topbar_timer = lv_timer_create(menu_topbar_update_cb, 30000, NULL);
    lv_timer_ready(menu_topbar_timer); // fire immediately to show current time
}

void um_menu_destroy()
{
    if (!menu_root) return;
    if (menu_topbar_timer) { lv_timer_del(menu_topbar_timer); menu_topbar_timer = NULL; }
    menu_time_lbl   = NULL;
    menu_coord_icon = NULL;
    lv_group_t *g = lv_group_get_default();
    if (g) lv_group_remove_all_objs(g);
    lv_obj_del(menu_root);
    menu_root = NULL;
    for (int i = 0; i < TILE_COUNT; i++) menu_tiles[i] = NULL;
}
