
#include <Arduino.h>
#ifndef SIM_BUILD
#include <ArduinoJson.h>
#endif
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <ctype.h>
#include <lvgl.h>
#ifndef SIM_BUILD
#include <esp_mac.h>
#endif
#include "um_nav.h"
#include "um_theme.h"
#include "um_shared.h"
#include "config.h"
#include "UniversalMesh.h"          // MeshPacket struct + packet type constants
#include "helpers/um_haptic.h"
#include "helpers/um_storage.h"
#include "helpers/um_toast.h"
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
// SF options  (index → value, shown in UI and used in radio config)
// -------------------------------------------------------
static const int lora_sf_vals[] = { 7, 8, 9, 10, 11, 12 };
static const char * const lora_sf_names[] = {
    "SF7  - Fast, short range",
    "SF8  - Balanced fast",
    "SF9  - Balanced",
    "SF10 - Long range",
    "SF11 - Very long range",
    "SF12 - Max range, slow",
};
static const int LORA_SF_COUNT = (int)(sizeof(lora_sf_vals) / sizeof(lora_sf_vals[0]));
static int lora_sf_idx = 2;         // default: SF9

// -------------------------------------------------------
// TX power options  (index → dBm, shown in UI and used in radio config)
// -------------------------------------------------------
static const int lora_pwr_vals[] = { 2, 10, 14, 17, 20, 22 };
static const char * const lora_pwr_names[] = {
    " 2 dBm - Min",
    "10 dBm - Low",
    "14 dBm - Medium",
    "17 dBm - High",
    "20 dBm - Max",
    "22 dBm - Boost",
};
static const int LORA_PWR_COUNT = (int)(sizeof(lora_pwr_vals) / sizeof(lora_pwr_vals[0]));
static int lora_pwr_idx = 5;        // default: 22 dBm

// Worst-case TX airtime per SF (220-byte packet, BW125, CR4/5) + margin
static const uint32_t lora_tx_airtime_ms[] = {
     600,   // SF7  — ~350ms + margin
    1000,   // SF8  — ~640ms + margin
    1500,   // SF9  — ~1100ms + margin  ← default
    3000,   // SF10 — ~2100ms + margin
    5500,   // SF11 — ~4200ms + margin
   10000,   // SF12 — ~7900ms + margin
};
// used as: lora_tx_airtime_ms[lora_sf_idx]

#define LORA_AUTO_ANNOUNCE_INTERVAL_MS 120000UL
#define LORA_MSG_MAX_LEN 180

enum LoraStatusState {
    LORA_STATUS_LISTENING = 0,
    LORA_STATUS_TRANSMITTING,
    LORA_STATUS_RETUNING,
};

// -------------------------------------------------------
// State
// -------------------------------------------------------
static uint8_t lora_my_mac[6]             = {};
static volatile bool lora_test_req        = false;
static volatile bool lora_announce_req    = false;
static volatile bool lora_freq_change_req = false;
static volatile bool lora_msg_send_req    = false;
static volatile uint8_t lora_status_state = LORA_STATUS_LISTENING;
static volatile bool lora_announce_show_toast = false;
static volatile bool lora_service_running = false;
static volatile bool lora_screen_active   = false;
static bool lora_auto_announce_on_open    = true;
static uint32_t lora_auto_announce_ms     = 0;
static uint8_t lora_msg_app_id            = 0x01;
static char lora_msg_outbox[LORA_MSG_MAX_LEN + 1] = {};

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
#endif

// -------------------------------------------------------
// LVGL state
// -------------------------------------------------------
static lv_obj_t   *lora_root       = NULL;
static lv_obj_t   *lora_compose_btn = NULL;
static lv_obj_t   *lora_home_btn   = NULL;
static lv_obj_t   *lora_auto_lbl   = NULL;
static lv_obj_t   *lora_status_lbl = NULL;
static lv_obj_t   *lora_info_lbl   = NULL;
static lv_obj_t   *lora_log_cont   = NULL;
static lv_obj_t   *lora_popup_cont = NULL;
static lv_obj_t   *lora_compose_cont = NULL;
static lv_obj_t   *lora_compose_ta = NULL;
static lv_obj_t   *lora_compose_count_lbl = NULL;
static lv_obj_t   *lora_compose_app_dd = NULL;
static lv_timer_t *lora_timer      = NULL;
static lv_timer_t *lora_bsp_timer  = NULL;

#define LORA_BSP_LONG_PRESS_MS  600
#define LORA_UI_TIMER_MS        250

// Timing
//#define LORA_TX_AIRTIME_MS    1500UL   // SF9/BW125 worst-case ~0.5s + margin

// Minimum valid wire-format MeshPacket size (header only, no payload)
#define LORA_PKT_MIN  (1+1+4+6+6+1+1)  // type+ttl+msgId+dest+src+appId+payloadLen = 20

// Forward declarations
static void lora_popup_close();
static void lora_popup_open();
static void lora_compose_close();
static void lora_compose_open();
static void lora_compose_update_count();
static void lora_key_bsp_cb(lv_event_t *e);
static void lora_rebuild_rows();
static void lora_log_push(const char *line);
static void lora_process_json_message(const MeshPacket *pkt, const char *payload, size_t plen);
static void lora_process_command(const MeshPacket *pkt, const char *payload, size_t plen);
static void lora_send_text_reply(const uint8_t *destMac, uint8_t appId, const char *text);
static void lora_send_info_reply(const uint8_t *destMac, uint8_t appId);

bool um_lora_background_active()
{
    return lora_service_running;
}

void lora_queue_message(const char *msg, uint8_t appId)
{
    if (!msg) return;

    char sanitized[LORA_MSG_MAX_LEN + 1] = {};
    size_t sanitized_len = 0;
    for (size_t i = 0; msg[i] != '\0' && sanitized_len < LORA_MSG_MAX_LEN; i++) {
        uint8_t b = (uint8_t)msg[i];
        if (b == '\r' || b == '\n') b = ' ';
        if (b < 0x20 || b >= 0x7F) continue;
        sanitized[sanitized_len++] = (char)b;
    }

    size_t start = 0;
    while (start < sanitized_len && sanitized[start] == ' ') start++;

    while (sanitized_len > start && sanitized[sanitized_len - 1] == ' ')
        sanitized_len--;

    if (start > 0 && sanitized_len > start)
        memmove(sanitized, sanitized + start, sanitized_len - start);

    sanitized_len -= start;
    sanitized[sanitized_len] = '\0';
    if (sanitized_len == 0) return;

#ifndef SIM_BUILD
    String payload;
    while (sanitized_len > 0) {
        sanitized[sanitized_len] = '\0';
        JsonDocument doc;
        doc["name"] = NODE_NAME;
        doc["msg"] = sanitized;
        payload = "";
        serializeJson(doc, payload);
        if (payload.length() <= LORA_MSG_MAX_LEN) break;
        sanitized_len--;
    }

    if (payload.isEmpty()) return;

    strncpy(lora_msg_outbox, payload.c_str(), sizeof(lora_msg_outbox) - 1);
    lora_msg_outbox[sizeof(lora_msg_outbox) - 1] = '\0';
    lora_msg_app_id = appId;
    lora_msg_send_req = true;
    lora_log_push("[TX] Message queued");
#endif // !SIM_BUILD
}

static void lora_compose_update_count()
{
    if (!lora_compose_ta || !lora_compose_count_lbl) return;
    size_t len = strlen(lv_textarea_get_text(lora_compose_ta));
    char buf[24];
    snprintf(buf, sizeof(buf), "%u/%u", (unsigned)len, (unsigned)LORA_MSG_MAX_LEN);
    lv_label_set_text(lora_compose_count_lbl, buf);
}

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
    p.sf        = lora_sf_vals[lora_sf_idx];   // SF7–SF12, see lora_sf_names[]
    p.cr        = 5;
    p.syncWord  = 0xCD;                        // private network byte — not exposed in UI
    p.power     = lora_pwr_vals[lora_pwr_idx]; // dBm, see lora_pwr_names[]
    p.mode      = RADIO_RX;
    Serial.printf("[LoRa CFG] freq=%.3f BW=%.0f SF=%d CR=%d sync=0x%02X pwr=%d\n",
                  p.freq, p.bandwidth, p.sf, p.cr, p.syncWord, p.power);
    hw_set_radio_params(p);
#endif
}

// Transmit a MeshPacket, then re-arm RX. Call only from the radio task.
static void lora_tx_packet(MeshPacket *pkt)
{
#ifndef SIM_BUILD
    size_t wire_len = LORA_PKT_MIN + pkt->payloadLen;
    if (wire_len > sizeof(MeshPacket)) wire_len = sizeof(MeshPacket);

    lora_status_state = LORA_STATUS_TRANSMITTING;

    char dbg[LORA_LOG_COL];
    snprintf(dbg, sizeof(dbg), "[TX] startTransmit %dB type=0x%02X", (int)wire_len, pkt->type);
    lora_log_push(dbg);
    Serial.printf("[LoRa TX] %dB type=0x%02X freq=%.3f\n", (int)wire_len, pkt->type, lora_freqs[lora_freq_idx]);

    radio_tx_params_t tx;
    tx.data   = (uint8_t *)pkt;
    tx.length = wire_len;
    hw_set_radio_tx(tx, false);

    snprintf(dbg, sizeof(dbg), "[TX] state=%d waiting done", tx.state);
    lora_log_push(dbg);
    Serial.printf("[LoRa TX] state=%d\n", tx.state);

    bool done = hw_wait_tx_done(lora_tx_airtime_ms[lora_sf_idx]);
    snprintf(dbg, sizeof(dbg), "[TX] %s, listening", done ? "ISR done" : "timeout");
    lora_log_push(dbg);
    Serial.printf("[LoRa TX] %s, re-arming RX\n", done ? "ISR done" : "timeout");
    hw_set_radio_listening();
    lora_status_state = LORA_STATUS_LISTENING;
#endif
}

static void lora_send_text_reply(const uint8_t *destMac, uint8_t appId, const char *text)
{
#ifndef SIM_BUILD
    if (!destMac || !text || text[0] == '\0') return;

    MeshPacket pkt = {};
    pkt.type       = MESH_TYPE_DATA;
    pkt.ttl        = 4;
    pkt.msgId      = esp_random() % 1000000000u;
    memcpy(pkt.destMac, destMac, 6);
    memcpy(pkt.srcMac, lora_my_mac, 6);
    pkt.appId      = appId;
    pkt.payloadLen = (uint8_t)strnlen(text, sizeof(pkt.payload));
    memcpy(pkt.payload, text, pkt.payloadLen);
    lora_tx_packet(&pkt);
#endif
}

static void lora_send_info_reply(const uint8_t *destMac, uint8_t appId)
{
#ifndef SIM_BUILD
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             lora_my_mac[0], lora_my_mac[1], lora_my_mac[2],
             lora_my_mac[3], lora_my_mac[4], lora_my_mac[5]);

    JsonDocument doc;
    doc["n"]    = NODE_NAME;
    doc["mac"]  = macStr;
    doc["up"]   = (unsigned long)(millis() / 1000UL);
    doc["heap"] = ESP.getFreeHeap();
    doc["chip"] = ESP.getChipModel();
    doc["rev"]  = (int)ESP.getChipRevision();
    doc["freq"] = lora_freqs[lora_freq_idx];
    doc["sf"]   = lora_sf_vals[lora_sf_idx];
    doc["pwr"]  = lora_pwr_vals[lora_pwr_idx];

    String out;
    serializeJson(doc, out);
    lora_send_text_reply(destMac, appId, out.c_str());
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
            snprintf(line, sizeof(line), "[PONG] %02X:%02X rssi=%d",
                     pkt->srcMac[4], pkt->srcMac[5], rssi);
            lora_log_push(line);
            break;

        case MESH_TYPE_DATA: {
            size_t plen = pkt->payloadLen;
            if (plen > sizeof(pkt->payload)) plen = sizeof(pkt->payload);

            uint8_t slen = plen > 52 ? 52 : (uint8_t)plen;
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

            char payload[sizeof(pkt->payload) + 1] = {};
            memcpy(payload, pkt->payload, plen);
            lora_process_json_message(pkt, payload, plen);

            bool directToMe = (memcmp(pkt->destMac, lora_my_mac, 6) == 0);
            bool isCmd = (plen >= 4 && strncmp(payload, "cmd:", 4) == 0);
            bool isJson = (plen > 0 && payload[0] == '{');
            if (directToMe && isCmd) {
                lora_process_command(pkt, payload, plen);
                break;
            }

            if (directToMe && !isCmd && !isJson && payload[0] != '\0') {
                bool saved = um_storage_save_message(0, 0, payload, pkt->srcMac);
                if (saved) {
                    um_unread_count = um_unread_count + 1;
                    char toast_txt[80];
                    snprintf(toast_txt, sizeof(toast_txt), "Direct: %.60s", payload);
                    um_haptic_notify();
                    um_toast_show(LV_SYMBOL_ENVELOPE, toast_txt);
                }
                char dmlog[80];
                snprintf(dmlog, sizeof(dmlog), "[DM] %02X:%02X: %.40s",
                         pkt->srcMac[4], pkt->srcMac[5], payload);
                lora_log_push(dmlog);
            }
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

static void lora_process_command(const MeshPacket *pkt, const char *payload, size_t plen)
{
    if (!pkt || !payload || plen < 4 || strncmp(payload, "cmd:", 4) != 0) return;

    const char *command = payload + 4;
    if (command[0] == '\0') return;

    {
        char cmd_msg[128];
        snprintf(cmd_msg, sizeof(cmd_msg), "cmd:%s", command);
        um_storage_save_message(0, 0xFF, cmd_msg, pkt->srcMac);
    }

    char ack[220];
    snprintf(ack, sizeof(ack), "command received:%s", command);
    lora_send_text_reply(pkt->srcMac, pkt->appId, ack);

    if (strcmp(command, "info") == 0 || strcmp(command, "info:long") == 0) {
        lora_send_info_reply(pkt->srcMac, pkt->appId);
        lora_log_push("[CMD] info sent");
    } else if (strcmp(command, "reboot") == 0) {
        lora_log_push("[CMD] Rebooting...");
#ifndef SIM_BUILD
        delay(100);
        ESP.restart();
#endif
    } else {
        lora_log_push("[CMD] Unsupported");
    }
}

static void lora_process_json_message(const MeshPacket *pkt, const char *payload, size_t plen)
{
#ifndef SIM_BUILD
    if (!payload || plen == 0 || payload[0] != '{') return;

    JsonDocument jdoc;
    DeserializationError jerr = deserializeJson(jdoc, payload, plen);
    if (jerr) return;

    uint32_t ric  = jdoc["ric"]  | 0xFFFFFFFFu;
    uint8_t  func = jdoc["func"] | 0xFF;

    if (ric == UM_RIC_TIME_SYNC) {
        const char *msg = jdoc["msg"] | "";
        size_t msglen = strlen(msg);
        const char *data = (msglen == 26) ? msg + 14 : (msglen == 12) ? msg : nullptr;
        if (data) {
            auto d2 = [](const char *s) -> int { return (s[0] - '0') * 10 + (s[1] - '0'); };
            uint16_t yr  = 2000 + d2(data);
            uint8_t  mo  = d2(data + 2);
            uint8_t  day = d2(data + 4);
            uint8_t  hr  = d2(data + 6);
            uint8_t  mn  = d2(data + 8);
            uint8_t  sec = d2(data + 10);
#ifndef SIM_BUILD
            instance.rtc.setDateTime(yr, mo, day, hr, mn, sec);
#endif
            um_time_synced = true;
            char tslog[48];
            snprintf(tslog, sizeof(tslog),
                     "[TIME] Synced %04u-%02u-%02u %02u:%02u:%02u",
                     yr, mo, day, hr, mn, sec);
            lora_log_push(tslog);
        }
    }

    if (ric == UM_RIC_MSG_SERVER) {
        const char *msg = jdoc["msg"] | "";
        size_t mlen = strlen(msg);
        if (mlen > 0 && mlen <= UM_MSG_SERVER_MAX_LEN) {
            char stripped[UM_MSG_SERVER_NAME_LEN] = {};
            strncpy(stripped, msg, UM_MSG_SERVER_NAME_LEN - 1);
            size_t tail = strlen(stripped);
            while (tail > 0 && isdigit((unsigned char)stripped[tail - 1]))
                stripped[--tail] = '\0';
            for (size_t i = 0; i < tail; i++)
                stripped[i] = toupper((unsigned char)stripped[i]);
            strncpy(um_msg_server_name, stripped, UM_MSG_SERVER_NAME_LEN - 1);
            um_msg_server_name[UM_MSG_SERVER_NAME_LEN - 1] = '\0';
            char mslog[48];
            snprintf(mslog, sizeof(mslog), "[MSG-SRV] %s", um_msg_server_name);
            lora_log_push(mslog);
        }
    }

    const char *msg_field = jdoc["msg"] | "";
    static const uint32_t exclude[] = UM_MSG_EXCLUDE_RICS;
    bool excluded = false;
    for (size_t xi = 0; xi < UM_MSG_EXCLUDE_COUNT; xi++) {
        if (ric == exclude[xi]) { excluded = true; break; }
    }

    if (!excluded && msg_field[0] != '\0') {
        bool saved = um_storage_save_message(ric, func, msg_field, pkt->srcMac);
        if (saved && ric == UM_RIC_MY_PAGER) {
            um_unread_count = um_unread_count + 1;
            char toast_txt[80];
            snprintf(toast_txt, sizeof(toast_txt),
                     "New message · RIC %lu", (unsigned long)ric);
            um_haptic_notify();
            um_toast_show(LV_SYMBOL_ENVELOPE, toast_txt);
        }
    }
#endif // !SIM_BUILD
}

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

    lora_set_freq(lora_freq_idx);
    hw_set_radio_listening();
    lora_service_running = true;
    char start_line[LORA_LOG_COL];
    snprintf(start_line, sizeof(start_line), "Listening on %.3f MHz", lora_freqs[lora_freq_idx]);
    lora_log_push(start_line);

    for (;;) {
        if (lora_auto_announce_on_open && !lora_announce_req &&
            (uint32_t)(millis() - lora_auto_announce_ms) >= LORA_AUTO_ANNOUNCE_INTERVAL_MS) {
            lora_announce_req = true;
            lora_announce_show_toast = false;
            lora_auto_announce_ms = millis();
            lora_log_push("[TX] Auto announce queued");
        }

        // Frequency change from popup
        if (lora_freq_change_req) {
            lora_freq_change_req = false;
            lora_status_state = LORA_STATUS_RETUNING;
            lora_set_freq(lora_freq_idx);
            hw_set_radio_listening();
            lora_status_state = LORA_STATUS_LISTENING;
            char line[LORA_LOG_COL];
            snprintf(line, sizeof(line), "Listening on %.3f MHz", lora_freqs[lora_freq_idx]);
            lora_log_push(line);
        }

        // Announce on open — broadcast node name when LoRa page is opened
        if (lora_announce_req) {
            lora_announce_req = false;
            static const uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
            MeshPacket pkt = {};
            pkt.type       = MESH_TYPE_DATA;
            pkt.ttl        = 4;
            pkt.msgId      = esp_random() % 1000000000u;
            memcpy(pkt.destMac, broadcast, 6);
            memcpy(pkt.srcMac,  lora_my_mac, 6);
            pkt.appId      = 0x06;
            const char *msg = NODE_NAME;
            pkt.payloadLen = (uint8_t)strlen(msg);
            memcpy(pkt.payload, msg, pkt.payloadLen);
            lora_tx_packet(&pkt);
            char line[LORA_LOG_COL];
            snprintf(line, sizeof(line), "[TX] Announce on %.3f MHz", lora_freqs[lora_freq_idx]);
            lora_log_push(line);
            if (lora_announce_show_toast) {
                um_toast_show(LV_SYMBOL_UPLOAD, "LoRa announce sent");
                lora_announce_show_toast = false;
            }
        }

        if (lora_msg_send_req) {
            lora_msg_send_req = false;
            static const uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
            MeshPacket pkt = {};
            pkt.type       = MESH_TYPE_DATA;
            pkt.ttl        = 4;
            pkt.msgId      = esp_random() % 1000000000u;
            memcpy(pkt.destMac, broadcast, 6);
            memcpy(pkt.srcMac,  lora_my_mac, 6);
            pkt.appId      = lora_msg_app_id;
            pkt.payloadLen = (uint8_t)strnlen(lora_msg_outbox, sizeof(lora_msg_outbox) - 1);
            memcpy(pkt.payload, lora_msg_outbox, pkt.payloadLen);
            lora_tx_packet(&pkt);
            char line[LORA_LOG_COL];
            snprintf(line, sizeof(line), "[TX] A%02X message on %.3f MHz", pkt.appId, lora_freqs[lora_freq_idx]);
            lora_log_push(line);
            um_toast_show(LV_SYMBOL_ENVELOPE, "LoRa message sent");
        }

        // Test message — broadcast on current frequency, lora_tx_packet re-arms RX
        if (lora_test_req) {
            lora_test_req = false;
#ifndef SIM_BUILD
            static const uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
            MeshPacket pkt = {};
            pkt.type       = MESH_TYPE_DATA;
            pkt.ttl        = 4;
            pkt.msgId      = esp_random() % 1000000000u;
            memcpy(pkt.destMac, broadcast, 6);
            memcpy(pkt.srcMac,  lora_my_mac, 6);
            pkt.appId      = 0x01; //0x7F;
            JsonDocument doc;
            doc["name"] = NODE_NAME;
            doc["msg"] = "Test from " NODE_NAME;
            String payload;
            serializeJson(doc, payload);
            pkt.payloadLen = (uint8_t)payload.length();
            memcpy(pkt.payload, payload.c_str(), pkt.payloadLen);
            lora_tx_packet(&pkt);
            char line[LORA_LOG_COL];
            snprintf(line, sizeof(line), "[TX] Testmessage on %.3f MHz", lora_freqs[lora_freq_idx]);
            lora_log_push(line);
            um_toast_show(LV_SYMBOL_UPLOAD, "LoRa test sent");
#endif // !SIM_BUILD
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
                char line[LORA_LOG_COL];
                snprintf(line, sizeof(line), "[RX] %dB rssi=%d", (int)rx.length, rx.rssi);
                lora_log_push(line);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(2));
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
        if (lora_status_state == LORA_STATUS_TRANSMITTING) {
            lv_label_set_text(lora_status_lbl, "Transmitting");
            lv_obj_set_style_text_color(lora_status_lbl, um_col_err(), LV_PART_MAIN);
        } else if (lora_status_state == LORA_STATUS_RETUNING) {
            lv_label_set_text(lora_status_lbl, "Re-tuning");
            lv_obj_set_style_text_color(lora_status_lbl, um_col_warn(), LV_PART_MAIN);
        } else {
            lv_label_set_text(lora_status_lbl, "Listening");
            lv_obj_set_style_text_color(lora_status_lbl, um_col_ok(), LV_PART_MAIN);
        }
    }

    if (lora_auto_lbl) {
        if (lora_auto_announce_on_open) {
            lv_label_set_text(lora_auto_lbl, LV_SYMBOL_UPLOAD);
            lv_obj_set_style_text_color(lora_auto_lbl, um_col_warn(), LV_PART_MAIN);
        } else {
            lv_label_set_text(lora_auto_lbl, "");
        }
    }

    if (lora_info_lbl) {
        char buf[48];
        snprintf(buf, sizeof(buf), "%.3f MHz  SF%d  BW125  %d dBm",
                 lora_freqs[lora_freq_idx],
                 lora_sf_vals[lora_sf_idx],
                 lora_pwr_vals[lora_pwr_idx]);
        lv_label_set_text(lora_info_lbl, buf);
        lv_obj_set_style_text_color(lora_info_lbl, um_col_text_dim(), LV_PART_MAIN);
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

    // Only steal focus to the log if the popup is not open
    if (!lora_popup_cont && !lora_compose_cont && g && lv_obj_get_child_count(lora_log_cont) > 0)
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

static void lora_compose_close()
{
    if (!lora_compose_cont) return;
    lv_group_t *g = lv_group_get_default();
    lv_obj_del(lora_compose_cont);
    lora_compose_cont = NULL;
    lora_compose_ta = NULL;
    lora_compose_count_lbl = NULL;
    lora_compose_app_dd = NULL;
    if (g && lora_compose_btn)
        lv_group_focus_obj(lora_compose_btn);
}

static void lora_compose_submit()
{
    if (!lora_compose_ta) return;
    const char *msg = lv_textarea_get_text(lora_compose_ta);
    if (!msg || !msg[0]) {
        lora_log_push("[TX] Empty message ignored");
        return;
    }
    static const uint8_t app_ids[] = { 0x01, 0x05, 0x06, 0x7F };
    uint16_t idx = lora_compose_app_dd ? lv_dropdown_get_selected(lora_compose_app_dd) : 0;
    if (idx >= (sizeof(app_ids) / sizeof(app_ids[0]))) idx = 0;
    lora_queue_message(msg, app_ids[idx]);
    lora_compose_close();
}

// Default radio settings — SF9, 22 dBm, freq index 0
#define LORA_DEFAULT_SF_IDX  2   // SF9  — Balanced
#define LORA_DEFAULT_PWR_IDX 5   // 22 dBm — Boost
#define LORA_DEFAULT_FREQ_IDX 0  // 868.000 MHz

static void lora_popup_open()
{
    if (lora_popup_cont) return;

    // --- Frequency dropdown options ---
    static char freq_opts[512];
    freq_opts[0] = '\0';
    for (int i = 0; i < LORA_FREQ_COUNT; i++) {
        char entry[24];
        snprintf(entry, sizeof(entry), "%.3f MHz", lora_freqs[i]);
        if (i > 0) strncat(freq_opts, "\n", sizeof(freq_opts) - strlen(freq_opts) - 1);
        strncat(freq_opts, entry, sizeof(freq_opts) - strlen(freq_opts) - 1);
    }

    // --- SF dropdown options (from lora_sf_names[]) ---
    static char sf_opts[256];
    sf_opts[0] = '\0';
    for (int i = 0; i < LORA_SF_COUNT; i++) {
        if (i > 0) strncat(sf_opts, "\n", sizeof(sf_opts) - strlen(sf_opts) - 1);
        strncat(sf_opts, lora_sf_names[i], sizeof(sf_opts) - strlen(sf_opts) - 1);
    }

    // --- Power dropdown options (from lora_pwr_names[]) ---
    static char pwr_opts[256];
    pwr_opts[0] = '\0';
    for (int i = 0; i < LORA_PWR_COUNT; i++) {
        if (i > 0) strncat(pwr_opts, "\n", sizeof(pwr_opts) - strlen(pwr_opts) - 1);
        strncat(pwr_opts, lora_pwr_names[i], sizeof(pwr_opts) - strlen(pwr_opts) - 1);
    }

    lora_popup_cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(lora_popup_cont, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(lora_popup_cont, 0, 0);
    lv_obj_set_style_bg_color(lora_popup_cont, um_col_surface(), LV_PART_MAIN);
    lv_obj_set_style_border_color(lora_popup_cont, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_border_width(lora_popup_cont, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(lora_popup_cont, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(lora_popup_cont, 24, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(lora_popup_cont, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(lora_popup_cont, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_pad_all(lora_popup_cont, 10, LV_PART_MAIN);
    lv_obj_add_flag(lora_popup_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(lora_popup_cont, LV_DIR_VER);
    lv_obj_set_flex_flow(lora_popup_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(lora_popup_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(lora_popup_cont, 6, LV_PART_MAIN);

    // Title — decorative only, not in focus group
    lv_obj_t *title = lv_label_create(lora_popup_cont);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS "  LoRa Options");
    lv_obj_set_style_text_color(title, um_col_cyan_bright(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);

    // --- Frequency ---
    lv_obj_t *freq_lbl = lv_label_create(lora_popup_cont);
    lv_label_set_text(freq_lbl, "Listen frequency:");
    lv_obj_set_style_text_color(freq_lbl, um_col_text_dim(), LV_PART_MAIN);
    lv_obj_set_width(freq_lbl, lv_pct(100));

    lv_obj_t *freq_dd = lv_dropdown_create(lora_popup_cont);
    lv_obj_set_width(freq_dd, lv_pct(100));
    lv_dropdown_set_options(freq_dd, freq_opts);
    lv_dropdown_set_selected(freq_dd, (uint16_t)lora_freq_idx);
    lv_obj_set_style_bg_color(freq_dd, um_col_surface(), LV_PART_MAIN);
    lv_obj_set_style_text_color(freq_dd, um_col_orange(), LV_PART_MAIN);
    lv_obj_set_style_border_color(freq_dd, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_border_width(freq_dd, 1, LV_PART_MAIN);
    // When freq_dd (first item) gets focus, scroll to top so title is visible
    lv_obj_add_event_cb(freq_dd, [](lv_event_t *e) {
        lv_obj_scroll_to_y(lora_popup_cont, 0, LV_ANIM_ON);
    }, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(freq_dd, [](lv_event_t *e) {
        lora_freq_idx = (int)lv_dropdown_get_selected((lv_obj_t *)lv_event_get_target(e));
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // --- Spreading Factor ---
    lv_obj_t *sf_lbl = lv_label_create(lora_popup_cont);
    lv_label_set_text(sf_lbl, "Spreading factor:");
    lv_obj_set_style_text_color(sf_lbl, um_col_text_dim(), LV_PART_MAIN);
    lv_obj_set_width(sf_lbl, lv_pct(100));

    lv_obj_t *sf_dd = lv_dropdown_create(lora_popup_cont);
    lv_obj_set_width(sf_dd, lv_pct(100));
    lv_dropdown_set_options(sf_dd, sf_opts);
    lv_dropdown_set_selected(sf_dd, (uint16_t)lora_sf_idx);
    lv_obj_set_style_bg_color(sf_dd, um_col_surface(), LV_PART_MAIN);
    lv_obj_set_style_text_color(sf_dd, um_col_orange(), LV_PART_MAIN);
    lv_obj_set_style_border_color(sf_dd, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_border_width(sf_dd, 1, LV_PART_MAIN);
    lv_obj_add_flag(sf_dd, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(sf_dd, [](lv_event_t *e) {
        lora_sf_idx = (int)lv_dropdown_get_selected((lv_obj_t *)lv_event_get_target(e));
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // --- TX Power ---
    lv_obj_t *pwr_lbl = lv_label_create(lora_popup_cont);
    lv_label_set_text(pwr_lbl, "TX power:");
    lv_obj_set_style_text_color(pwr_lbl, um_col_text_dim(), LV_PART_MAIN);
    lv_obj_set_width(pwr_lbl, lv_pct(100));

    lv_obj_t *pwr_dd = lv_dropdown_create(lora_popup_cont);
    lv_obj_set_width(pwr_dd, lv_pct(100));
    lv_dropdown_set_options(pwr_dd, pwr_opts);
    lv_dropdown_set_selected(pwr_dd, (uint16_t)lora_pwr_idx);
    lv_obj_set_style_bg_color(pwr_dd, um_col_surface(), LV_PART_MAIN);
    lv_obj_set_style_text_color(pwr_dd, um_col_orange(), LV_PART_MAIN);
    lv_obj_set_style_border_color(pwr_dd, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_border_width(pwr_dd, 1, LV_PART_MAIN);
    lv_obj_add_flag(pwr_dd, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(pwr_dd, [](lv_event_t *e) {
        lora_pwr_idx = (int)lv_dropdown_get_selected((lv_obj_t *)lv_event_get_target(e));
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // --- Auto announce ---
    lv_obj_t *auto_row = lv_obj_create(lora_popup_cont);
    lv_obj_set_width(auto_row, lv_pct(100));
    lv_obj_set_height(auto_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(auto_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(auto_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(auto_row, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_column(auto_row, 8, LV_PART_MAIN);
    lv_obj_clear_flag(auto_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(auto_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(auto_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *auto_lbl = lv_label_create(auto_row);
    lv_label_set_text(auto_lbl, "Auto announce (after open - 120sec)");
    lv_obj_set_style_text_color(auto_lbl, um_col_text_dim(), LV_PART_MAIN);
    lv_obj_set_flex_grow(auto_lbl, 1);

    lv_obj_t *auto_sw = lv_switch_create(auto_row);
    if (lora_auto_announce_on_open) lv_obj_add_state(auto_sw, LV_STATE_CHECKED);
    lv_obj_add_flag(auto_sw, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_bg_color(auto_sw, um_col_divider(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(auto_sw, um_col_cyan(),
                              (lv_style_selector_t)((int)LV_STATE_CHECKED | (int)LV_PART_MAIN));
    lv_obj_set_style_bg_color(auto_sw, um_col_text_hint(), LV_PART_KNOB);
    lv_obj_set_style_bg_color(auto_sw, um_col_text(),
                              (lv_style_selector_t)((int)LV_STATE_CHECKED | (int)LV_PART_KNOB));
    lv_obj_add_event_cb(auto_sw, [](lv_event_t *e) {
        lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
        lora_auto_announce_on_open = lv_obj_has_state(sw, LV_STATE_CHECKED);
        lora_auto_announce_ms = millis();
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // --- Send Test button ---
    lv_obj_t *test_btn = lv_btn_create(lora_popup_cont);
    lv_obj_set_width(test_btn, lv_pct(100));
    lv_obj_set_style_bg_color(test_btn, UM_COL(0,50,20, 200,242,215), LV_PART_MAIN);
    lv_obj_set_style_bg_color(test_btn, um_col_focus_cyan(),
                              (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_border_color(test_btn, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_border_width(test_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(test_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(test_btn, 6, LV_PART_MAIN);
    lv_obj_add_flag(test_btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(test_btn, [](lv_event_t *e) {
        lora_test_req = true;
        lora_popup_close();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *test_lbl = lv_label_create(test_btn);
    lv_label_set_text(test_lbl, LV_SYMBOL_UPLOAD "  Send Test Message");
    lv_obj_set_style_text_color(test_lbl, um_col_ok(), LV_PART_MAIN);
    lv_obj_center(test_lbl);

    // --- Send Announce button ---
    lv_obj_t *ann_btn = lv_btn_create(lora_popup_cont);
    lv_obj_set_width(ann_btn, lv_pct(100));
    lv_obj_set_style_bg_color(ann_btn, UM_COL(0,30,50, 200,220,242), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ann_btn, um_col_focus_cyan(),
                              (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_border_color(ann_btn, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_border_width(ann_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(ann_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(ann_btn, 6, LV_PART_MAIN);
    lv_obj_add_flag(ann_btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(ann_btn, [](lv_event_t *e) {
        lora_announce_req = true;
        lora_announce_show_toast = true;
        lora_popup_close();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ann_lbl = lv_label_create(ann_btn);
    lv_label_set_text(ann_lbl, LV_SYMBOL_WIFI "  Send Announce");
    lv_obj_set_style_text_color(ann_lbl, um_col_cyan_bright(), LV_PART_MAIN);
    lv_obj_center(ann_lbl);

    // --- Button row: Apply + Defaults + Cancel ---
    lv_obj_t *btn_row = lv_obj_create(lora_popup_cont);
    lv_obj_set_width(btn_row, lv_pct(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(btn_row, 6, LV_PART_MAIN);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Apply
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
        lora_freq_change_req = true;
        lora_popup_close();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *apply_lbl = lv_label_create(apply_btn);
    lv_label_set_text(apply_lbl, LV_SYMBOL_OK "  Apply");
    lv_obj_set_style_text_color(apply_lbl, um_col_cyan_bright(), LV_PART_MAIN);
    lv_obj_center(apply_lbl);

    // Defaults  (SF9 / 22 dBm / freq 0 — see LORA_DEFAULT_* defines)
    lv_obj_t *def_btn = lv_btn_create(btn_row);
    lv_obj_set_flex_grow(def_btn, 1);
    lv_obj_set_style_bg_color(def_btn, UM_COL(50,40,0, 242,230,190), LV_PART_MAIN);
    lv_obj_set_style_bg_color(def_btn, um_col_focus_cyan(),
                              (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_border_color(def_btn, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_border_width(def_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(def_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(def_btn, 6, LV_PART_MAIN);
    // capture dropdowns so we can reset them visually too
    struct DefCtx { lv_obj_t *freq_dd; lv_obj_t *sf_dd; lv_obj_t *pwr_dd; };
    static DefCtx def_ctx;
    def_ctx = { freq_dd, sf_dd, pwr_dd };
    lv_obj_set_user_data(def_btn, &def_ctx);
    lv_obj_add_event_cb(def_btn, [](lv_event_t *e) {
        DefCtx *ctx = (DefCtx *)lv_obj_get_user_data((lv_obj_t *)lv_event_get_target(e));
        lora_freq_idx = LORA_DEFAULT_FREQ_IDX;  // 868.000 MHz
        lora_sf_idx   = LORA_DEFAULT_SF_IDX;    // SF9  — Balanced
        lora_pwr_idx  = LORA_DEFAULT_PWR_IDX;   // 22 dBm — Boost
        lv_dropdown_set_selected(ctx->freq_dd, (uint16_t)lora_freq_idx);
        lv_dropdown_set_selected(ctx->sf_dd,   (uint16_t)lora_sf_idx);
        lv_dropdown_set_selected(ctx->pwr_dd,  (uint16_t)lora_pwr_idx);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *def_lbl = lv_label_create(def_btn);
    lv_label_set_text(def_lbl, LV_SYMBOL_REFRESH "  Defaults");
    lv_obj_set_style_text_color(def_lbl, um_col_warn(), LV_PART_MAIN);
    lv_obj_center(def_lbl);

    // Cancel
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

    lv_obj_scroll_to_y(lora_popup_cont, 0, LV_ANIM_OFF);

    lv_group_t *g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, freq_dd);
        lv_group_add_obj(g, sf_dd);
        lv_group_add_obj(g, pwr_dd);
        lv_group_add_obj(g, auto_sw);
        lv_group_add_obj(g, test_btn);
        lv_group_add_obj(g, ann_btn);
        lv_group_add_obj(g, apply_btn);
        lv_group_add_obj(g, def_btn);
        lv_group_add_obj(g, cancel_btn);
        lv_group_focus_obj(freq_dd);
    }
}

static void lora_compose_open()
{
    if (lora_compose_cont) return;

    lora_compose_cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(lora_compose_cont, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(lora_compose_cont, 0, 0);
    lv_obj_set_style_bg_color(lora_compose_cont, um_col_surface(), LV_PART_MAIN);
    lv_obj_set_style_border_color(lora_compose_cont, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_border_width(lora_compose_cont, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(lora_compose_cont, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(lora_compose_cont, 24, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(lora_compose_cont, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(lora_compose_cont, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_pad_all(lora_compose_cont, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(lora_compose_cont, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(lora_compose_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(lora_compose_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(lora_compose_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(lora_compose_cont);
    lv_label_set_text(title, LV_SYMBOL_ENVELOPE "  Compose LoRa Message");
    lv_obj_set_style_text_color(title, um_col_ok(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);

    lv_obj_t *hint = lv_label_create(lora_compose_cont);
    lv_label_set_text(hint, "Type a short message. Use Send to transmit.");
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_color(hint, um_col_text_dim(), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, LV_PART_MAIN);

    lv_obj_t *app_row = lv_obj_create(lora_compose_cont);
    lv_obj_set_width(app_row, lv_pct(100));
    lv_obj_set_height(app_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(app_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(app_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(app_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(app_row, 4, LV_PART_MAIN);
    lv_obj_clear_flag(app_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(app_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(app_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *app_lbl = lv_label_create(app_row);
    lv_label_set_text(app_lbl, "App ID:   ");
    lv_obj_set_style_text_color(app_lbl, um_col_text_dim(), LV_PART_MAIN);
    lv_obj_set_style_text_font(app_lbl, &lv_font_montserrat_12, LV_PART_MAIN);

    lora_compose_app_dd = lv_dropdown_create(app_row);
    lv_obj_set_flex_grow(lora_compose_app_dd, 1);
    lv_dropdown_set_options(lora_compose_app_dd,
                            "A01 Text\n"
                            "A05 Status\n"
                            "A06 Announce\n"
                            "A7F Raw");
    lv_dropdown_set_selected(lora_compose_app_dd, 0);
    lv_obj_set_style_bg_color(lora_compose_app_dd, um_col_bg(), LV_PART_MAIN);
    lv_obj_set_style_text_color(lora_compose_app_dd, um_col_orange(), LV_PART_MAIN);
    lv_obj_set_style_border_color(lora_compose_app_dd, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_border_width(lora_compose_app_dd, 1, LV_PART_MAIN);
    lv_obj_add_flag(lora_compose_app_dd, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_text_font(lora_compose_app_dd, &lv_font_montserrat_12, LV_PART_MAIN);

    lora_compose_ta = lv_textarea_create(lora_compose_cont);
    lv_obj_set_width(lora_compose_ta, lv_pct(100));
    lv_obj_set_height(lora_compose_ta, 64);
    lv_textarea_set_placeholder_text(lora_compose_ta, "Message to broadcast over LoRa");
    lv_textarea_set_max_length(lora_compose_ta, LORA_MSG_MAX_LEN);
    lv_textarea_set_one_line(lora_compose_ta, false);
    lv_obj_add_state(lora_compose_ta, LV_STATE_EDITED);
    lv_obj_set_style_bg_color(lora_compose_ta, um_col_bg(), LV_PART_MAIN);
    lv_obj_set_style_text_color(lora_compose_ta, um_col_text(), LV_PART_MAIN);
    lv_obj_set_style_border_color(lora_compose_ta, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_border_width(lora_compose_ta, 1, LV_PART_MAIN);
    lv_obj_add_flag(lora_compose_ta, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(lora_compose_ta, [](lv_event_t *) {
        lora_compose_update_count();
    }, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(lora_compose_ta, [](lv_event_t *e) {
        lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
        uint32_t key = lv_event_get_key(e);
        bool editing = lv_obj_has_state(ta, LV_STATE_EDITED);
        lv_group_t *g = lv_group_get_default();

        if (key == LV_KEY_ENTER) {
            if (editing) {
                lv_obj_clear_state(ta, LV_STATE_EDITED);
                if (g) lv_group_set_editing(g, false);
                if (g) lv_group_focus_next(g);
            } else {
                lv_obj_add_state(ta, LV_STATE_EDITED);
                if (g) lv_group_set_editing(g, true);
            }
            lv_event_stop_processing(e);
        } else if ((key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) && editing) {
            lv_obj_clear_state(ta, LV_STATE_EDITED);
            if (g) lv_group_set_editing(g, false);
            lv_event_stop_processing(e);
        } else if (key == LV_KEY_ESC) {
            lora_compose_close();
        }
    }, LV_EVENT_KEY, NULL);

    lora_compose_count_lbl = lv_label_create(lora_compose_cont);
    lv_obj_set_width(lora_compose_count_lbl, lv_pct(100));
    lv_obj_set_style_text_align(lora_compose_count_lbl, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_color(lora_compose_count_lbl, um_col_text_hint(), LV_PART_MAIN);
    lv_obj_set_style_text_font(lora_compose_count_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lora_compose_update_count();

    lv_obj_t *btn_row = lv_obj_create(lora_compose_cont);
    lv_obj_set_width(btn_row, lv_pct(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(btn_row, 6, LV_PART_MAIN);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

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
    lv_obj_add_event_cb(send_btn, [](lv_event_t *) { lora_compose_submit(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *send_lbl = lv_label_create(send_btn);
    lv_label_set_text(send_lbl, LV_SYMBOL_UPLOAD "  Send");
    lv_obj_set_style_text_color(send_lbl, um_col_ok(), LV_PART_MAIN);
    lv_obj_center(send_lbl);

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
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t *) { lora_compose_close(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, LV_SYMBOL_CLOSE "  Cancel");
    lv_obj_set_style_text_color(cancel_lbl, UM_COL(180,100,100, 150,35,35), LV_PART_MAIN);
    lv_obj_center(cancel_lbl);

    lv_group_t *g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, lora_compose_app_dd);
        lv_group_add_obj(g, lora_compose_ta);
        lv_group_add_obj(g, send_btn);
        lv_group_add_obj(g, cancel_btn);
        lv_group_focus_obj(lora_compose_app_dd);
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
        else if (lora_compose_cont) lora_compose_close();
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
    bool first_start = false;
#ifndef SIM_BUILD
    if (!lora_mutex) lora_mutex = xSemaphoreCreateMutex();
    if (!lora_task) {
        hw_radio_begin();
        first_start = true;
        lora_service_running = true;
    }
#endif

    lora_screen_active    = true;
    lora_test_req        = false;
    lora_announce_req    = lora_auto_announce_on_open;
    lora_freq_change_req = false;
    lora_status_state    = LORA_STATUS_LISTENING;
    lora_announce_show_toast = false;
    lora_auto_announce_ms = millis();

    if (first_start) {
        lora_logHead  = 0;
        lora_logCount = 0;
        lora_log_dirty = false;
    }

    if (lora_auto_announce_on_open)
        lora_log_push("[TX] Auto announce queued");

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
    lv_label_set_text(lora_status_lbl, "Listening");
    lv_obj_set_style_text_color(lora_status_lbl, um_col_ok(), LV_PART_MAIN);

    lv_obj_t *hdr_actions = lv_obj_create(hdr);
    lv_obj_set_width(hdr_actions, LV_SIZE_CONTENT);
    lv_obj_set_height(hdr_actions, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(hdr_actions, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(hdr_actions, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(hdr_actions, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(hdr_actions, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(hdr_actions, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(hdr_actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr_actions, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(hdr_actions, LV_OBJ_FLAG_SCROLLABLE);

    lora_auto_lbl = lv_label_create(hdr_actions);
    lv_label_set_text(lora_auto_lbl, lora_auto_announce_on_open ? LV_SYMBOL_UPLOAD : "");
    lv_obj_set_style_text_color(lora_auto_lbl, um_col_warn(), LV_PART_MAIN);

    lora_compose_btn = lv_btn_create(hdr_actions);
    lv_obj_set_size(lora_compose_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(lora_compose_btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(lora_compose_btn, um_col_focus_cyan(),
                              (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_bg_opa(lora_compose_btn, LV_OPA_COVER,
                            (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_border_width(lora_compose_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(lora_compose_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(lora_compose_btn, 2, LV_PART_MAIN);
    lv_obj_add_event_cb(lora_compose_btn, [](lv_event_t *) { lora_compose_open(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(lora_compose_btn, lora_key_bsp_cb, LV_EVENT_KEY, NULL);
    lv_obj_t *compose_lbl = lv_label_create(lora_compose_btn);
    lv_label_set_text(compose_lbl, LV_SYMBOL_ENVELOPE);
    lv_obj_set_style_text_color(compose_lbl, um_col_ok(), LV_PART_MAIN);
    lv_obj_center(compose_lbl);

    lora_home_btn = lv_btn_create(hdr_actions);
    lv_obj_set_size(lora_home_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(lora_home_btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(lora_home_btn, um_col_focus_cyan(),
                              (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_bg_opa(lora_home_btn, LV_OPA_COVER,
                            (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_border_width(lora_home_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(lora_home_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(lora_home_btn, 2, LV_PART_MAIN);
    lv_obj_add_event_cb(lora_home_btn, [](lv_event_t *e) {
        um_haptic_select();
        um_nav_back();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(lora_home_btn, [](lv_event_t *e) {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_ENTER) um_haptic_select();
        lora_key_bsp_cb(e);
    }, LV_EVENT_KEY, NULL);
    lv_obj_t *back_lbl = lv_label_create(lora_home_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_HOME);
    lv_obj_set_style_text_color(back_lbl, um_col_orange(), LV_PART_MAIN);
    lv_obj_center(back_lbl);

    lv_group_t *g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, lora_home_btn);
        lv_group_add_obj(g, lora_compose_btn);
        lv_group_focus_obj(lora_home_btn);
    }

    // --- Divider ---
    lv_obj_t *div1 = lv_obj_create(lora_root);
    lv_obj_set_size(div1, lv_pct(100), 1);
    lv_obj_set_style_bg_color(div1, um_col_divider(), LV_PART_MAIN);
    lv_obj_set_style_border_width(div1, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(div1, 0, LV_PART_MAIN);

    // --- Info line ---
    lora_info_lbl = lv_label_create(lora_root);
    lv_label_set_text(lora_info_lbl, "Starting...");
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

    lora_screen_active = false;

    if (lora_timer)      { lv_timer_del(lora_timer);      lora_timer      = NULL; }
    if (lora_bsp_timer)  { lv_timer_del(lora_bsp_timer);  lora_bsp_timer  = NULL; }
    if (lora_popup_cont) { lv_obj_del(lora_popup_cont);   lora_popup_cont = NULL; }
    if (lora_compose_cont) { lv_obj_del(lora_compose_cont); lora_compose_cont = NULL; }

    lv_group_t *g = lv_group_get_default();
    if (g) lv_group_remove_all_objs(g);

    lv_obj_del(lora_root);
    lora_root       = NULL;
    lora_compose_btn = NULL;
    lora_home_btn   = NULL;
    lora_auto_lbl   = NULL;
    lora_status_lbl = NULL;
    lora_info_lbl   = NULL;
    lora_log_cont   = NULL;
    lora_compose_ta = NULL;
}
