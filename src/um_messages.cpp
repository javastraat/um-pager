#include <Arduino.h>
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <lvgl.h>
#include <string.h>
#ifndef SIM_BUILD
#include <ArduinoJson.h>
#include <SD.h>
#endif
#include "um_nav.h"
#include "um_theme.h"
#include "um_shared.h"
#include "config.h"
#include "helpers/um_storage.h"
#include "um_mesh_api.h"

// Maximum number of messages shown in the inbox list
#define MSG_MAX_FILES  64
#define MSG_PATH_LEN   96

static lv_obj_t   *msg_root      = nullptr;
static lv_obj_t   *msg_list_cont = nullptr;
static lv_obj_t   *msg_overlay   = nullptr;   // reader overlay (nullptr when closed)
static lv_obj_t   *msg_compose_popup = nullptr; // compose popup (nullptr when closed)
static lv_obj_t   *msg_compose_ta         = nullptr;
static lv_obj_t   *msg_compose_method_dd  = nullptr;
static lv_obj_t   *msg_compose_count_lbl  = nullptr;
static lv_obj_t   *msg_compose_btn        = nullptr;
static bool        msg_compose_mesh_first = true; // true: dd index 0 = ESP-NOW, false: index 0 = LoRa

static void msg_compose_update_count() {
    if (!msg_compose_ta || !msg_compose_count_lbl) return;
    size_t len = strlen(lv_textarea_get_text(msg_compose_ta));
    char buf[16];
    snprintf(buf, sizeof(buf), "%u/180", (unsigned)len);
    lv_label_set_text(msg_compose_count_lbl, buf);
}

// Compose popup close
static void msg_compose_close() {
    if (!msg_compose_popup) return;
    lv_group_t *g = lv_group_get_default();
    lv_obj_del(msg_compose_popup);
    msg_compose_popup       = nullptr;
    msg_compose_ta          = nullptr;
    msg_compose_method_dd   = nullptr;
    msg_compose_count_lbl   = nullptr;
    if (g && msg_compose_btn) lv_group_focus_obj(msg_compose_btn);
}

static void msg_compose_submit() {
    if (!msg_compose_ta) return;
    const char *raw = lv_textarea_get_text(msg_compose_ta);
    if (!raw || !*raw) return;

    // Strip trailing whitespace/newlines added by the textarea widget
    char msg[181];
    strncpy(msg, raw, sizeof(msg) - 1);
    msg[sizeof(msg) - 1] = '\0';
    size_t len = strlen(msg);
    while (len > 0 && (msg[len - 1] == '\n' || msg[len - 1] == '\r' || msg[len - 1] == ' '))
        msg[--len] = '\0';
    if (len == 0) return;

    uint16_t sel = msg_compose_method_dd ? lv_dropdown_get_selected(msg_compose_method_dd) : 0;
    bool use_lora = msg_compose_mesh_first ? (sel == 1) : (sel == 0);

    if (use_lora) {
        extern void lora_queue_message(const char *msg, uint8_t appId);
        lora_queue_message(msg, 0x01);
    } else {
        if (um_mesh_has_coordinator()) {
            JsonDocument doc;
            doc["name"] = NODE_NAME;
            doc["msg"] = msg;
            String payload;
            serializeJson(doc, payload);
            um_mesh_send_to_coordinator(0x01, payload);
        }
    }
    msg_compose_close();
}

// Compose popup open
static void msg_compose_open() {
    if (msg_compose_popup) return;

    bool mesh_active = um_mesh_has_coordinator();
    bool lora_active = um_lora_background_active();
    if (!mesh_active && !lora_active) return;

    msg_compose_mesh_first = mesh_active; // ESP-NOW at index 0 when coordinator present

    msg_compose_popup = lv_obj_create(lv_scr_act());
    lv_obj_set_size(msg_compose_popup, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(msg_compose_popup, 0, 0);
    lv_obj_set_style_bg_color(msg_compose_popup, um_col_surface(), LV_PART_MAIN);
    lv_obj_set_style_border_color(msg_compose_popup, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_border_width(msg_compose_popup, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(msg_compose_popup, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(msg_compose_popup, 24, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(msg_compose_popup, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(msg_compose_popup, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_pad_all(msg_compose_popup, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(msg_compose_popup, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(msg_compose_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(msg_compose_popup, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(msg_compose_popup, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t *title = lv_label_create(msg_compose_popup);
    lv_label_set_text(title, LV_SYMBOL_ENVELOPE "  Compose Message");
    lv_obj_set_style_text_color(title, um_col_ok(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);

    // Hint
    lv_obj_t *hint = lv_label_create(msg_compose_popup);
    lv_label_set_text(hint, "Type a short message. Use Send to transmit.");
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_color(hint, um_col_text_dim(), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, LV_PART_MAIN);

    // Via row: label + method dropdown
    lv_obj_t *via_row = lv_obj_create(msg_compose_popup);
    lv_obj_set_width(via_row, lv_pct(100));
    lv_obj_set_height(via_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(via_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(via_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(via_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(via_row, 4, LV_PART_MAIN);
    lv_obj_clear_flag(via_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(via_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(via_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *via_lbl = lv_label_create(via_row);
    lv_label_set_text(via_lbl, "Via:   ");
    lv_obj_set_style_text_color(via_lbl, um_col_text_dim(), LV_PART_MAIN);
    lv_obj_set_style_text_font(via_lbl, &lv_font_montserrat_12, LV_PART_MAIN);

    // Build dropdown options and default selection
    const char *dd_opts;
    uint16_t    dd_default = 0;
    if (mesh_active && lora_active) {
        dd_opts    = "ESP-NOW\nLoRa";
        dd_default = 0; // ESP-NOW first
    } else if (mesh_active) {
        dd_opts    = "ESP-NOW";
        dd_default = 0;
    } else {
        dd_opts    = "LoRa";
        dd_default = 0;
    }

    msg_compose_method_dd = lv_dropdown_create(via_row);
    lv_obj_set_flex_grow(msg_compose_method_dd, 1);
    lv_dropdown_set_options(msg_compose_method_dd, dd_opts);
    lv_dropdown_set_selected(msg_compose_method_dd, dd_default);
    lv_obj_set_style_bg_color(msg_compose_method_dd, um_col_bg(), LV_PART_MAIN);
    lv_obj_set_style_text_color(msg_compose_method_dd, um_col_orange(), LV_PART_MAIN);
    lv_obj_set_style_border_color(msg_compose_method_dd, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_border_width(msg_compose_method_dd, 1, LV_PART_MAIN);
    lv_obj_add_flag(msg_compose_method_dd, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_text_font(msg_compose_method_dd, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_add_event_cb(msg_compose_method_dd, [](lv_event_t *e) {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) msg_compose_close();
    }, LV_EVENT_KEY, nullptr);

    // Textarea
    msg_compose_ta = lv_textarea_create(msg_compose_popup);
    lv_obj_set_width(msg_compose_ta, lv_pct(100));
    lv_obj_set_height(msg_compose_ta, 64);
    lv_textarea_set_placeholder_text(msg_compose_ta, "Message to send");
    lv_textarea_set_max_length(msg_compose_ta, 180);
    lv_textarea_set_one_line(msg_compose_ta, false);
    lv_obj_add_state(msg_compose_ta, LV_STATE_EDITED);
    lv_obj_set_style_bg_color(msg_compose_ta, um_col_bg(), LV_PART_MAIN);
    lv_obj_set_style_text_color(msg_compose_ta, um_col_text(), LV_PART_MAIN);
    lv_obj_set_style_border_color(msg_compose_ta, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_border_width(msg_compose_ta, 1, LV_PART_MAIN);
    lv_obj_add_flag(msg_compose_ta, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(msg_compose_ta, [](lv_event_t *) {
        msg_compose_update_count();
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(msg_compose_ta, [](lv_event_t *e) {
        lv_obj_t *ta   = (lv_obj_t *)lv_event_get_target(e);
        uint32_t  key  = lv_event_get_key(e);
        bool      edit = lv_obj_has_state(ta, LV_STATE_EDITED);
        lv_group_t *g  = lv_group_get_default();
        if (key == LV_KEY_ENTER) {
            if (edit) {
                lv_obj_clear_state(ta, LV_STATE_EDITED);
                if (g) lv_group_set_editing(g, false);
                if (g) lv_group_focus_next(g);
            } else {
                lv_obj_add_state(ta, LV_STATE_EDITED);
                if (g) lv_group_set_editing(g, true);
            }
            lv_event_stop_processing(e);
        } else if ((key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) && edit) {
            lv_obj_clear_state(ta, LV_STATE_EDITED);
            if (g) lv_group_set_editing(g, false);
            lv_event_stop_processing(e);
        } else if (key == LV_KEY_ESC) {
            msg_compose_close();
        }
    }, LV_EVENT_KEY, nullptr);

    // Char count label
    msg_compose_count_lbl = lv_label_create(msg_compose_popup);
    lv_obj_set_width(msg_compose_count_lbl, lv_pct(100));
    lv_obj_set_style_text_align(msg_compose_count_lbl, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_color(msg_compose_count_lbl, um_col_text_hint(), LV_PART_MAIN);
    lv_obj_set_style_text_font(msg_compose_count_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    msg_compose_update_count();

    // Button row
    lv_obj_t *btn_row = lv_obj_create(msg_compose_popup);
    lv_obj_set_width(btn_row, lv_pct(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(btn_row, 6, LV_PART_MAIN);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Send button
    lv_obj_t *send_btn = lv_btn_create(btn_row);
    lv_obj_set_flex_grow(send_btn, 1);
    lv_obj_set_style_bg_color(send_btn, UM_COL(0,50,20, 200,242,215), LV_PART_MAIN);
    lv_obj_set_style_bg_color(send_btn, um_col_focus_cyan(),
                              (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_border_color(send_btn, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_border_width(send_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(send_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(send_btn, 6, LV_PART_MAIN);
    lv_obj_add_flag(send_btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(send_btn, [](lv_event_t *) { msg_compose_submit(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(send_btn, [](lv_event_t *e) {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) msg_compose_close();
    }, LV_EVENT_KEY, nullptr);
    lv_obj_t *send_lbl = lv_label_create(send_btn);
    lv_label_set_text(send_lbl, LV_SYMBOL_UPLOAD "  Send");
    lv_obj_set_style_text_color(send_lbl, um_col_ok(), LV_PART_MAIN);
    lv_obj_center(send_lbl);

    // Cancel button
    lv_obj_t *cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_flex_grow(cancel_btn, 1);
    lv_obj_set_style_bg_color(cancel_btn, UM_COL(30,20,20, 242,215,215), LV_PART_MAIN);
    lv_obj_set_style_bg_color(cancel_btn, um_col_focus_red(),
                              (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_border_color(cancel_btn, UM_COL(80,40,40, 210,165,165), LV_PART_MAIN);
    lv_obj_set_style_border_width(cancel_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(cancel_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(cancel_btn, 6, LV_PART_MAIN);
    lv_obj_add_flag(cancel_btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t *) { msg_compose_close(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t *e) {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) msg_compose_close();
    }, LV_EVENT_KEY, nullptr);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, LV_SYMBOL_CLOSE "  Cancel");
    lv_obj_set_style_text_color(cancel_lbl, UM_COL(180,100,100, 150,35,35), LV_PART_MAIN);
    lv_obj_center(cancel_lbl);

    // Keyboard group navigation: dropdown → textarea → send → cancel
    lv_group_t *g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, msg_compose_method_dd);
        lv_group_add_obj(g, msg_compose_ta);
        lv_group_add_obj(g, send_btn);
        lv_group_add_obj(g, cancel_btn);
        lv_group_focus_obj(msg_compose_method_dd);
    }
}
static lv_group_t *msg_grp         = nullptr;
static lv_obj_t   *msg_back_btn    = nullptr;

// Per-row path storage (stable pointers used by callbacks)
static char msg_paths[MSG_MAX_FILES][MSG_PATH_LEN];
static int  msg_path_count = 0;

// Path of the message currently open in the reader
static char msg_reader_path[MSG_PATH_LEN];

// -------------------------------------------------------
// Forward declarations
// -------------------------------------------------------
static void msg_close_overlay(void);
static void msg_show_reader(const char *filepath);
static void msg_populate(void);
static void msg_repopulate(void);

// -------------------------------------------------------
// Helpers
// -------------------------------------------------------

// Parse a pretty timestamp from the filename.
// Filename format: YYYYMMDD_HHMMSS_NNNN_ricXXX.json
// Output example: "10 Apr  15:30"
static void msg_parse_ts(const char *fn, char *out, size_t out_len)
{
    static const char *months[] = {
        "", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    if (strlen(fn) >= 15) {
        int mo = (fn[4] - '0') * 10 + (fn[5] - '0');
        if (mo < 1 || mo > 12) mo = 1;
        snprintf(out, out_len, "%c%c %s  %c%c:%c%c",
                 fn[6], fn[7], months[mo],
                 fn[9], fn[10], fn[11], fn[12]);
    } else {
        strncpy(out, fn, out_len - 1);
        out[out_len - 1] = '\0';
    }
}

// Accent colour by message type
static lv_color_t msg_accent(uint32_t ric, uint8_t func)
{
    if (ric == UM_RIC_MY_PAGER) return um_col_green_bright(); // personal
    if (ric == 0 && func == 0)  return um_col_cyan_bright();  // direct MAC
    if (func == 0xFF)           return um_col_orange();       // coordinator cmd
    return um_col_text();                                     // broadcast
}

// -------------------------------------------------------
// ESC / BACKSPACE key handler — attached to every focusable obj
// -------------------------------------------------------
static void msg_esc_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) {
        if (msg_compose_popup) msg_compose_close();
        else if (msg_overlay)  msg_close_overlay();
        else                   um_nav_back();
    }
}

// -------------------------------------------------------
// Close reader overlay and restore list focus
// -------------------------------------------------------
static void msg_close_overlay(void)
{
    if (!msg_overlay) return;
    lv_obj_del(msg_overlay);   // LVGL 9 auto-removes children from their groups
    msg_overlay = nullptr;
    // Restore focus to first message row (or back_btn if list is empty)
    if (msg_grp && msg_back_btn) {
        lv_group_focus_obj(msg_back_btn);
        if (msg_path_count > 0)
            lv_group_focus_next(msg_grp);
    }
}

// -------------------------------------------------------
// Key handler for message list rows
// -------------------------------------------------------
static void msg_row_key_cb(lv_event_t *e)
{
    uint32_t key     = lv_event_get_key(e);
    const char *path = (const char *)lv_event_get_user_data(e);
    if (key == LV_KEY_ENTER) {
        if (path) msg_show_reader(path);
    } else if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) {
        um_nav_back();
    }
}

// -------------------------------------------------------
// Add one message row to the list.
// Flat flex-ROW layout (no nested containers) so LVGL 9 can
// correctly compute content height and scroll the list.
// -------------------------------------------------------
static void msg_add_row(const char *fullpath,
                        const char *ts, const char *ric_str,
                        const char *preview, lv_color_t accent)
{
    if (msg_path_count >= MSG_MAX_FILES) return;
    strncpy(msg_paths[msg_path_count], fullpath, MSG_PATH_LEN - 1);
    msg_paths[msg_path_count][MSG_PATH_LEN - 1] = '\0';
    const char *path_ptr = msg_paths[msg_path_count++];

    lv_obj_t *btn = lv_obj_create(msg_list_cont);
    lv_obj_set_width(btn, lv_pct(100));
    lv_obj_set_height(btn, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, um_col_focus_cyan(),
        (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER,
        (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_radius(btn, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(btn, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(btn, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_column(btn, 6, LV_PART_MAIN);
    // Bottom border acts as divider — no extra child object needed
    lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, um_col_divider(), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    // Flat flex-ROW: icon | body (2-line label) | ric tag
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Accent envelope icon
    lv_obj_t *ico_lbl = lv_label_create(btn);
    lv_label_set_text(ico_lbl, LV_SYMBOL_ENVELOPE);
    lv_obj_set_style_text_color(ico_lbl, accent, LV_PART_MAIN);
    lv_obj_set_style_text_font(ico_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_width(ico_lbl, 20);

    // Body: timestamp on line 1, message preview on line 2
    char body[100];
    snprintf(body, sizeof(body), "%s\n%s", ts, preview);
    lv_obj_t *body_lbl = lv_label_create(btn);
    lv_label_set_text(body_lbl, body);
    lv_obj_set_flex_grow(body_lbl, 1);
    lv_obj_set_style_text_font(body_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(body_lbl, um_col_text_sub(), LV_PART_MAIN);
    lv_label_set_long_mode(body_lbl, LV_LABEL_LONG_DOT);

    // RIC / type tag (right side)
    lv_obj_t *ric_lbl = lv_label_create(btn);
    lv_label_set_text(ric_lbl, ric_str);
    lv_obj_set_style_text_font(ric_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(ric_lbl, accent, LV_PART_MAIN);
    lv_obj_set_width(ric_lbl, 68);
    lv_obj_set_style_text_align(ric_lbl, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

    lv_obj_add_event_cb(btn, [](lv_event_t *e) {
        msg_show_reader((const char *)lv_event_get_user_data(e));
    }, LV_EVENT_CLICKED, (void *)path_ptr);
    lv_obj_add_event_cb(btn, msg_row_key_cb, LV_EVENT_KEY, (void *)path_ptr);

    if (msg_grp) lv_group_add_obj(msg_grp, btn);
}

// -------------------------------------------------------
// Show full-screen reader overlay for one message
// -------------------------------------------------------
static void msg_show_reader(const char *filepath)
{
    if (msg_overlay) msg_close_overlay();

    strncpy(msg_reader_path, filepath, MSG_PATH_LEN - 1);
    msg_reader_path[MSG_PATH_LEN - 1] = '\0';

    // Full-screen semi-transparent backdrop (sibling of msg_root)
    msg_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(msg_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(msg_overlay, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(msg_overlay, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(msg_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(msg_overlay, 12, LV_PART_MAIN);
    lv_obj_clear_flag(msg_overlay, LV_OBJ_FLAG_SCROLLABLE);

    // Card
    lv_obj_t *card = lv_obj_create(msg_overlay);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, um_col_surface(), LV_PART_MAIN);
    lv_obj_set_style_border_color(card, um_col_divider(), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_row(card, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);

#ifndef SIM_BUILD
    char buf[512] = {};
    um_storage_read(filepath, buf, sizeof(buf));

    uint32_t ric  = 0;
    uint8_t  func = 0;
    char from_str[32] = "unknown";
    char msg_str[512] = "(empty)";

    JsonDocument jdoc;
    if (!deserializeJson(jdoc, buf)) {
        ric  = jdoc["ric"]  | 0u;
        func = jdoc["func"] | 0u;
        const char *f = jdoc["from"] | "";
        const char *m = jdoc["msg"]  | "";
        if (f[0]) strncpy(from_str, f, sizeof(from_str) - 1);
        if (m[0]) strncpy(msg_str,  m, sizeof(msg_str)  - 1);
    } else {
        strncpy(msg_str, "(parse error)", sizeof(msg_str) - 1);
    }

    const char *slash = strrchr(filepath, '/');
    const char *fn    = slash ? slash + 1 : filepath;
    char ts[32] = {};
    if (strlen(fn) >= 15) {
        snprintf(ts, sizeof(ts), "%.4s-%.2s-%.2s  %.2s:%.2s:%.2s",
                 fn, fn+4, fn+6, fn+9, fn+11, fn+13);
    } else {
        strncpy(ts, fn, sizeof(ts) - 1);
    }

    lv_color_t accent = msg_accent(ric, func);

    // Timestamp
    lv_obj_t *ts_lbl = lv_label_create(card);
    lv_label_set_text(ts_lbl, ts);
    lv_obj_set_style_text_font(ts_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(ts_lbl, um_col_text_hint(), LV_PART_MAIN);

    // RIC / message type
    char ric_buf[64];
    if (func == 0xFF)
        snprintf(ric_buf, sizeof(ric_buf),
                 LV_SYMBOL_WARNING "  CMD from coordinator");
    else if (ric == 0 && func == 0)
        snprintf(ric_buf, sizeof(ric_buf),
                 LV_SYMBOL_WIFI "  Direct message");
    else if (ric == UM_RIC_MY_PAGER)
        snprintf(ric_buf, sizeof(ric_buf),
                 LV_SYMBOL_ENVELOPE "  Personal  RIC %lu", (unsigned long)ric);
    else
        snprintf(ric_buf, sizeof(ric_buf),
                 "RIC %lu  func %u", (unsigned long)ric, func);

    lv_obj_t *ric_lbl = lv_label_create(card);
    lv_label_set_text(ric_lbl, ric_buf);
    lv_obj_set_style_text_font(ric_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(ric_lbl, accent, LV_PART_MAIN);

    // From MAC
    char from_buf[56];
    snprintf(from_buf, sizeof(from_buf), LV_SYMBOL_WIFI "  %s", from_str);
    lv_obj_t *from_lbl = lv_label_create(card);
    lv_label_set_text(from_lbl, from_buf);
    lv_obj_set_style_text_font(from_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(from_lbl, um_col_text_sub(), LV_PART_MAIN);

    // Divider
    lv_obj_t *rdiv = lv_obj_create(card);
    lv_obj_set_size(rdiv, lv_pct(100), 1);
    lv_obj_set_style_bg_color(rdiv, um_col_divider(), LV_PART_MAIN);
    lv_obj_set_style_border_width(rdiv, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(rdiv, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(rdiv, 0, LV_PART_MAIN);

    // Full message body — wrapped
    lv_obj_t *body = lv_label_create(card);
    lv_label_set_text(body, msg_str);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_style_text_font(body, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(body, um_col_text(), LV_PART_MAIN);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_pad_top(body, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(body, 8, LV_PART_MAIN);
#else
    lv_obj_t *sim_lbl = lv_label_create(card);
    lv_label_set_text(sim_lbl, "Reader not available in simulator");
    lv_obj_set_style_text_color(sim_lbl, um_col_text_hint(), LV_PART_MAIN);
    lv_obj_set_style_text_font(sim_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(sim_lbl, 8, LV_PART_MAIN);
#endif

    // Button row  [Delete]  [Close]  (right-aligned)
    lv_obj_t *brow = lv_obj_create(card);
    lv_obj_set_width(brow, lv_pct(100));
    lv_obj_set_height(brow, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(brow, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(brow, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(brow, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(brow, 8, LV_PART_MAIN);
    lv_obj_clear_flag(brow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(brow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brow, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Delete button
    lv_obj_t *del_btn = lv_btn_create(brow);
    lv_obj_set_height(del_btn, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(del_btn, lv_color_make(140, 25, 25), LV_PART_MAIN);
    lv_obj_set_style_bg_color(del_btn, um_col_focus_red(),
        (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_bg_opa(del_btn, LV_OPA_COVER,
        (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_border_width(del_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(del_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(del_btn, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(del_btn, 4, LV_PART_MAIN);
    lv_obj_t *del_lbl = lv_label_create(del_btn);
    lv_label_set_text(del_lbl, LV_SYMBOL_TRASH "  Delete");
    lv_obj_set_style_text_font(del_lbl, &lv_font_montserrat_12, LV_PART_MAIN);

    lv_obj_add_event_cb(del_btn, [](lv_event_t *) {
        um_storage_remove(msg_reader_path);
        msg_close_overlay();
        msg_repopulate();
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(del_btn, [](lv_event_t *e) {
        uint32_t k = lv_event_get_key(e);
        if (k == LV_KEY_ENTER) {
            um_storage_remove(msg_reader_path);
            msg_close_overlay();
            msg_repopulate();
        } else if (k == LV_KEY_ESC || k == LV_KEY_BACKSPACE) {
            msg_close_overlay();
        }
    }, LV_EVENT_KEY, nullptr);

    // Close button
    lv_obj_t *close_btn = lv_btn_create(brow);
    lv_obj_set_height(close_btn, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(close_btn, um_col_surface(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(close_btn, um_col_focus_cyan(),
        (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER,
        (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_border_color(close_btn, um_col_border(), LV_PART_MAIN);
    lv_obj_set_style_border_width(close_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(close_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(close_btn, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(close_btn, 4, LV_PART_MAIN);
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, LV_SYMBOL_LEFT "  Close");
    lv_obj_set_style_text_font(close_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(close_lbl, um_col_text_dim(), LV_PART_MAIN);
    lv_obj_add_event_cb(close_btn, [](lv_event_t *) { msg_close_overlay(); },
                        LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(close_btn, [](lv_event_t *e) {
        uint32_t k = lv_event_get_key(e);
        if (k == LV_KEY_ESC || k == LV_KEY_BACKSPACE || k == LV_KEY_ENTER)
            msg_close_overlay();
    }, LV_EVENT_KEY, nullptr);

    // Add both buttons to group — del first (left via encoder), focus close
    if (msg_grp) {
        lv_group_add_obj(msg_grp, del_btn);
        lv_group_add_obj(msg_grp, close_btn);
        lv_group_focus_obj(close_btn);
    }
}

// -------------------------------------------------------
// Populate message list from SD card
// -------------------------------------------------------
static void msg_populate(void)
{
    if (!msg_list_cont) return;

    if (!um_sd_online) {
        lv_obj_t *lbl = lv_label_create(msg_list_cont);
        lv_label_set_text(lbl, LV_SYMBOL_WARNING "  No SD card");
        lv_obj_set_style_text_color(lbl, lv_color_make(200, 60, 60), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
        return;
    }

#ifndef SIM_BUILD
    // Collect filenames into array first so we can reverse (newest first)
    static char names[MSG_MAX_FILES][64];
    int name_count = 0;

    File dir = SD.open(UM_SD_DIR_MESSAGES);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        lv_obj_t *lbl = lv_label_create(msg_list_cont);
        lv_label_set_text(lbl, "Messages folder not found");
        lv_obj_set_style_text_color(lbl, um_col_text_inactive(), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
        return;
    }
    while (name_count < MSG_MAX_FILES) {
        File entry = dir.openNextFile();
        if (!entry) break;
        if (!entry.isDirectory()) {
            strncpy(names[name_count], entry.name(), 63);
            names[name_count][63] = '\0';
            name_count++;
        }
        entry.close();
    }
    dir.close();

    if (name_count == 0) {
        lv_obj_t *lbl = lv_label_create(msg_list_cont);
        lv_label_set_text(lbl, LV_SYMBOL_ENVELOPE "  No messages yet");
        lv_obj_set_style_text_color(lbl, um_col_text_inactive(), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
        return;
    }

    // Reverse iteration → newest first
    for (int i = name_count - 1; i >= 0; i--) {
        const char *fn = names[i];
        char fullpath[MSG_PATH_LEN];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", UM_SD_DIR_MESSAGES, fn);

        char buf[512] = {};
        um_storage_read(fullpath, buf, sizeof(buf));

        uint32_t ric  = 0;
        uint8_t  func = 0;
        char preview[64] = {};

        JsonDocument jdoc;
        if (!deserializeJson(jdoc, buf)) {
            ric  = jdoc["ric"]  | 0u;
            func = jdoc["func"] | 0u;
            const char *m = jdoc["msg"] | "";
            strncpy(preview, m, sizeof(preview) - 1);
        } else {
            strncpy(preview, "(unreadable)", sizeof(preview) - 1);
        }

        char ts[32] = {};
        msg_parse_ts(fn, ts, sizeof(ts));

        char ric_str[24];
        if (ric == UM_RIC_MY_PAGER)
            snprintf(ric_str, sizeof(ric_str), "Personal");
        else if (ric == 0 && func == 0)
            snprintf(ric_str, sizeof(ric_str), "Direct");
        else if (func == 0xFF)
            snprintf(ric_str, sizeof(ric_str), "CMD");
        else
            snprintf(ric_str, sizeof(ric_str), "RIC %lu", (unsigned long)ric);

        msg_add_row(fullpath, ts, ric_str, preview, msg_accent(ric, func));
    }
#else
    lv_obj_t *lbl = lv_label_create(msg_list_cont);
    lv_label_set_text(lbl, "SD not available in simulator");
    lv_obj_set_style_text_color(lbl, um_col_text_hint(), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
#endif
}

// -------------------------------------------------------
// Repopulate list (called after a message is deleted)
// -------------------------------------------------------
static void msg_repopulate(void)
{
    if (!msg_grp || !msg_list_cont) return;

    lv_group_remove_all_objs(msg_grp);    // removes back_btn + all rows
    lv_obj_clean(msg_list_cont);
    msg_path_count = 0;

    if (msg_back_btn) lv_group_add_obj(msg_grp, msg_back_btn);
    msg_populate();

    if (msg_grp && msg_back_btn) {
        lv_group_focus_obj(msg_back_btn);
        if (msg_path_count > 0)
            lv_group_focus_next(msg_grp);
    }
}

// -------------------------------------------------------
// Build screen
// -------------------------------------------------------
void um_messages_create()
{
    um_unread_count = 0;
    msg_path_count  = 0;
    msg_overlay     = nullptr;

    msg_root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(msg_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(msg_root, um_col_bg(), LV_PART_MAIN);
    lv_obj_set_style_border_width(msg_root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(msg_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(msg_root, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(msg_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(msg_root, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(msg_root, LV_OBJ_FLAG_SCROLLABLE);

    // ---- Header bar ----
    lv_obj_t *hdr = lv_obj_create(msg_root);
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
    lv_label_set_text(title, LV_SYMBOL_ENVELOPE "  Messages");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, um_col_text(), LV_PART_MAIN);
    lv_obj_set_flex_grow(title, 1);

    // Home / back button
    msg_back_btn = lv_btn_create(hdr);
    lv_obj_set_size(msg_back_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(msg_back_btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(msg_back_btn, um_col_focus_cyan(),
        (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_bg_opa(msg_back_btn, LV_OPA_COVER,
        (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_border_width(msg_back_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(msg_back_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(msg_back_btn, 2, LV_PART_MAIN);
    lv_obj_add_event_cb(msg_back_btn,
        [](lv_event_t *) { um_nav_back(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(msg_back_btn, msg_esc_cb, LV_EVENT_KEY, nullptr);
    lv_obj_t *home_lbl = lv_label_create(msg_back_btn);
    lv_label_set_text(home_lbl, LV_SYMBOL_HOME);
    lv_obj_set_style_text_color(home_lbl, um_col_cyan(), LV_PART_MAIN);
    lv_obj_center(home_lbl);

    // Compose button
    msg_compose_btn = lv_btn_create(hdr);
    lv_obj_set_size(msg_compose_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(msg_compose_btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(msg_compose_btn, um_col_focus_cyan(), (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_bg_opa(msg_compose_btn, LV_OPA_COVER, (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_border_width(msg_compose_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(msg_compose_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(msg_compose_btn, 2, LV_PART_MAIN);
    lv_obj_add_event_cb(msg_compose_btn, [](lv_event_t *) { msg_compose_open(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(msg_compose_btn, msg_esc_cb, LV_EVENT_KEY, nullptr);
    lv_obj_t *compose_lbl = lv_label_create(msg_compose_btn);
    lv_label_set_text(compose_lbl, LV_SYMBOL_EDIT);
    lv_obj_set_style_text_color(compose_lbl, um_col_ok(), LV_PART_MAIN);
    lv_obj_center(compose_lbl);

    // ---- Scrollable message list ----
    msg_list_cont = lv_obj_create(msg_root);
    lv_obj_set_width(msg_list_cont, lv_pct(100));
    lv_obj_set_flex_grow(msg_list_cont, 1);
    lv_obj_set_style_bg_opa(msg_list_cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(msg_list_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(msg_list_cont, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(msg_list_cont, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(msg_list_cont, 2, LV_PART_MAIN);
    lv_obj_set_flex_flow(msg_list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(msg_list_cont, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(msg_list_cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(msg_list_cont, LV_SCROLLBAR_MODE_ACTIVE);

    // Group setup — back_btn, compose_btn, then message rows added by msg_populate()
    msg_grp = lv_group_get_default();
    if (msg_grp) lv_group_add_obj(msg_grp, msg_back_btn);
    if (msg_grp) lv_group_add_obj(msg_grp, msg_compose_btn);

    msg_populate();

    // Skip past back_btn to the first message row (if any)
    if (msg_grp && msg_back_btn) {
        lv_group_focus_obj(msg_back_btn);
        if (msg_path_count > 0)
            lv_group_focus_next(msg_grp);
    }
}

void um_messages_destroy()
{
    if (!msg_root) return;
    if (msg_grp)     lv_group_remove_all_objs(msg_grp);
    if (msg_overlay) { lv_obj_del(msg_overlay); msg_overlay = nullptr; }
    lv_obj_del(msg_root);
    msg_root        = nullptr;
    msg_list_cont   = nullptr;
    msg_back_btn    = nullptr;
    msg_compose_btn = nullptr;
    msg_grp         = nullptr;
    msg_path_count = 0;
}
