#include <Arduino.h>
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <lvgl.h>
#ifndef SIM_BUILD
#include <ArduinoJson.h>
#include <SD.h>
#endif
#include "um_nav.h"
#include "um_shared.h"
#include "helpers/um_storage.h"
#include "config.h"

// Max message files shown in the list
#define SD_MAX_MSG_FILES 48

static lv_obj_t   *sd_root      = NULL;
static lv_obj_t   *sd_list_cont = NULL;
static lv_obj_t   *sd_preview   = NULL;
static lv_group_t *sd_grp       = NULL;

// Static path storage per message row
static char sd_paths[SD_MAX_MSG_FILES][80];
static int  sd_path_count = 0;

// -------------------------------------------------------
// Forward declarations
// -------------------------------------------------------
static void sd_close_preview();
static void sd_show_preview(const char *filepath);

// -------------------------------------------------------
// ESC / back key handler — attached to every focusable obj
// -------------------------------------------------------
static void sd_esc_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) {
        if (sd_preview) sd_close_preview();
        else             um_nav_back();
    }
}

// -------------------------------------------------------
// Key handler for message rows (ENTER → preview, ESC → back)
// -------------------------------------------------------
static void sd_msg_row_key_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    const char *path = (const char *)lv_event_get_user_data(e);
    if (key == LV_KEY_ENTER) {
        if (path) sd_show_preview(path);
    } else if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) {
        if (sd_preview) sd_close_preview();
        else             um_nav_back();
    }
}

// -------------------------------------------------------
// Close preview
// -------------------------------------------------------
static void sd_close_preview()
{
    if (!sd_preview) return;
    lv_obj_del(sd_preview);
    sd_preview = NULL;
}

// -------------------------------------------------------
// Show message preview modal
// -------------------------------------------------------
void sd_show_preview(const char *filepath)
{
    sd_close_preview();

    // Full-screen semi-transparent overlay (sibling of sd_root)
    sd_preview = lv_obj_create(lv_scr_act());
    lv_obj_set_size(sd_preview, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(sd_preview, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sd_preview, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(sd_preview, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sd_preview, 14, LV_PART_MAIN);
    lv_obj_clear_flag(sd_preview, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(sd_preview, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(sd_preview,
        [](lv_event_t *) { sd_close_preview(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(sd_preview, [](lv_event_t *e) {
        uint32_t k = lv_event_get_key(e);
        if (k == LV_KEY_ESC || k == LV_KEY_BACKSPACE || k == LV_KEY_ENTER)
            sd_close_preview();
    }, LV_EVENT_KEY, NULL);

    // Card
    lv_obj_t *card = lv_obj_create(sd_preview);
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
    char msg_str[256]  = "(empty)";

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

    // Timestamp from filename: YYYYMMDD_HHMMSS_NNNN_ricXXX.json
    const char *slash = strrchr(filepath, '/');
    const char *fn = slash ? slash + 1 : filepath;
    char ts[32] = {};
    if (strlen(fn) >= 15) {
        snprintf(ts, sizeof(ts), "%.4s-%.2s-%.2s  %.2s:%.2s:%.2s",
                 fn, fn+4, fn+6, fn+9, fn+11, fn+13);
    } else {
        strncpy(ts, fn, sizeof(ts) - 1);
    }

    // Timestamp
    lv_obj_t *ts_lbl = lv_label_create(card);
    lv_label_set_text(ts_lbl, ts);
    lv_obj_set_style_text_font(ts_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(ts_lbl, um_col_text_hint(), LV_PART_MAIN);

    // RIC + func
    char ric_buf[48];
    snprintf(ric_buf, sizeof(ric_buf), "RIC %lu  func %u", (unsigned long)ric, func);
    lv_obj_t *ric_lbl = lv_label_create(card);
    lv_label_set_text(ric_lbl, ric_buf);
    lv_obj_set_style_text_font(ric_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(ric_lbl, um_col_cyan(), LV_PART_MAIN);

    // From
    char from_buf[52];
    snprintf(from_buf, sizeof(from_buf), LV_SYMBOL_WIFI "  %s", from_str);
    lv_obj_t *from_lbl = lv_label_create(card);
    lv_label_set_text(from_lbl, from_buf);
    lv_obj_set_style_text_font(from_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(from_lbl, um_col_text_sub(), LV_PART_MAIN);

    // Divider
    lv_obj_t *div = lv_obj_create(card);
    lv_obj_set_size(div, lv_pct(100), 1);
    lv_obj_set_style_bg_color(div, um_col_divider(), LV_PART_MAIN);
    lv_obj_set_style_border_width(div, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(div, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(div, 0, LV_PART_MAIN);

    // Message text
    lv_obj_t *msg_lbl = lv_label_create(card);
    lv_label_set_text(msg_lbl, msg_str);
    lv_obj_set_width(msg_lbl, lv_pct(100));
    lv_obj_set_style_text_font(msg_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(msg_lbl, um_col_text(), LV_PART_MAIN);
    lv_label_set_long_mode(msg_lbl, LV_LABEL_LONG_WRAP);
#else
    lv_obj_t *sim_lbl = lv_label_create(card);
    lv_label_set_text(sim_lbl, "Preview not available\nin simulator");
    lv_obj_set_style_text_color(sim_lbl, um_col_text_hint(), LV_PART_MAIN);
    lv_obj_set_style_text_font(sim_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
#endif

    // Dismiss hint
    lv_obj_t *hint = lv_label_create(card);
    lv_label_set_text(hint, LV_SYMBOL_LEFT "  click or press to close");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, um_col_text_dim(), LV_PART_MAIN);
    lv_obj_set_style_pad_top(hint, 4, LV_PART_MAIN);

    // Focus overlay so it receives key events
    if (sd_grp) {
        lv_group_add_obj(sd_grp, sd_preview);
        lv_group_focus_obj(sd_preview);
    }
}

// -------------------------------------------------------
// Non-clickable info row (section headers, empty, ota/logs)
// -------------------------------------------------------
static void sd_add_row(const char *icon, const char *name,
                       const char *size_str, lv_color_t name_col)
{
    lv_obj_t *row = lv_obj_create(sd_list_cont);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(row, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(row, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, 6, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_bg_color(row, um_col_focus_cyan(),
        (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER,
        (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(row, sd_esc_cb, LV_EVENT_KEY, NULL);
    if (sd_grp) lv_group_add_obj(sd_grp, row);

    lv_obj_t *ico = lv_label_create(row);
    lv_label_set_text(ico, icon);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(ico, name_col, LV_PART_MAIN);
    lv_obj_set_width(ico, 20);

    lv_obj_t *name_lbl = lv_label_create(row);
    lv_label_set_text(name_lbl, name);
    lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(name_lbl, name_col, LV_PART_MAIN);
    lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_SCROLL);
    lv_obj_set_flex_grow(name_lbl, 1);

    lv_obj_t *sz_lbl = lv_label_create(row);
    lv_label_set_text(sz_lbl, size_str);
    lv_obj_set_style_text_font(sz_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(sz_lbl, um_col_text_inactive(), LV_PART_MAIN);
    lv_obj_set_style_text_align(sz_lbl, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_width(sz_lbl, 72);
}

// -------------------------------------------------------
// Clickable message row — focusable, added to group
// -------------------------------------------------------
static void sd_add_msg_row(const char *name, const char *size_str,
                           const char *fullpath)
{
    if (sd_path_count >= SD_MAX_MSG_FILES) return;
    strncpy(sd_paths[sd_path_count], fullpath, sizeof(sd_paths[0]) - 1);
    const char *path_ptr = sd_paths[sd_path_count++];

    lv_obj_t *btn = lv_obj_create(sd_list_cont);
    lv_obj_set_width(btn, lv_pct(100));
    lv_obj_set_height(btn, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
    // Focused highlight
    lv_obj_set_style_bg_color(btn, um_col_focus_cyan(),
        (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER,
        (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_radius(btn, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(btn, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(btn, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_column(btn, 6, LV_PART_MAIN);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *ico = lv_label_create(btn);
    lv_label_set_text(ico, LV_SYMBOL_FILE);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(ico, um_col_green(), LV_PART_MAIN);
    lv_obj_set_width(ico, 20);

    lv_obj_t *name_lbl = lv_label_create(btn);
    lv_label_set_text(name_lbl, name);
    lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(name_lbl, um_col_text(), LV_PART_MAIN);
    lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_SCROLL);
    lv_obj_set_flex_grow(name_lbl, 1);

    lv_obj_t *sz_lbl = lv_label_create(btn);
    lv_label_set_text(sz_lbl, size_str);
    lv_obj_set_style_text_font(sz_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(sz_lbl, um_col_text_inactive(), LV_PART_MAIN);
    lv_obj_set_style_text_align(sz_lbl, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_width(sz_lbl, 72);

    // Click (touch or encoder ENTER) → open preview
    lv_obj_add_event_cb(btn, [](lv_event_t *e) {
        sd_show_preview((const char *)lv_event_get_user_data(e));
    }, LV_EVENT_CLICKED, (void *)path_ptr);

    // Key handler (ESC/BACKSPACE → back, ENTER → preview)
    lv_obj_add_event_cb(btn, sd_msg_row_key_cb, LV_EVENT_KEY, (void *)path_ptr);

    if (sd_grp) lv_group_add_obj(sd_grp, btn);
}

// -------------------------------------------------------
// Populate list from SD card
// -------------------------------------------------------
static void sd_populate()
{
    if (!sd_list_cont) return;
    lv_obj_clean(sd_list_cont);
    sd_path_count = 0;

    if (!um_sd_online) {
        lv_obj_t *lbl = lv_label_create(sd_list_cont);
        lv_label_set_text(lbl, "No SD card detected");
        lv_obj_set_style_text_color(lbl, lv_color_make(200, 60, 60), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
        return;
    }

#ifndef SIM_BUILD
    // Disk usage summary
    uint64_t total = um_storage_total_bytes();
    uint64_t used  = um_storage_used_bytes();
    char usage[52];
    snprintf(usage, sizeof(usage), "%.1f / %.1f MB used",
             (double)used  / (1024.0 * 1024.0),
             (double)total / (1024.0 * 1024.0));
    lv_obj_t *sum = lv_label_create(sd_list_cont);
    lv_label_set_text(sum, usage);
    lv_obj_set_style_text_font(sum, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(sum, um_col_text_hint(), LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(sum, 6, LV_PART_MAIN);
    // First focusable item in the list — navigating back up past the first
    // file row lands here and scroll_to_view pulls the page to the top.
    lv_obj_add_flag(sum, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(sum, um_col_focus_cyan(),
        (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_bg_opa(sum, LV_OPA_COVER,
        (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_add_event_cb(sum, [](lv_event_t *ev) {
        lv_obj_scroll_to_view(lv_event_get_target_obj(ev), LV_ANIM_ON);
    }, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(sum, sd_esc_cb, LV_EVENT_KEY, NULL);
    if (sd_grp) lv_group_add_obj(sd_grp, sum);

    const char *dirs[] = { UM_SD_DIR_MESSAGES, UM_SD_DIR_OTA, UM_SD_DIR_LOGS };
    for (int di = 0; di < 3; di++) {
        const char *dir_path  = dirs[di];
        bool        is_messages = (di == 0);

        // Section header
        lv_obj_t *sec = lv_label_create(sd_list_cont);
        lv_label_set_text(sec, dir_path + 1);   // strip leading '/'
        lv_obj_set_width(sec, lv_pct(100));
        lv_obj_set_style_text_font(sec, &lv_font_montserrat_12, LV_PART_MAIN);
        lv_obj_set_style_text_color(sec, um_col_cyan(), LV_PART_MAIN);
        lv_obj_set_style_pad_top(sec, 6, LV_PART_MAIN);

        // Divider
        lv_obj_t *div = lv_obj_create(sd_list_cont);
        lv_obj_set_size(div, lv_pct(100), 1);
        lv_obj_set_style_bg_color(div, um_col_divider(), LV_PART_MAIN);
        lv_obj_set_style_border_width(div, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(div, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(div, 0, LV_PART_MAIN);

        File dir = SD.open(dir_path);
        if (!dir || !dir.isDirectory()) {
            if (dir) dir.close();
            sd_add_row(LV_SYMBOL_WARNING, "(not found)", "", um_col_text_inactive());
            continue;
        }

        int count = 0;
        while (true) {
            File entry = dir.openNextFile();
            if (!entry) break;

            char sz[16];
            uint32_t bytes = entry.size();
            if (bytes >= 1024 * 1024)
                snprintf(sz, sizeof(sz), "%.1f MB", (double)bytes / (1024.0 * 1024.0));
            else if (bytes >= 1024)
                snprintf(sz, sizeof(sz), "%.1f KB", (double)bytes / 1024.0);
            else
                snprintf(sz, sizeof(sz), "%lu B", (unsigned long)bytes);

            if (entry.isDirectory()) {
                sd_add_row(LV_SYMBOL_DIRECTORY, entry.name(),
                           "", lv_color_make(100, 180, 255));
            } else if (is_messages) {
                char fullpath[80];
                snprintf(fullpath, sizeof(fullpath), "%s/%s", dir_path, entry.name());
                sd_add_msg_row(entry.name(), sz, fullpath);
            } else {
                sd_add_row(LV_SYMBOL_FILE, entry.name(), sz, um_col_text());
            }
            entry.close();
            count++;
        }
        dir.close();

        if (count == 0)
            sd_add_row(LV_SYMBOL_LIST, "(empty)", "", um_col_text_inactive());
    }
#else
    lv_obj_t *lbl = lv_label_create(sd_list_cont);
    lv_label_set_text(lbl, "SD not available in simulator");
    lv_obj_set_style_text_color(lbl, um_col_text_hint(), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
#endif
}

// -------------------------------------------------------
// Build screen
// -------------------------------------------------------
void um_sd_create()
{
    sd_path_count = 0;

    sd_root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(sd_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(sd_root, um_col_bg(), LV_PART_MAIN);
    lv_obj_set_style_border_width(sd_root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(sd_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sd_root, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(sd_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(sd_root, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(sd_root, LV_OBJ_FLAG_SCROLLABLE);

    // ---- Header bar (fixed — not inside the scrollable list) ----
    lv_obj_t *hdr = lv_obj_create(sd_root);
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
    lv_label_set_text(title, LV_SYMBOL_SD_CARD "  Storage");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, um_col_text(), LV_PART_MAIN);
    lv_obj_set_flex_grow(title, 1);

    // Home / back button
    lv_obj_t *back_btn = lv_btn_create(hdr);
    lv_obj_set_size(back_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(back_btn, um_col_focus_cyan(),
                              (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_COVER,
                            (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_border_width(back_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(back_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(back_btn, 2, LV_PART_MAIN);
    lv_obj_add_event_cb(back_btn,
        [](lv_event_t *) { um_nav_back(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(back_btn, sd_esc_cb, LV_EVENT_KEY, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_HOME);
    lv_obj_set_style_text_color(back_lbl, um_col_cyan(), LV_PART_MAIN);
    lv_obj_center(back_lbl);

    // ---- Scrollable list ----
    sd_list_cont = lv_obj_create(sd_root);
    lv_obj_set_width(sd_list_cont, lv_pct(100));
    lv_obj_set_flex_grow(sd_list_cont, 1);
    lv_obj_set_style_bg_opa(sd_list_cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(sd_list_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(sd_list_cont, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(sd_list_cont, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(sd_list_cont, 2, LV_PART_MAIN);
    lv_obj_set_flex_flow(sd_list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(sd_list_cont, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(sd_list_cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(sd_list_cont, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_clear_flag(sd_list_cont, LV_OBJ_FLAG_SCROLL_CHAIN_VER);

    // Group: back_btn first, then message rows added during populate
    sd_grp = lv_group_get_default();
    if (sd_grp) {
        lv_group_add_obj(sd_grp, back_btn);
        lv_group_focus_obj(back_btn);
    }

    sd_populate();

    // Skip back_btn and start at the disk usage summary (top of list)
    if (sd_grp) lv_group_focus_next(sd_grp);
}

void um_sd_destroy()
{
    if (!sd_root) return;
    if (sd_grp) lv_group_remove_all_objs(sd_grp);
    if (sd_preview) {
        lv_obj_del(sd_preview);
        sd_preview = NULL;
    }
    lv_obj_del(sd_root);
    sd_root       = NULL;
    sd_list_cont  = NULL;
    sd_grp        = NULL;
    sd_path_count = 0;
}
