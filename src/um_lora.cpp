
#include <Arduino.h>
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <lvgl.h>
#include "um_nav.h"
#include "um_theme.h"
#include "radio/hal_interface.h"
#include "radio/hw_lr1121.h"

// --- LoRa config ---
static radio_params_t lora_params;

static const float lora_freqs[] = {
    868.0,   // EU
    868.3,   // EU
    868.8,   // EU
    902.0,   // US
    903.0,   // US
    904.6,   // US
    915.0,   // US/AU
    920.0,   // Korea
    923.0,   // Asia
    928.0,   // US/Asia
    950.0,   // Japan
    960.0,   // Upper sub-GHz
    2400.0,  // 2.4GHz LoRa
    2425.0,
    2450.0,
    2475.0,
    2483.5
};
static const int lora_freq_count = (int)(sizeof(lora_freqs) / sizeof(lora_freqs[0]));
static int lora_freq_idx = 0;  // Default: 868.0 MHz

// --- Log buffer ---
#define LORA_LOG_ROWS 32
#define LORA_LOG_COL  80

static char    lora_log[LORA_LOG_ROWS][LORA_LOG_COL] = {};
static uint8_t lora_logHead  = 0;
static uint8_t lora_logCount = 0;
static bool    lora_log_dirty = false;

#ifndef SIM_BUILD
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
static SemaphoreHandle_t lora_mutex = NULL;
#endif

// --- LVGL state ---
static lv_obj_t   *lora_root       = NULL;
static lv_obj_t   *lora_status_lbl = NULL;
static lv_obj_t   *lora_info_lbl   = NULL;
static lv_obj_t   *lora_log_cont   = NULL;
static lv_obj_t   *lora_popup_cont = NULL;
static lv_timer_t *lora_timer      = NULL;
static lv_timer_t *lora_bsp_timer  = NULL;

#define LORA_BSP_LONG_PRESS_MS  600
#define LORA_UI_TIMER_MS        200

// Forward declarations
static void lora_popup_close();
static void lora_popup_open();
static void lora_key_bsp_cb(lv_event_t *e);
static void lora_rebuild_rows();

// -------------------------------------------------------
// Log helpers
// -------------------------------------------------------
static void lora_log_push(const char *line)
{
#ifndef SIM_BUILD
    if (!lora_mutex) return;
    if (xSemaphoreTake(lora_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
#endif
    strncpy(lora_log[lora_logHead], line, LORA_LOG_COL - 1);
    lora_log[lora_logHead][LORA_LOG_COL - 1] = '\0';
    lora_logHead = (lora_logHead + 1) % LORA_LOG_ROWS;
    if (lora_logCount < LORA_LOG_ROWS) lora_logCount++;
    lora_log_dirty = true;
#ifndef SIM_BUILD
    xSemaphoreGive(lora_mutex);
#endif
}

// -------------------------------------------------------
// Radio setup
// -------------------------------------------------------
static void lora_set_radio_params()
{
    lora_params.freq      = lora_freqs[lora_freq_idx];
    lora_params.bandwidth = 125.0;
    lora_params.sf        = 12;
    lora_params.cr        = 5;
    lora_params.syncWord  = 0xCD;
    lora_params.power     = 22;
    lora_params.mode      = RADIO_RX;
#ifndef SIM_BUILD
    hw_set_radio_params(lora_params);
#endif
}

static void lora_start_receive()
{
#ifndef SIM_BUILD
    hw_set_radio_listening();
#endif
}

// -------------------------------------------------------
// RX handler — called from UI timer
// -------------------------------------------------------
static void lora_handle_rx()
{
#ifndef SIM_BUILD
    radio_rx_params_t rx;
    rx.data   = (uint8_t *)malloc(257);
    if (!rx.data) return;
    rx.length = 0;
    hw_get_radio_rx(rx);
    if (rx.length > 0 && rx.state == RADIOLIB_ERR_NONE) {
        char line[LORA_LOG_COL];
        uint8_t slen = rx.length > 56 ? 56 : rx.length;
        char snippet[57] = {};
        for (int i = 0; i < slen; i++) {
            uint8_t b = rx.data[i];
            snippet[i] = (b >= 0x20 && b < 0x7F) ? (char)b : '.';
        }
        snprintf(line, sizeof(line), "[RX] %ddBm %s", (int)rx.rssi, snippet);
        lora_log_push(line);
        Serial.printf("[LoRa RX] %d bytes rssi=%d: %s\n", rx.length, rx.rssi, (char *)rx.data);
        // Re-arm receive after read
        lora_start_receive();
    }
    free(rx.data);
#endif
}

// -------------------------------------------------------
// UI timer callback
// -------------------------------------------------------
static void lora_timer_cb(lv_timer_t *t)
{
    lora_handle_rx();

    if (lora_log_dirty && lora_log_cont)
        lora_rebuild_rows();

    if (lora_info_lbl) {
        char buf[48];
        snprintf(buf, sizeof(buf), "Freq: %.3f MHz  SF12  BW125", lora_freqs[lora_freq_idx]);
        lv_label_set_text(lora_info_lbl, buf);
    }
}

// -------------------------------------------------------
// Rebuild message rows (orange accent, matching um_mesh style)
// -------------------------------------------------------
static void lora_rebuild_rows()
{
    if (!lora_log_cont) return;

    lv_group_t *g = lv_group_get_default();
    lv_obj_clean(lora_log_cont);

#ifndef SIM_BUILD
    if (!lora_mutex || xSemaphoreTake(lora_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
#endif

    int start = (LORA_LOG_ROWS + lora_logHead - lora_logCount) % LORA_LOG_ROWS;
    for (int i = lora_logCount - 1; i >= 0; i--) {
        int idx = (start + i) % LORA_LOG_ROWS;

        lv_obj_t *row = lv_btn_create(lora_log_cont);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(row, 2, LV_PART_MAIN);
        lv_obj_set_style_pad_ver(row, 1, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(row, 3, LV_PART_MAIN);
        lv_obj_set_style_bg_color(row, UM_COL(80,35,0, 242,215,180),
                                  (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER,
                                (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, lora_log[idx]);
        lv_obj_set_width(lbl, lv_pct(100));
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(lbl, um_col_orange(), LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, um_col_warn(),
                                    (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));

        lv_obj_add_event_cb(row, lora_key_bsp_cb, LV_EVENT_KEY, NULL);
        lv_obj_add_event_cb(row, [](lv_event_t *ev) {
            lv_obj_scroll_to_view(lv_event_get_target_obj(ev), LV_ANIM_ON);
        }, LV_EVENT_FOCUSED, NULL);

        if (g) lv_group_add_obj(g, row);
    }

    if (g && lv_obj_get_child_count(lora_log_cont) > 0)
        lv_group_focus_obj(lv_obj_get_child(lora_log_cont, 0));

#ifndef SIM_BUILD
    xSemaphoreGive(lora_mutex);
#endif
    lora_log_dirty = false;
}

// -------------------------------------------------------
// Options popup (frequency dropdown + apply/cancel)
// -------------------------------------------------------
static void lora_popup_close()
{
    if (!lora_popup_cont) return;
    lv_group_t *g = lv_group_get_default();
    lv_obj_del(lora_popup_cont);
    lora_popup_cont = NULL;
    if (g && lora_log_cont && lv_obj_get_child_count(lora_log_cont) > 0)
        lv_group_focus_obj(lv_obj_get_child(lora_log_cont, 0));
}

static void lora_popup_open()
{
    if (lora_popup_cont) return;

    // Build dropdown option string
    static char dd_opts[512];
    dd_opts[0] = '\0';
    for (int i = 0; i < lora_freq_count; i++) {
        char entry[24];
        snprintf(entry, sizeof(entry), "%.3f MHz", lora_freqs[i]);
        if (i > 0) strncat(dd_opts, "\n", sizeof(dd_opts) - strlen(dd_opts) - 1);
        strncat(dd_opts, entry, sizeof(dd_opts) - strlen(dd_opts) - 1);
    }

    lora_popup_cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(lora_popup_cont, 260, 168);
    lv_obj_center(lora_popup_cont);
    lv_obj_set_style_bg_color(lora_popup_cont, um_col_surface(), LV_PART_MAIN);
    lv_obj_set_style_border_color(lora_popup_cont, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_border_width(lora_popup_cont, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(lora_popup_cont, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(lora_popup_cont, 24, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(lora_popup_cont, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(lora_popup_cont, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_pad_all(lora_popup_cont, 10, LV_PART_MAIN);
    lv_obj_clear_flag(lora_popup_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(lora_popup_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(lora_popup_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(lora_popup_cont, 8, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(lora_popup_cont);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS "  LoRa Options");
    lv_obj_set_style_text_color(title, um_col_cyan_bright(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);

    lv_obj_t *freq_lbl = lv_label_create(lora_popup_cont);
    lv_label_set_text(freq_lbl, "Frequency:");
    lv_obj_set_style_text_color(freq_lbl, um_col_text_dim(), LV_PART_MAIN);
    lv_obj_set_width(freq_lbl, lv_pct(100));

    lv_obj_t *dd = lv_dropdown_create(lora_popup_cont);
    lv_obj_set_width(dd, lv_pct(100));
    lv_dropdown_set_options(dd, dd_opts);
    lv_dropdown_set_selected(dd, (uint16_t)lora_freq_idx);
    lv_obj_set_style_bg_color(dd, um_col_surface(), LV_PART_MAIN);
    lv_obj_set_style_text_color(dd, um_col_orange(), LV_PART_MAIN);
    lv_obj_set_style_border_color(dd, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_border_width(dd, 1, LV_PART_MAIN);
    lv_obj_add_event_cb(dd, [](lv_event_t *e) {
        lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
        lora_freq_idx = (int)lv_dropdown_get_selected(obj);
        lora_set_radio_params();
        lora_start_receive();
        if (lora_status_lbl) {
            char buf[24];
            snprintf(buf, sizeof(buf), "%.3f MHz", lora_freqs[lora_freq_idx]);
            lv_label_set_text(lora_status_lbl, buf);
        }
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // Button row: Apply + Cancel
    lv_obj_t *btn_row = lv_obj_create(lora_popup_cont);
    lv_obj_set_width(btn_row, lv_pct(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(btn_row, 8, LV_PART_MAIN);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *apply_btn = lv_btn_create(btn_row);
    lv_obj_set_flex_grow(apply_btn, 1);
    lv_obj_set_style_bg_color(apply_btn, UM_COL(0,50,70, 200,225,242), LV_PART_MAIN);
    lv_obj_set_style_bg_color(apply_btn, um_col_focus_cyan(),
                              (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_border_color(apply_btn, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_border_width(apply_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(apply_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(apply_btn, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(apply_btn, [](lv_event_t *e) {
        lora_set_radio_params();
        lora_start_receive();
        lora_popup_close();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *apply_lbl = lv_label_create(apply_btn);
    lv_label_set_text(apply_lbl, LV_SYMBOL_OK "  Apply");
    lv_obj_set_style_text_color(apply_lbl, um_col_cyan_bright(), LV_PART_MAIN);
    lv_obj_center(apply_lbl);

    lv_obj_t *cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_flex_grow(cancel_btn, 1);
    lv_obj_set_style_bg_color(cancel_btn, UM_COL(30,20,20, 242,215,215), LV_PART_MAIN);
    lv_obj_set_style_bg_color(cancel_btn, um_col_focus_red(),
                              (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_border_color(cancel_btn, UM_COL(80,40,40, 210,165,165), LV_PART_MAIN);
    lv_obj_set_style_border_width(cancel_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(cancel_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(cancel_btn, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t *e) { lora_popup_close(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, LV_SYMBOL_CLOSE "  Cancel");
    lv_obj_set_style_text_color(cancel_lbl, UM_COL(180,100,100, 150,35,35), LV_PART_MAIN);
    lv_obj_center(cancel_lbl);

    lv_group_t *g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, dd);
        lv_group_add_obj(g, apply_btn);
        lv_group_add_obj(g, cancel_btn);
        lv_group_focus_obj(apply_btn);
    }
}

// -------------------------------------------------------
// Long-press backspace → open options popup
// -------------------------------------------------------
static void lora_bsp_timer_cb(lv_timer_t *t)
{
    lora_bsp_timer = NULL;
    if (!lora_popup_cont)
        lora_popup_open();
}

static void lora_key_bsp_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_BACKSPACE || key == 8) {
        if (lora_bsp_timer) { lv_timer_reset(lora_bsp_timer); return; }
        lora_bsp_timer = lv_timer_create(lora_bsp_timer_cb, LORA_BSP_LONG_PRESS_MS, NULL);
        lv_timer_set_repeat_count(lora_bsp_timer, 1);
    } else if (key == LV_KEY_ESC) {
        if (lora_popup_cont) lora_popup_close();
        else um_nav_back();
    } else {
        if (lora_bsp_timer) { lv_timer_del(lora_bsp_timer); lora_bsp_timer = NULL; }
    }
}

// -------------------------------------------------------
// um_lora_create / um_lora_destroy
// -------------------------------------------------------
void um_lora_create()
{
#ifndef SIM_BUILD
    if (!lora_mutex) lora_mutex = xSemaphoreCreateMutex();
    hw_radio_begin();
#endif
    lora_set_radio_params();
    lora_start_receive();
    lora_log_push("LoRa RX ready...");

    lora_root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(lora_root, lv_pct(100), lv_pct(100));
    lv_obj_center(lora_root);
    lv_obj_set_style_bg_color(lora_root, um_col_bg(), LV_PART_MAIN);
    lv_obj_set_style_border_width(lora_root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(lora_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(lora_root, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(lora_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(lora_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(lora_root, LV_OBJ_FLAG_SCROLLABLE);

    // --- Header row ---
    lv_obj_t *hdr = lv_obj_create(lora_root);
    lv_obj_set_width(hdr, lv_pct(100));
    lv_obj_set_height(hdr, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(hdr, 2, LV_PART_MAIN);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hdr_title = lv_label_create(hdr);
    lv_label_set_text(hdr_title, LV_SYMBOL_WIFI " LoRa RX");
    lv_obj_set_style_text_color(hdr_title, um_col_orange(), LV_PART_MAIN);

    lora_status_lbl = lv_label_create(hdr);
    {
        char buf[24];
        snprintf(buf, sizeof(buf), "%.3f MHz", lora_freqs[lora_freq_idx]);
        lv_label_set_text(lora_status_lbl, buf);
    }
    lv_obj_set_style_text_color(lora_status_lbl, um_col_warn(), LV_PART_MAIN);

    // Back button (home icon, top-right)
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
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) { um_nav_back(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(back_btn, lora_key_bsp_cb, LV_EVENT_KEY, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_HOME);
    lv_obj_set_style_text_color(back_lbl, um_col_orange(), LV_PART_MAIN);
    lv_obj_center(back_lbl);

    lv_group_t *g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, back_btn);
        lv_group_focus_obj(back_btn);
    }

    // --- Divider ---
    lv_obj_t *div1 = lv_obj_create(lora_root);
    lv_obj_set_size(div1, lv_pct(100), 1);
    lv_obj_set_style_bg_color(div1, um_col_divider(), LV_PART_MAIN);
    lv_obj_set_style_border_width(div1, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(div1, 0, LV_PART_MAIN);

    // --- Info line ---
    lora_info_lbl = lv_label_create(lora_root);
    {
        char buf[48];
        snprintf(buf, sizeof(buf), "Freq: %.3f MHz  SF12  BW125", lora_freqs[lora_freq_idx]);
        lv_label_set_text(lora_info_lbl, buf);
    }
    lv_obj_set_style_text_color(lora_info_lbl, um_col_text_hint(), LV_PART_MAIN);
    lv_obj_set_width(lora_info_lbl, lv_pct(100));

    // --- Divider ---
    lv_obj_t *div2 = lv_obj_create(lora_root);
    lv_obj_set_size(div2, lv_pct(100), 1);
    lv_obj_set_style_bg_color(div2, um_col_divider(), LV_PART_MAIN);
    lv_obj_set_style_border_width(div2, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(div2, 0, LV_PART_MAIN);

    // --- Scrollable message log (grows to fill available space) ---
    lora_log_cont = lv_obj_create(lora_root);
    lv_obj_set_width(lora_log_cont, lv_pct(100));
    lv_obj_set_flex_grow(lora_log_cont, 1);
    lv_obj_set_style_bg_opa(lora_log_cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(lora_log_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(lora_log_cont, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(lora_log_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(lora_log_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(lora_log_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(lora_log_cont, LV_DIR_VER);
    lv_obj_set_style_bg_color(lora_log_cont, um_col_scrollbar(), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(lora_log_cont, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(lora_log_cont, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(lora_log_cont, 2, LV_PART_SCROLLBAR);


    // --- UI update timer ---
    lora_timer = lv_timer_create(lora_timer_cb, LORA_UI_TIMER_MS, NULL);
    lv_timer_ready(lora_timer);

    if (lora_logCount > 0) lora_log_dirty = true;
}

void um_lora_destroy()
{
    if (!lora_root) return;

    if (lora_timer)     { lv_timer_del(lora_timer);     lora_timer     = NULL; }
    if (lora_bsp_timer) { lv_timer_del(lora_bsp_timer); lora_bsp_timer = NULL; }
    if (lora_popup_cont) { lv_obj_del(lora_popup_cont); lora_popup_cont = NULL; }

    lv_group_t *g = lv_group_get_default();
    if (g) lv_group_remove_all_objs(g);

    lv_obj_del(lora_root);
    lora_root       = NULL;
    lora_status_lbl = NULL;
    lora_info_lbl   = NULL;
    lora_log_cont   = NULL;
}
