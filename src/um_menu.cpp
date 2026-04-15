#include <Arduino.h>
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <lvgl.h>
#include <time.h>
#include "um_nav.h"
#include "um_shared.h"
#include "config.h"
#include "helpers/um_haptic.h"

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
        LV_SYMBOL_WIFI,"UniMesh",       "ESPNow Mesh",
        lv_color_make(0, 200, 255),   UM_SCREEN_MESH
    },
    {
        LV_SYMBOL_WIFI,"LoRa",          "LoRa Radio",
        lv_color_make(255, 120, 0),   UM_SCREEN_LORA
    },
    {
        LV_SYMBOL_ENVELOPE,"Mailbox",   "Inbox & Send",
        lv_color_make(0, 230, 120),   UM_SCREEN_MESSAGES
    },
    {
        LV_SYMBOL_LOOP,"NFC",           "NFC Reader",
        lv_color_make(0, 200, 160),   UM_SCREEN_NFC
    },
    {
        LV_SYMBOL_SD_CARD,"Storage",    "SD Card files",
        lv_color_make(80, 160, 100),  UM_SCREEN_SD
    },
    {
        LV_SYMBOL_SETTINGS,"Settings",  "Device Config",
        lv_color_make(200, 160, 0),   UM_SCREEN_SETTINGS
    },
    {
        LV_SYMBOL_LIST,"Info",          "System & OTA",
        lv_color_make(120, 80, 220),  UM_SCREEN_INFO
    },
    {
        LV_SYMBOL_WARNING,"Help",       "About & Help",
        lv_color_make(220, 50, 50),   UM_SCREEN_HELP
    },
};
static const int TILE_COUNT = sizeof(TILES) / sizeof(TILES[0]);

// -------------------------------------------------------
// State
// -------------------------------------------------------
static lv_obj_t   *menu_root          = NULL;
static lv_obj_t   *menu_tiles[8]      = {};
static int         menu_focused       = 0;
static lv_obj_t   *menu_time_lbl      = NULL;  // topbar clock
static lv_obj_t   *menu_app_lbl       = NULL;  // topbar left label
static lv_obj_t   *menu_coord_icon    = NULL;  // topbar coordinator indicator
static lv_obj_t   *menu_lora_icon     = NULL;  // topbar LoRa background indicator
static lv_timer_t *menu_topbar_timer  = NULL;
static lv_obj_t   *menu_msg_badge_lbl = NULL;  // Messages tile unread badge
static lv_obj_t   *menu_bat_lbl       = NULL;  // Topbar battery indicator

static void menu_reset_indev_state()
{
    lv_indev_t *indev = NULL;
    while ((indev = lv_indev_get_next(indev)) != NULL) {
        lv_indev_reset(indev, NULL);
    }
}

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
        lv_obj_set_style_bg_color(tile, um_col_surface_tile_focus(), LV_PART_MAIN);
        lv_obj_set_style_shadow_color(tile, accent, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(tile, 20, LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(tile, LV_OPA_50, LV_PART_MAIN);
    } else {
        lv_obj_set_style_border_color(tile, um_col_border(), LV_PART_MAIN);
        lv_obj_set_style_border_width(tile, 1, LV_PART_MAIN);
        lv_obj_set_style_bg_color(tile, um_col_surface_tile(), LV_PART_MAIN);
        lv_obj_set_style_shadow_width(tile, 0, LV_PART_MAIN);
    }
}

static void menu_focus_tile(int idx)
{
    if (idx < 0 || idx >= TILE_COUNT || !menu_tiles[idx]) return;
    menu_apply_focus(menu_focused, false);
    menu_focused = idx;
    menu_apply_focus(menu_focused, true);
    lv_group_focus_obj(menu_tiles[menu_focused]);
    lv_obj_scroll_to_view(menu_tiles[menu_focused], LV_ANIM_ON);
}

static void menu_focus_next_tile()
{
    if (menu_focused < TILE_COUNT - 1)
        menu_focus_tile(menu_focused + 1);
}

static void menu_focus_prev_tile()
{
    if (menu_focused > 0)
        menu_focus_tile(menu_focused - 1);
}

// -------------------------------------------------------
// Input handler — rotary encoder events routed here via
// lv_group so we get LV_EVENT_KEY on the focused tile
// -------------------------------------------------------
static void menu_tile_key_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);

    if (key == LV_KEY_RIGHT || key == LV_KEY_DOWN || key == LV_KEY_NEXT) {
        menu_focus_next_tile();
        lv_event_stop_processing(e); // prevent LVGL group from also advancing focus
    } else if (key == LV_KEY_LEFT || key == LV_KEY_UP || key == LV_KEY_PREV) {
        menu_focus_prev_tile();
        lv_event_stop_processing(e); // prevent LVGL group from also advancing focus
    } else if (key == LV_KEY_ENTER) {
        um_haptic_select();
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
    int found = -1;
    for (int i = 0; i < TILE_COUNT; i++) {
        menu_apply_focus(i, menu_tiles[i] == tile);
        if (menu_tiles[i] == tile) found = i;
    }
    if (found >= 0) {
        menu_focused = found;
        lv_obj_scroll_to_view(tile, LV_ANIM_ON);
    } else {
        // Focus landed on a non-tile object (shouldn't happen); reset to first tile
        menu_focus_tile(0);
    }
}

// Called when the power button in the top bar receives focus —
// clears the tile highlight so the UI doesn't show two things selected.
static void menu_pwr_focused_cb(lv_event_t *e)
{
    for (int i = 0; i < TILE_COUNT; i++)
        menu_apply_focus(i, false);
}

static void menu_pwr_key_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_RIGHT || key == LV_KEY_DOWN || key == LV_KEY_NEXT) {
        menu_focus_next_tile();
        lv_event_stop_processing(e);
    } else if (key == LV_KEY_LEFT || key == LV_KEY_UP || key == LV_KEY_PREV) {
        menu_focus_prev_tile();
        lv_event_stop_processing(e);
    } else if (key == LV_KEY_ENTER) {
        menu_focus_tile(menu_focused);
        lv_event_stop_processing(e);
    }
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
        instance.rtc.getDateTime(&t);
#else
        time_t now = time(NULL);
        t = *localtime(&now);
#endif
        lv_label_set_text_fmt(menu_time_lbl, "%02d:%02d", t.tm_hour, t.tm_min);
        // Red = time is from RTC only (no network sync yet)
        // Green = time has been synced from the network
        lv_obj_set_style_text_color(menu_time_lbl,
            um_time_synced ? lv_color_make(0, 200, 80) : lv_color_make(200, 50, 50),
            LV_PART_MAIN);
    }

    // ---- Coordinator indicator ----
    if (menu_coord_icon) {
        bool connected = um_mesh_has_coordinator();
        lv_obj_set_style_text_color(menu_coord_icon,
            connected ? um_col_cyan_bright() : um_col_text_inactive(),
            LV_PART_MAIN);
    }

    if (menu_lora_icon) {
        bool running = um_lora_background_active();
        lv_obj_set_style_text_color(menu_lora_icon,
            running ? um_col_orange() : um_col_text_inactive(),
            LV_PART_MAIN);
    }

    // ---- App / server label ----
    if (menu_app_lbl) {
        if (um_msg_server_name[0] != '\0') {
            lv_label_set_text_fmt(menu_app_lbl, LV_SYMBOL_ENVELOPE "  %s", um_msg_server_name);
            lv_obj_set_style_text_color(menu_app_lbl, lv_color_make(0, 200, 80), LV_PART_MAIN);
        } else {
            lv_label_set_text(menu_app_lbl, LV_SYMBOL_WIFI "  UniversalMesh Pager");
            lv_obj_set_style_text_color(menu_app_lbl, um_col_cyan(), LV_PART_MAIN);
        }
    }

    // ---- Battery indicator ----
#ifndef SIM_BUILD
    if (menu_bat_lbl) {
        instance.gauge.refresh();
        uint16_t pct  = instance.gauge.getStateOfCharge();
        bool charging = instance.ppm.isCharging();
        bool done     = instance.ppm.isChargeDone();
        const char *icon = (pct > 75) ? LV_SYMBOL_BATTERY_FULL :
                           (pct > 50) ? LV_SYMBOL_BATTERY_3    :
                           (pct > 25) ? LV_SYMBOL_BATTERY_2    :
                           (pct > 10) ? LV_SYMBOL_BATTERY_1    : LV_SYMBOL_BATTERY_EMPTY;
        if (charging)
            lv_label_set_text_fmt(menu_bat_lbl, LV_SYMBOL_CHARGE " %d%%", (int)pct);
        else
            lv_label_set_text_fmt(menu_bat_lbl, "%s %d%%", icon, (int)pct);
        lv_obj_set_style_text_color(menu_bat_lbl,
            charging        ? lv_color_make(0, 220, 80)  :
            done            ? lv_color_make(0, 200, 80)  :
            (pct < 15)      ? lv_color_make(220, 60, 60) : um_col_text_hint(),
            LV_PART_MAIN);
    }
#endif

    // ---- Messages tile unread badge ----
    if (menu_msg_badge_lbl) {
        if (um_unread_count > 0) {
            lv_label_set_text_fmt(menu_msg_badge_lbl,
                                  LV_SYMBOL_BELL "  %lu new",
                                  (unsigned long)um_unread_count);
            lv_obj_set_style_text_color(menu_msg_badge_lbl,
                                        lv_color_make(255, 200, 0), LV_PART_MAIN);
        } else {
            lv_label_set_text(menu_msg_badge_lbl, "Send & Received");
            lv_obj_set_style_text_color(menu_msg_badge_lbl, um_col_text_dim(), LV_PART_MAIN);
        }
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
    lv_obj_set_style_bg_color(menu_root, um_col_bg(), LV_PART_MAIN);
    lv_obj_set_style_border_width(menu_root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(menu_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(menu_root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(menu_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(menu_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(menu_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // Defensive: group focus callback to clamp focus to valid tile
    lv_group_t *g = lv_group_get_default();
    if (g) {
        lv_group_set_focus_cb(g, [](lv_group_t *grp) {
            lv_obj_t *focused = lv_group_get_focused(grp);
            bool valid = false;
            for (int i = 0; i < TILE_COUNT; ++i) {
                if (menu_tiles[i] == focused) {
                    valid = true;
                    break;
                }
            }
            if (!valid && menu_tiles[menu_focused]) {
                lv_group_focus_obj(menu_tiles[menu_focused]);
            }
        });
    }
    // ---- Top bar ----
    lv_obj_t *topbar = lv_obj_create(menu_root);
    lv_obj_set_width(topbar, lv_pct(100));
    lv_obj_set_height(topbar, 28);
    lv_obj_set_style_bg_color(topbar, um_col_bg(), LV_PART_MAIN);
    lv_obj_set_style_border_width(topbar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(topbar, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(topbar, 4, LV_PART_MAIN);
    lv_obj_clear_flag(topbar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(topbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topbar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Left: app name — updated by topbar timer once server ident is received
    menu_app_lbl = lv_label_create(topbar);
    lv_label_set_text(menu_app_lbl, LV_SYMBOL_WIFI "  UniversalMesh");
    lv_obj_set_style_text_color(menu_app_lbl, um_col_cyan(), LV_PART_MAIN);
    lv_obj_set_style_text_font(menu_app_lbl, &lv_font_montserrat_14, LV_PART_MAIN);

    // Center: clock — updated by timer
    menu_time_lbl = lv_label_create(topbar);
    lv_label_set_text(menu_time_lbl, "--:--");
    lv_obj_set_style_text_color(menu_time_lbl, um_col_text_sub(), LV_PART_MAIN);
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
    lv_obj_set_style_text_color(node_lbl, um_col_text_hint(), LV_PART_MAIN);
    lv_obj_set_style_text_font(node_lbl, &lv_font_montserrat_12, LV_PART_MAIN);

    // Coordinator indicator: wifi symbol, gray = no coordinator, blue = connected
    menu_coord_icon = lv_label_create(right_box);
    lv_label_set_text(menu_coord_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(menu_coord_icon, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(menu_coord_icon, um_col_text_inactive(), LV_PART_MAIN);

    // LoRa background indicator: orange when LoRa keeps listening off-screen
    menu_lora_icon = lv_label_create(right_box);
    lv_label_set_text(menu_lora_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(menu_lora_icon, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(menu_lora_icon,
                                um_lora_background_active() ? um_col_orange() : um_col_text_inactive(),
                                LV_PART_MAIN);

    // Battery indicator
    menu_bat_lbl = lv_label_create(right_box);
    lv_label_set_text(menu_bat_lbl, LV_SYMBOL_BATTERY_FULL " --%");
    lv_obj_set_style_text_font(menu_bat_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(menu_bat_lbl, um_col_text_hint(), LV_PART_MAIN);

    // Power / sleep button
    lv_obj_t *pwr_btn = lv_btn_create(right_box);
    lv_obj_set_size(pwr_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(pwr_btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(pwr_btn, um_col_border(), LV_PART_MAIN);
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
    lv_obj_add_event_cb(pwr_btn, menu_pwr_key_cb, LV_EVENT_KEY, NULL);
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
    lv_obj_set_style_bg_color(accent_line, um_col_accent_line(), LV_PART_MAIN);
    lv_obj_set_style_border_width(accent_line, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(accent_line, 0, LV_PART_MAIN);

    // ---- Tile row ----
    lv_obj_t *tile_row = lv_obj_create(menu_root);
    lv_obj_set_width(tile_row, lv_pct(100));
    lv_obj_set_flex_grow(tile_row, 1);
    lv_obj_set_style_bg_color(tile_row, um_col_bg(), LV_PART_MAIN);
    lv_obj_set_style_border_width(tile_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tile_row, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_column(tile_row, 10, LV_PART_MAIN);
    lv_obj_set_scroll_dir(tile_row, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(tile_row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_snap_x(tile_row, LV_SCROLL_SNAP_NONE);
    lv_obj_set_flex_flow(tile_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tile_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

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
        lv_obj_set_style_bg_color(tile, um_col_surface_tile(), LV_PART_MAIN);
        lv_obj_set_style_radius(tile, 10, LV_PART_MAIN);
        lv_obj_set_style_border_color(tile, um_col_border(), LV_PART_MAIN);
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
        lv_obj_set_style_text_color(title_lbl, um_col_text(), LV_PART_MAIN);

        // Subtitle
        lv_obj_t *sub_lbl = lv_label_create(tile);
        lv_label_set_text(sub_lbl, TILES[i].subtitle);
        lv_obj_set_style_text_font(sub_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
        lv_obj_set_style_text_color(sub_lbl, um_col_text_dim(), LV_PART_MAIN);
        lv_label_set_long_mode(sub_lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(sub_lbl, lv_pct(100));
        lv_obj_set_style_text_align(sub_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        // Store pointer for Messages tile so topbar timer can update the badge
        if (TILES[i].target == UM_SCREEN_MESSAGES)
            menu_msg_badge_lbl = sub_lbl;

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
    lv_obj_set_style_bg_color(hint_line, um_col_accent_line(), LV_PART_MAIN);
    lv_obj_set_style_border_width(hint_line, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(hint_line, 0, LV_PART_MAIN);

    lv_obj_t *hint = lv_obj_create(menu_root);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_height(hint, 18);
    lv_obj_set_style_bg_color(hint, um_col_bg(), LV_PART_MAIN);
    lv_obj_set_style_border_width(hint, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(hint, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(hint, 2, LV_PART_MAIN);
    lv_obj_clear_flag(hint, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(hint, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hint, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *hint_lbl = lv_label_create(hint);
    lv_label_set_text(hint_lbl, LV_SYMBOL_LEFT " / " LV_SYMBOL_RIGHT " navigate      " LV_SYMBOL_OK " select");
    lv_obj_set_style_text_color(hint_lbl, um_col_text_hint(), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint_lbl, &lv_font_montserrat_10, LV_PART_MAIN);

    // ---- Topbar timer: update clock + coordinator icon every 30 s ----
    menu_topbar_timer = lv_timer_create(menu_topbar_update_cb, UM_MENU_TOPBAR_INTERVAL_MS, NULL);
    menu_topbar_update_cb(NULL);
}

void um_menu_destroy()
{
    if (!menu_root) return;
    if (menu_topbar_timer) { lv_timer_del(menu_topbar_timer); menu_topbar_timer = NULL; }
    menu_time_lbl      = NULL;
    menu_app_lbl       = NULL;
    menu_coord_icon    = NULL;
    menu_lora_icon     = NULL;
    menu_msg_badge_lbl = NULL;
    menu_bat_lbl       = NULL;
    lv_group_t *g = lv_group_get_default();
    if (g) {
        menu_reset_indev_state();
        lv_group_remove_all_objs(g);
    }
    lv_obj_del(menu_root);
    menu_root = NULL;
    for (int i = 0; i < TILE_COUNT; i++) menu_tiles[i] = NULL;
}
