
#include <Arduino.h>

#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <lvgl.h>
#include "um_nav.h"
#include "um_theme.h"
#include "UniversalMesh.h" // For mesh integration
#include "radio/hal_interface.h"
#include "radio/hw_lr1121.h"

// --- LoRa/RadioLib/UniversalMesh integration ---
// These would be adapted to your project structure and needs
static radio_params_t lora_params; // Holds frequency, SF, etc.
static EventGroupHandle_t radioEvent = NULL;
#define LORA_ISR_FLAG _BV(0)

static lv_obj_t *lora_root = NULL;
static lv_obj_t *lora_popup = NULL;
static lv_obj_t *freq_label = NULL;
static float lora_freqs[] = { 433.775, 868.0, 915.0 };
static int lora_freq_idx = 1; // Default to 868.0

static void lora_radio_isr() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xEventGroupSetBitsFromISR(radioEvent, LORA_ISR_FLAG, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void lora_set_radio_params() {
    lora_params.freq = lora_freqs[lora_freq_idx];
    lora_params.bandwidth = 125.0;
    lora_params.sf = 12;
    lora_params.cr = 5;
    lora_params.syncWord = 0xCD;
    lora_params.power = 22;
    lora_params.mode = RADIO_RX;
    hw_set_radio_params(lora_params);
}

void lora_start_receive() {
    hw_set_radio_listening();
}

void lora_handle_rx() {
    radio_rx_params_t rx;
    rx.data = (uint8_t*)malloc(256);
    rx.length = 0;
    hw_get_radio_rx(rx);
    if (rx.length > 0 && rx.state == RADIOLIB_ERR_NONE) {
        // TODO: Pass to UniversalMesh if needed, or display
        Serial.printf("[LoRa RX] %d bytes: %s\n", rx.length, (char*)rx.data);
        // Example: show popup or update UI
    }
    free(rx.data);
}

// --- Popup for frequency selection ---
static void lora_popup_close() {
    if (!lora_popup) return;
    lv_obj_del(lora_popup);
    lora_popup = NULL;
}

static void lora_popup_open() {
    if (lora_popup) return;
    lora_popup = lv_obj_create(lv_scr_act());
    lv_obj_set_size(lora_popup, 260, 120);
    lv_obj_center(lora_popup);
    lv_obj_set_style_bg_color(lora_popup, um_col_surface(), LV_PART_MAIN);
    lv_obj_set_style_border_color(lora_popup, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_border_width(lora_popup, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(lora_popup, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(lora_popup, 24, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(lora_popup, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(lora_popup, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_pad_all(lora_popup, 10, LV_PART_MAIN);
    lv_obj_clear_flag(lora_popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(lora_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(lora_popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(lora_popup, 8, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(lora_popup);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS "  LoRa Settings");
    lv_obj_set_style_text_color(title, um_col_cyan_bright(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);

    // Frequency selection row
    lv_obj_t *freq_row = lv_obj_create(lora_popup);
    lv_obj_set_width(freq_row, lv_pct(100));
    lv_obj_set_height(freq_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(freq_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(freq_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(freq_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(freq_row, 8, LV_PART_MAIN);
    lv_obj_clear_flag(freq_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(freq_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(freq_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *freq_lbl = lv_label_create(freq_row);
    lv_label_set_text(freq_lbl, "Frequency:");
    freq_label = lv_label_create(freq_row);
    char freq_buf[16];
    snprintf(freq_buf, sizeof(freq_buf), "%.3f MHz", lora_freqs[lora_freq_idx]);
    lv_label_set_text(freq_label, freq_buf);

    // Frequency next button
    lv_obj_t *freq_next_btn = lv_btn_create(freq_row);
    lv_obj_set_size(freq_next_btn, 40, 32);
    lv_obj_t *freq_next_lbl = lv_label_create(freq_next_btn);
    lv_label_set_text(freq_next_lbl, LV_SYMBOL_RIGHT);
    lv_obj_center(freq_next_lbl);
    lv_obj_add_event_cb(freq_next_btn, [](lv_event_t *e) {
        lora_freq_idx = (lora_freq_idx + 1) % (sizeof(lora_freqs)/sizeof(lora_freqs[0]));
        char buf[16];
        snprintf(buf, sizeof(buf), "%.3f MHz", lora_freqs[lora_freq_idx]);
        lv_label_set_text(freq_label, buf);
        lora_set_radio_params();
    }, LV_EVENT_CLICKED, NULL);

    // Rescan button
    lv_obj_t *scan_btn = lv_btn_create(lora_popup);
    lv_obj_set_width(scan_btn, 100);
    lv_obj_t *scan_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_lbl, LV_SYMBOL_REFRESH "  Rescan");
    lv_obj_center(scan_lbl);
    lv_obj_add_event_cb(scan_btn, [](lv_event_t *e) {
        lora_set_radio_params();
        lora_start_receive();
    }, LV_EVENT_CLICKED, NULL);

    // Close button
    lv_obj_t *close_btn = lv_btn_create(lora_popup);
    lv_obj_set_width(close_btn, 100);
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE "  Close");
    lv_obj_center(close_lbl);
    lv_obj_add_event_cb(close_btn, [](lv_event_t *e) { lora_popup_close(); }, LV_EVENT_CLICKED, NULL);
}

static void lora_back_cb(lv_event_t *e) {
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) um_nav_back();
    // Long-press logic for popup (optional)
    // TODO: Add long-press detection for popup
}

void um_lora_create() {
    lora_root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(lora_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(lora_root, um_col_bg(), LV_PART_MAIN);
    lv_obj_set_style_border_width(lora_root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(lora_root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(lora_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(lora_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(lora_root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(lora_root, 12, LV_PART_MAIN);

    lv_obj_t *ico = lv_label_create(lora_root);
    lv_label_set_text(ico, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_40, LV_PART_MAIN);
    lv_obj_set_style_text_color(ico, um_col_orange(), LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(lora_root);
    lv_label_set_text(title, "LoRa");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, um_col_text(), LV_PART_MAIN);

    lv_obj_t *sub = lv_label_create(lora_root);
    lv_label_set_text(sub, "Receiving...");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(sub, um_col_text_inactive(), LV_PART_MAIN);

    // Back button
    lv_obj_t *back_btn = lv_btn_create(lora_root);
    lv_obj_set_width(back_btn, 160);
    lv_obj_set_style_bg_color(back_btn, um_col_surface(), LV_PART_MAIN);
    lv_obj_set_style_border_color(back_btn, um_col_border(), LV_PART_MAIN);
    lv_obj_set_style_border_width(back_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(back_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(back_btn, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) { um_nav_back(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(back_btn, lora_back_cb, LV_EVENT_KEY, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT "  Back");
    lv_obj_set_style_text_color(back_lbl, um_col_text_dim(), LV_PART_MAIN);
    lv_obj_center(back_lbl);

    // Popup button (hold back for popup, or add a dedicated button)
    lv_obj_t *popup_btn = lv_btn_create(lora_root);
    lv_obj_set_width(popup_btn, 160);
    lv_obj_t *popup_lbl = lv_label_create(popup_btn);
    lv_label_set_text(popup_lbl, LV_SYMBOL_SETTINGS "  Options");
    lv_obj_center(popup_lbl);
    lv_obj_add_event_cb(popup_btn, [](lv_event_t *e) { lora_popup_open(); }, LV_EVENT_CLICKED, NULL);

    lv_group_t *g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, back_btn);
        lv_group_add_obj(g, popup_btn);
        lv_group_focus_obj(back_btn);
    }

    // --- Radio/LoRa init ---
    radioEvent = xEventGroupCreate();
    instance.initLoRa();
    // instance.setPacketSentAction(lora_radio_isr); // Not needed, handled in hw_lr1121.cpp
    lora_set_radio_params();
    lora_start_receive();
}

void um_lora_destroy() {
    if (!lora_root) return;
    lv_group_t *g = lv_group_get_default();
    if (g) lv_group_remove_all_objs(g);
    lv_obj_del(lora_root);
    lora_root = NULL;
    lora_popup_close();
}
