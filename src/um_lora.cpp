
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <lvgl.h>
#include <esp_mac.h>
#include "um_nav.h"
#include "um_theme.h"
#include "config.h"
#include "UniversalMesh.h"          // MeshPacket struct + packet type constants
#include "radio/hal_interface.h"
#include "radio/hw_lr1121.h"

#ifndef SIM_BUILD
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#endif

// -------------------------------------------------------
// LoRa frequencies to scan (EU, US, Asia, 2.4GHz)
// -------------------------------------------------------
static const float lora_freqs[] = {
    868.0, 868.3, 868.8,
    902.0, 903.0, 904.6,
    915.0, 920.0, 923.0, 928.0,
    950.0, 960.0,
    2400.0, 2425.0, 2450.0, 2475.0, 2483.5
};
static const int LORA_FREQ_COUNT = (int)(sizeof(lora_freqs) / sizeof(lora_freqs[0]));
static int lora_freq_idx = 0;       // index of active/connected frequency

// -------------------------------------------------------
// Mesh state
// -------------------------------------------------------
enum LoraState { LORA_DISCOVERING, LORA_CONNECTED, LORA_NO_COORD };
static volatile LoraState lora_state   = LORA_DISCOVERING;
static uint8_t lora_my_mac[6]         = {};
static uint8_t lora_coord_mac[6]      = {};
static volatile bool lora_rescan_req  = false;

// -------------------------------------------------------
// Log buffer
// -------------------------------------------------------
#define LORA_LOG_ROWS 32
#define LORA_LOG_COL  80

static char    lora_log[LORA_LOG_ROWS][LORA_LOG_COL] = {};
static uint8_t lora_logHead   = 0;
static uint8_t lora_logCount  = 0;
static bool    lora_log_dirty = false;

#ifndef SIM_BUILD
static SemaphoreHandle_t lora_mutex = NULL;
static TaskHandle_t      lora_task  = NULL;
static bool              lora_started = false;
static volatile bool     lora_screen_active = false;
#endif

// -------------------------------------------------------
// LVGL state
// -------------------------------------------------------
static lv_obj_t   *lora_root       = NULL;
static lv_obj_t   *lora_status_lbl = NULL;
static lv_obj_t   *lora_info_lbl   = NULL;
static lv_obj_t   *lora_log_cont   = NULL;
static lv_obj_t   *lora_popup_cont = NULL;
static lv_timer_t *lora_timer      = NULL;
static lv_timer_t *lora_bsp_timer  = NULL;
static uint8_t     lora_dot_phase  = 0;

#define LORA_BSP_LONG_PRESS_MS  600
#define LORA_UI_TIMER_MS        250

// Timing
#define LORA_TX_AIRTIME_MS     1600UL   // conservative SF12/BW125 packet airtime
#define LORA_PONG_WAIT_MS      4000UL   // how long to listen for PONG per frequency
#define LORA_NO_COORD_WAIT_MS 10000UL   // pause before retry after full scan fails
#define LORA_HB_INTERVAL      UM_HB_INTERVAL
#define LORA_TEMP_INTERVAL    UM_TEMP_INTERVAL

// Minimum valid wire-format MeshPacket size (header only, no payload)
#define LORA_PKT_MIN  (1+1+4+6+6+1+1)  // type+ttl+msgId+dest+src+appId+payloadLen = 20

// Forward declarations
static void lora_popup_close();
static void lora_popup_open();
static void lora_key_bsp_cb(lv_event_t *e);
static void lora_rebuild_rows();

// -------------------------------------------------------
// Log push  (mutex-safe, callable from task or UI)
// -------------------------------------------------------
static void lora_log_push(const char *line)
{
#ifndef SIM_BUILD
    if (!lora_mutex) return;
    if (xSemaphoreTake(lora_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
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
// Radio helpers
// -------------------------------------------------------
static void lora_set_freq(int idx)
{
#ifndef SIM_BUILD
    radio_params_t p;
    p.freq      = lora_freqs[idx];
    p.bandwidth = 125.0f;
    p.sf        = 12;
    p.cr        = 5;
    p.syncWord  = 0xCD;
    p.power     = 22;
    p.mode      = RADIO_RX;
    hw_set_radio_params(p);
#endif
}

// Transmit a MeshPacket, then re-arm RX. Call only from the radio task.
static void lora_tx_packet(MeshPacket *pkt)
{
#ifndef SIM_BUILD
    size_t wire_len = LORA_PKT_MIN + pkt->payloadLen;
    if (wire_len > sizeof(MeshPacket)) wire_len = sizeof(MeshPacket);

    radio_tx_params_t tx;
    tx.data   = (uint8_t *)pkt;
    tx.length = wire_len;
    hw_set_radio_tx(tx, false);                       // starts TX async
    vTaskDelay(pdMS_TO_TICKS(LORA_TX_AIRTIME_MS));    // wait for airtime
    hw_set_radio_listening();                          // back to RX
#endif
}

// Build and transmit a PING (optionally with NODE_NAME payload)
static void lora_send_ping(const uint8_t dest[6], bool with_name = false)
{
#ifndef SIM_BUILD
    MeshPacket ping = {};
    ping.type    = MESH_TYPE_PING;
    ping.ttl     = 4;
    ping.msgId   = esp_random() % 1000000000u;
    memcpy(ping.destMac, dest, 6);
    memcpy(ping.srcMac,  lora_my_mac, 6);
    ping.appId   = 0x00;
    if (with_name) {
        uint8_t nlen = (uint8_t)strlen(NODE_NAME);
        ping.payloadLen = nlen > 200 ? 200 : nlen;
        memcpy(ping.payload, NODE_NAME, ping.payloadLen);
    } else {
        ping.payloadLen = 0;
    }
    lora_tx_packet(&ping);
#endif
}

// Send DATA packet to coordinator
static void lora_send_data(uint8_t appId, const uint8_t *payload, uint8_t len)
{
#ifndef SIM_BUILD
    if (lora_state != LORA_CONNECTED) return;
    MeshPacket pkt = {};
    pkt.type       = MESH_TYPE_DATA;
    pkt.ttl        = 4;
    pkt.msgId      = esp_random() % 1000000000u;
    memcpy(pkt.destMac, lora_coord_mac, 6);
    memcpy(pkt.srcMac,  lora_my_mac, 6);
    pkt.appId      = appId;
    pkt.payloadLen = len > 200 ? 200 : len;
    memcpy(pkt.payload, payload, pkt.payloadLen);
    lora_tx_packet(&pkt);
#endif
}

// -------------------------------------------------------
// Process a received MeshPacket (called from task, log-push safe)
// -------------------------------------------------------
static void lora_process_packet(const MeshPacket *pkt, int16_t rssi)
{
    char line[LORA_LOG_COL];

    switch (pkt->type) {

        case MESH_TYPE_PONG:
            if (lora_state == LORA_DISCOVERING) {
                memcpy(lora_coord_mac, pkt->srcMac, 6);
                lora_state = LORA_CONNECTED;
                snprintf(line, sizeof(line),
                         "[PONG] Coord %02X:%02X rssi=%d",
                         pkt->srcMac[4], pkt->srcMac[5], rssi);
                lora_log_push(line);
            }
            break;

        case MESH_TYPE_DATA: {
            uint8_t slen = pkt->payloadLen > 52 ? 52 : pkt->payloadLen;
            char snippet[53] = {};
            for (int i = 0; i < slen; i++) {
                uint8_t b = pkt->payload[i];
                snippet[i] = (b >= 0x20 && b < 0x7F) ? (char)b : '.';
            }
            snprintf(line, sizeof(line), "[DATA] %02X:%02X A%02X %s",
                     pkt->srcMac[4], pkt->srcMac[5], pkt->appId, snippet);
            lora_log_push(line);
            Serial.printf("[LoRa MESH] DATA %02X:%02X A%02X: %s\n",
                          pkt->srcMac[4], pkt->srcMac[5], pkt->appId, snippet);
            break;
        }

        case MESH_TYPE_PING:
            snprintf(line, sizeof(line), "[PING] %02X:%02X rssi=%d",
                     pkt->srcMac[4], pkt->srcMac[5], rssi);
            lora_log_push(line);
            break;

        case MESH_TYPE_ACK:
            snprintf(line, sizeof(line), "[ACK] %02X:%02X rssi=%d",
                     pkt->srcMac[4], pkt->srcMac[5], rssi);
            lora_log_push(line);
            break;

        default:
            snprintf(line, sizeof(line), "[0x%02X] %02X:%02X rssi=%d",
                     pkt->type, pkt->srcMac[4], pkt->srcMac[5], rssi);
            lora_log_push(line);
            break;
    }
}

// -------------------------------------------------------
// Frequency scanner — returns freq index where PONG was
// received, or -1 if none found. Scans all frequencies.
// -------------------------------------------------------
#ifndef SIM_BUILD
static int lora_scan_for_coordinator()
{
    static const uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    static uint8_t rx_buf[sizeof(MeshPacket) + 1];

    char line[LORA_LOG_COL];

    for (int i = 0; i < LORA_FREQ_COUNT; i++) {
        snprintf(line, sizeof(line), "Scanning %.3f MHz...", lora_freqs[i]);
        lora_log_push(line);

        lora_set_freq(i);
        hw_set_radio_listening();

        // Send PING broadcast on this frequency
        MeshPacket ping = {};
        ping.type       = MESH_TYPE_PING;
        ping.ttl        = 4;
        ping.msgId      = esp_random() % 1000000000u;
        memcpy(ping.destMac, broadcast, 6);
        memcpy(ping.srcMac,  lora_my_mac, 6);
        ping.appId      = 0x00;
        ping.payloadLen = (uint8_t)strlen(NODE_NAME);
        memcpy(ping.payload, NODE_NAME, ping.payloadLen);
        lora_tx_packet(&ping);  // TX + wait airtime + re-arm RX

        // Listen for PONG on this frequency
        unsigned long deadline = (unsigned long)(millis()) + LORA_PONG_WAIT_MS;
        while ((unsigned long)(millis()) < deadline) {
            radio_rx_params_t rx;
            rx.data   = rx_buf;
            rx.length = 0;
            hw_get_radio_rx(rx);

            if (rx.length >= LORA_PKT_MIN && rx.state == 0) {
                MeshPacket *pkt = (MeshPacket *)rx_buf;
                if (pkt->type == MESH_TYPE_PONG) {
                    snprintf(line, sizeof(line),
                             "[PONG] Coord found on %.3f MHz  %02X:%02X",
                             lora_freqs[i], pkt->srcMac[4], pkt->srcMac[5]);
                    lora_log_push(line);
                    memcpy(lora_coord_mac, pkt->srcMac, 6);
                    return i;
                }
                // Log non-PONG packets seen during scan
                lora_process_packet(pkt, rx.rssi);
            }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
    return -1;  // not found
}
#endif

// -------------------------------------------------------
// Radio / mesh FreeRTOS task
// -------------------------------------------------------
#ifndef SIM_BUILD
static void lora_mesh_task(void *param)
{
    static uint8_t rx_buf[sizeof(MeshPacket) + 1];

    esp_efuse_mac_get_default(lora_my_mac);

    char mac_line[LORA_LOG_COL];
    snprintf(mac_line, sizeof(mac_line), "My MAC %02X:%02X:%02X:%02X:%02X:%02X",
             lora_my_mac[0], lora_my_mac[1], lora_my_mac[2],
             lora_my_mac[3], lora_my_mac[4], lora_my_mac[5]);
    lora_log_push(mac_line);
    lora_log_push("Scanning frequencies for coordinator...");

    unsigned long last_hb   = 0;
    unsigned long last_temp = 0;

    for (;;) {
        // Handle rescan request from UI options popup
        if (lora_rescan_req) {
            lora_rescan_req = false;
            lora_state      = LORA_DISCOVERING;
            memset(lora_coord_mac, 0, 6);
            lora_log_push("Rescanning...");
        }

        // -----------------------------------------------
        // DISCOVERING: scan all frequencies for coordinator
        // -----------------------------------------------
        if (lora_state == LORA_DISCOVERING) {
            int found_idx = lora_scan_for_coordinator();

            if (found_idx >= 0) {
                lora_freq_idx = found_idx;
                lora_state    = LORA_CONNECTED;

                // Announce ourselves on the found frequency
                lora_send_ping(lora_coord_mac, true);      // PING with node name
                uint8_t nm = (uint8_t)strlen(NODE_NAME);   // DATA appId 0x06 = node name
                lora_send_data(0x06, (const uint8_t *)NODE_NAME, nm);
                lora_log_push("Announced. Listening...");

                last_hb   = (unsigned long)(millis()) - LORA_HB_INTERVAL;
                last_temp = (unsigned long)(millis()) - LORA_TEMP_INTERVAL;

            } else {
                lora_state = LORA_NO_COORD;
                lora_log_push("No coordinator found. Waiting 10s...");
                vTaskDelay(pdMS_TO_TICKS(LORA_NO_COORD_WAIT_MS));
                lora_state = LORA_DISCOVERING;  // retry
            }
        }

        // -----------------------------------------------
        // CONNECTED: receive packets + send heartbeat/temp
        // -----------------------------------------------
        if (lora_state == LORA_CONNECTED) {
            unsigned long now = (unsigned long)(millis());

            // Heartbeat: appId 0x05 (ping) + 0x06 (node name)
            if (now - last_hb >= LORA_HB_INTERVAL) {
                last_hb = now;
                uint8_t hb = '1';
                lora_send_data(0x05, &hb, 1);
                lora_send_data(0x06, (const uint8_t *)NODE_NAME, (uint8_t)strlen(NODE_NAME));
                lora_log_push("[HB] Heartbeat sent");
            }

            // Temperature report: appId 0x01, JSON {"name":"...","temp":"23.5"}
            if (now - last_temp >= LORA_TEMP_INTERVAL) {
                last_temp = now;
                float tempC = temperatureRead();
                JsonDocument doc;
                doc["name"] = NODE_NAME;
                doc["temp"] = serialized(String(tempC, 1));
                String payload;
                serializeJson(doc, payload);
                lora_send_data(0x01, (const uint8_t *)payload.c_str(),
                               (uint8_t)payload.length());
                char tline[48];
                snprintf(tline, sizeof(tline), "[TX] Temp: %.1fC", tempC);
                lora_log_push(tline);
            }

            // Poll for incoming packet
            radio_rx_params_t rx;
            rx.data   = rx_buf;
            rx.length = 0;
            hw_get_radio_rx(rx);

            if (rx.length >= LORA_PKT_MIN && rx.state == 0) {
                if (rx.length <= sizeof(MeshPacket)) {
                    lora_process_packet((MeshPacket *)rx_buf, rx.rssi);
                } else {
                    // Oversized / raw packet
                    char line[LORA_LOG_COL];
                    snprintf(line, sizeof(line), "[RX] %dB rssi=%d", (int)rx.length, rx.rssi);
                    lora_log_push(line);
                }
            }
        }

        // Keep LoRa responsive while screen is visible; back off when hidden.
        vTaskDelay(pdMS_TO_TICKS(lora_screen_active ? 2 : 250));
    }
}
#endif

// -------------------------------------------------------
// UI timer  (display only — no radio I/O here)
// -------------------------------------------------------
static void lora_timer_cb(lv_timer_t *t)
{
    if (lora_log_dirty && lora_log_cont)
        lora_rebuild_rows();

    if (lora_status_lbl) {
        switch (lora_state) {
            case LORA_DISCOVERING: {
                static const char *dots[] = {".", "..", "..."};
                char buf[24];
                snprintf(buf, sizeof(buf), "Scanning%s", dots[lora_dot_phase % 3]);
                lora_dot_phase++;
                lv_label_set_text(lora_status_lbl, buf);
                lv_obj_set_style_text_color(lora_status_lbl, um_col_warn(), LV_PART_MAIN);
                break;
            }
            case LORA_CONNECTED: {
                char buf[20];
                snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                         lora_coord_mac[0], lora_coord_mac[1], lora_coord_mac[2],
                         lora_coord_mac[3], lora_coord_mac[4], lora_coord_mac[5]);
                lv_label_set_text(lora_status_lbl, buf);
                lv_obj_set_style_text_color(lora_status_lbl, um_col_ok(), LV_PART_MAIN);
                break;
            }
            case LORA_NO_COORD:
                lv_label_set_text(lora_status_lbl, "No coordinator");
                lv_obj_set_style_text_color(lora_status_lbl, um_col_err(), LV_PART_MAIN);
                break;
        }
    }

    if (lora_info_lbl) {
        if (lora_state == LORA_CONNECTED) {
            char buf[48];
            snprintf(buf, sizeof(buf), "Freq: %.3f MHz  SF12  BW125",
                     lora_freqs[lora_freq_idx]);
            lv_label_set_text(lora_info_lbl, buf);
            lv_obj_set_style_text_color(lora_info_lbl, um_col_text_dim(), LV_PART_MAIN);
        } else {
            char buf[48];
            snprintf(buf, sizeof(buf), "Scanning %.3f MHz...",
                     lora_freqs[lora_freq_idx < LORA_FREQ_COUNT ? lora_freq_idx : 0]);
            lv_label_set_text(lora_info_lbl, buf);
            lv_obj_set_style_text_color(lora_info_lbl, um_col_text_hint(), LV_PART_MAIN);
        }
    }
}

// -------------------------------------------------------
// Rebuild message rows
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
// Options popup  (frequency start + rescan)
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

    static char dd_opts[512];
    dd_opts[0] = '\0';
    for (int i = 0; i < LORA_FREQ_COUNT; i++) {
        char entry[24];
        snprintf(entry, sizeof(entry), "%.3f MHz", lora_freqs[i]);
        if (i > 0) strncat(dd_opts, "\n", sizeof(dd_opts) - strlen(dd_opts) - 1);
        strncat(dd_opts, entry, sizeof(dd_opts) - strlen(dd_opts) - 1);
    }

    lora_popup_cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(lora_popup_cont, 260, 178);
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

    // Start-frequency hint (scan will begin here)
    lv_obj_t *freq_lbl = lv_label_create(lora_popup_cont);
    lv_label_set_text(freq_lbl, "Scan start frequency:");
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
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // Button row: Rescan + Cancel
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

    lv_obj_t *scan_btn = lv_btn_create(btn_row);
    lv_obj_set_flex_grow(scan_btn, 1);
    lv_obj_set_style_bg_color(scan_btn, UM_COL(0,50,70, 200,225,242), LV_PART_MAIN);
    lv_obj_set_style_bg_color(scan_btn, um_col_focus_cyan(),
                              (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_border_color(scan_btn, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_border_width(scan_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(scan_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(scan_btn, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(scan_btn, [](lv_event_t *e) {
        lora_rescan_req = true;
        lora_popup_close();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *scan_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_lbl, LV_SYMBOL_REFRESH "  Rescan");
    lv_obj_set_style_text_color(scan_lbl, um_col_cyan_bright(), LV_PART_MAIN);
    lv_obj_center(scan_lbl);

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
        lv_group_add_obj(g, scan_btn);
        lv_group_add_obj(g, cancel_btn);
        lv_group_focus_obj(scan_btn);
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
    lora_screen_active = true;
#endif

    lora_dot_phase = 0;

#ifndef SIM_BUILD
    // Match mesh screen lifecycle: initialize/start once, then keep task alive.
    if (!lora_started) {
        lora_started = true;
        if (!lora_mutex) lora_mutex = xSemaphoreCreateMutex();
        hw_radio_begin();
        lora_state      = LORA_DISCOVERING;
        lora_rescan_req = false;
        memset(lora_coord_mac, 0, 6);
        lora_logHead   = 0;
        lora_logCount  = 0;
        lora_log_dirty = false;
    }
#else
    lora_state      = LORA_DISCOVERING;
    lora_rescan_req = false;
    memset(lora_coord_mac, 0, 6);
    lora_logHead   = 0;
    lora_logCount  = 0;
    lora_log_dirty = false;
#endif

    // --- Root container ---
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
    lv_label_set_text(hdr_title, LV_SYMBOL_WIFI " LoRa Mesh");
    lv_obj_set_style_text_color(hdr_title, um_col_orange(), LV_PART_MAIN);

    lora_status_lbl = lv_label_create(hdr);
    lv_label_set_text(lora_status_lbl, "Scanning...");
    lv_obj_set_style_text_color(lora_status_lbl, um_col_warn(), LV_PART_MAIN);

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
    lv_label_set_text(lora_info_lbl, "Scanning frequencies...");
    lv_obj_set_style_text_color(lora_info_lbl, um_col_text_hint(), LV_PART_MAIN);
    lv_obj_set_width(lora_info_lbl, lv_pct(100));

    // --- Divider ---
    lv_obj_t *div2 = lv_obj_create(lora_root);
    lv_obj_set_size(div2, lv_pct(100), 1);
    lv_obj_set_style_bg_color(div2, um_col_divider(), LV_PART_MAIN);
    lv_obj_set_style_border_width(div2, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(div2, 0, LV_PART_MAIN);

    // --- Scrollable message log ---
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

    // --- UI timer ---
    lora_timer = lv_timer_create(lora_timer_cb, LORA_UI_TIMER_MS, NULL);
    lv_timer_ready(lora_timer);

    // --- Start radio task (once) ---
#ifndef SIM_BUILD
    if (!lora_task) {
        xTaskCreate(lora_mesh_task, "lora_mesh", 6144, NULL, 4, &lora_task);
    }
#endif
}

void um_lora_destroy()
{
    if (!lora_root) return;

#ifndef SIM_BUILD
    lora_screen_active = false;
#endif

    if (lora_timer)      { lv_timer_del(lora_timer);      lora_timer      = NULL; }
    if (lora_bsp_timer)  { lv_timer_del(lora_bsp_timer);  lora_bsp_timer  = NULL; }
    if (lora_popup_cont) { lv_obj_del(lora_popup_cont);   lora_popup_cont = NULL; }

    lv_group_t *g = lv_group_get_default();
    if (g) lv_group_remove_all_objs(g);

    lv_obj_del(lora_root);
    lora_root       = NULL;
    lora_status_lbl = NULL;
    lora_info_lbl   = NULL;
    lora_log_cont   = NULL;

#ifndef SIM_BUILD
    // Keep LoRa task/radio alive so behavior matches mesh screen lifecycle.
#endif
}
