#include <Arduino.h>
#include <ArduinoJson.h>
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <lvgl.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "UniversalMesh.h"
#include "um_nav.h"
#include "um_shared.h"
#include "config.h"
#include "helpers/um_storage.h"
#include "helpers/um_toast.h"
#include "helpers/um_haptic.h"

// Forward declarations
static void um_discovery_task(void *param);
static void um_key_bsp_cb(lv_event_t *e);

// -------------------------------------------------------
// Mesh state (persists across UI destroy/create)
// -------------------------------------------------------
enum UMState { UM_DISCOVERING, UM_CONNECTED, UM_NO_COORD };

volatile bool            um_otaRequested        = false;
volatile bool            um_fwDownloadRequested = false;
um_fw_widgets_t          um_fw_widgets          = {};

static UniversalMesh     um_mesh;
static volatile UMState  um_state    = UM_DISCOVERING;
static uint8_t           um_myMac[6]    = {};
static uint8_t           um_coordMac[6] = {};
static uint8_t           um_channel     = 0;
static unsigned long     um_lastHB      = 0;
static unsigned long     um_lastTemp    = 0;
static TaskHandle_t      um_task        = NULL;
static TaskHandle_t      um_update_task  = NULL;  // persistent Core-0 task for mesh work
static SemaphoreHandle_t um_mutex        = NULL;
static bool              um_mesh_started  = false;
static volatile bool     um_screen_active = false; // true only while mesh screen is open
static volatile bool     um_mesh_paused   = false; // true while OTA / FW download owns WiFi

static char    um_log[UM_LOG_ROWS][UM_LOG_COL]        = {};
static char    um_log_full[UM_LOG_ROWS][UM_FULL_LEN] = {};
static uint8_t um_logHead                            = 0;
static uint8_t um_logCount                           = 0;
static bool    um_log_dirty                          = false;

// -------------------------------------------------------
// LVGL state
// -------------------------------------------------------
static lv_timer_t *um_timer         = NULL;
static lv_timer_t *um_bsp_timer     = NULL;  // backspace long-press timer
static lv_obj_t   *um_root          = NULL;
static lv_obj_t   *um_status_lbl    = NULL;
static lv_obj_t   *um_info_lbl      = NULL;
static lv_obj_t   *um_log_cont      = NULL;
static lv_obj_t   *um_detail_cont   = NULL;
static lv_obj_t   *um_popup_cont    = NULL;
static uint8_t     um_dotPhase      = 0;

// -------------------------------------------------------
// Log helpers
// -------------------------------------------------------
static void um_log_push(const char *line, const char *full = nullptr)
{
    if (!um_mutex) return;
    if (xSemaphoreTake(um_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
    strncpy(um_log[um_logHead], line, UM_LOG_COL - 1);
    um_log[um_logHead][UM_LOG_COL - 1] = '\0';
    const char *src = full ? full : line;
    strncpy(um_log_full[um_logHead], src, UM_FULL_LEN - 1);
    um_log_full[um_logHead][UM_FULL_LEN - 1] = '\0';
    um_logHead = (um_logHead + 1) % UM_LOG_ROWS;
    if (um_logCount < UM_LOG_ROWS) um_logCount++;
    um_log_dirty = true;
    xSemaphoreGive(um_mutex);
}

static const char *um_type_name(uint8_t t)
{
    switch (t) {
        case 0x12: return "PING";
        case 0x13: return "PONG";
        case 0x14: return "ACK";
        case 0x15: return "DATA";
        case 0x16: return "KEYREQ";
        case 0x17: return "SEC";
        case 0x18: return "E2E";
        default:   return "???";
    }
}

// -------------------------------------------------------
// sendInfo — JSON node info reply
// -------------------------------------------------------
static void um_sendInfo(uint8_t *destMac, uint8_t appId)
{
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             um_myMac[0], um_myMac[1], um_myMac[2],
             um_myMac[3], um_myMac[4], um_myMac[5]);
    JsonDocument doc;
    doc["n"]    = NODE_NAME;
    doc["mac"]  = macStr;
    doc["up"]   = (unsigned long)(millis() / 1000UL);
    doc["heap"] = ESP.getFreeHeap();
    doc["rssi"] = WiFi.RSSI();
    doc["ch"]   = um_channel;
    doc["chip"] = ESP.getChipModel();
    doc["rev"]  = (int)ESP.getChipRevision();
    String out;
    serializeJson(doc, out);
    um_mesh.send(destMac, MESH_TYPE_DATA, appId, out);
}

// -------------------------------------------------------
// Mesh receive callback
// -------------------------------------------------------
static void um_on_receive(MeshPacket *pkt, uint8_t *senderMac)
{
    char line[72];
    uint8_t slen = pkt->payloadLen > 36 ? 36 : pkt->payloadLen;
    char snippet[37] = {};
    for (int i = 0; i < slen; i++) {
        uint8_t b = pkt->payload[i];
        snippet[i] = (b >= 0x20 && b < 0x7F) ? (char)b : '.';
    }
    snprintf(line, sizeof(line), "%02X:%02X %s A%02X %s",
             senderMac[4], senderMac[5],
             um_type_name(pkt->type), pkt->appId, snippet);

    char full[UM_FULL_LEN];
    uint8_t plen = pkt->payloadLen > 200 ? 200 : pkt->payloadLen;
    char payload[201] = {};
    for (int i = 0; i < plen; i++) {
        uint8_t b = pkt->payload[i];
        payload[i] = (b >= 0x20 && b < 0x7F) ? (char)b : '.';
    }
    snprintf(full, sizeof(full),
             "From: %02X:%02X:%02X:%02X:%02X:%02X\n"
             "To:   %02X:%02X:%02X:%02X:%02X:%02X\n"
             "Type: %s (0x%02X)\n"
             "AppID: 0x%02X\n\n"
             "%s",
             senderMac[0], senderMac[1], senderMac[2],
             senderMac[3], senderMac[4], senderMac[5],
             pkt->destMac[0], pkt->destMac[1], pkt->destMac[2],
             pkt->destMac[3], pkt->destMac[4], pkt->destMac[5],
             um_type_name(pkt->type), pkt->type,
             pkt->appId, payload);
    um_log_push(line, full);

    // ---- Time sync broadcast ------------------------------------------
    // Message format: {"ric":224,"func":3,"msg":"YYMMDDHHmmss"}
    // Any node may send it; we just check ric and func.
    // Runs on the mesh task, not the LVGL task — only touch volatile data
    // and RTC (both safe from ISR/task context).
    if (pkt->type == MESH_TYPE_DATA) {
        JsonDocument jdoc;
        DeserializationError jerr = deserializeJson(jdoc, payload, plen);
        if (!jerr) {
            uint32_t ric  = jdoc["ric"]  | 0xFFFFFFFFu;
            uint8_t  func = jdoc["func"] | 0xFF;
            if (ric == UM_RIC_TIME_SYNC && func == 3) {
                const char *msg = jdoc["msg"] | "";
                // Format: "YYYYMMDDHHMMSS" prefix (14 chars) + YYMMDDHHmmss (12 chars)
                // Total length 26; date data starts at offset 14.
                size_t msglen = strlen(msg);
                const char *data = (msglen == 26) ? msg + 14 : (msglen == 12) ? msg : nullptr;
                if (data) {
                    auto d2 = [](const char *s) -> int { return (s[0]-'0')*10 + (s[1]-'0'); };
                    uint16_t yr  = 2000 + d2(data);
                    uint8_t  mo  = d2(data+2);
                    uint8_t  day = d2(data+4);
                    uint8_t  hr  = d2(data+6);
                    uint8_t  mn  = d2(data+8);
                    uint8_t  sec = d2(data+10);
#ifndef SIM_BUILD
                    instance.rtc.setDateTime(yr, mo, day, hr, mn, sec);
#endif
                    um_time_synced = true;
                    char tslog[48];
                    snprintf(tslog, sizeof(tslog),
                             "[TIME] Synced %04u-%02u-%02u %02u:%02u:%02u",
                             yr, mo, day, hr, mn, sec);
                    um_log_push(tslog);
                }
            }
            // ---- Messaging-server identification broadcast ----------------
            // Message format: {"ric":8,"func":3,"msg":"<callsign>"}
            // Strip trailing digits, uppercase: "pd2emc1" -> "PD2EMC"
            if (ric == UM_RIC_MSG_SERVER && func == 3) {
                const char *msg = jdoc["msg"] | "";
                size_t mlen = strlen(msg);
                if (mlen > 0 && mlen <= UM_MSG_SERVER_MAX_LEN) {
                    // Strip trailing digits
                    char stripped[UM_MSG_SERVER_NAME_LEN] = {};
                    strncpy(stripped, msg, UM_MSG_SERVER_NAME_LEN - 1);
                    size_t tail = strlen(stripped);
                    while (tail > 0 && isdigit((unsigned char)stripped[tail - 1]))
                        stripped[--tail] = '\0';
                    // Uppercase
                    for (size_t i = 0; i < tail; i++)
                        stripped[i] = toupper((unsigned char)stripped[i]);
                    strncpy(um_msg_server_name, stripped, UM_MSG_SERVER_NAME_LEN - 1);
                    um_msg_server_name[UM_MSG_SERVER_NAME_LEN - 1] = '\0';
                    char mslog[48];
                    snprintf(mslog, sizeof(mslog), "[MSG-SRV] %s", um_msg_server_name);
                    um_log_push(mslog);
                }
            }
            // ---- Inbox: save user messages to SD -------------------------
            // Any ric/func/msg packet not in the exclude list is a user message.
            {
                const char *msg_field = jdoc["msg"] | "";
                static const uint32_t exclude[] = UM_MSG_EXCLUDE_RICS;
                bool excluded = false;
                for (size_t xi = 0; xi < UM_MSG_EXCLUDE_COUNT; xi++) {
                    if (ric == exclude[xi]) { excluded = true; break; }
                }
                if (!excluded && msg_field[0] != '\0') {
                    bool saved = um_storage_save_message(ric, func, msg_field, senderMac);
                    // Direct message to this pager → toast + unread badge
                    if (saved && ric == UM_RIC_MY_PAGER) {
                        um_unread_count = um_unread_count + 1;
                        char toast_txt[80];
                        snprintf(toast_txt, sizeof(toast_txt),
                                 "New message · RIC %lu", (unsigned long)ric);
                        um_haptic_notify();
                        um_toast_show(LV_SYMBOL_ENVELOPE, toast_txt);
                    }
                }
            }
        }
    }

    bool directToMe = (memcmp(pkt->destMac, um_myMac, 6) == 0);
    if (pkt->type != MESH_TYPE_DATA || !directToMe) return;

    // Plain-text direct message (not cmd: prefix, not JSON already handled above)
    bool isCmd = (plen >= 4 && strncmp(payload, "cmd:", 4) == 0);
    bool isJson = (plen > 0 && payload[0] == '{');
    if (!isCmd && !isJson && plen > 0) {
        // Save to SD as a direct message, RIC 0 = MAC-addressed
        bool saved = um_storage_save_message(0, 0, payload, senderMac);
        if (saved) {
            um_unread_count = um_unread_count + 1;
            char toast_txt[80];
            snprintf(toast_txt, sizeof(toast_txt), "Direct: %.60s", payload);
            um_haptic_notify();
            um_toast_show(LV_SYMBOL_ENVELOPE, toast_txt);
        }
        char dmlog[80];
        snprintf(dmlog, sizeof(dmlog), "[DM] %02X:%02X: %.40s",
                 senderMac[4], senderMac[5], payload);
        um_log_push(dmlog);
        return;
    }

    if (!isCmd) return;

    bool fromCoord = (memcmp(senderMac, um_coordMac, 6) == 0);
    if (!fromCoord) { um_log_push("[CMD] Ignored - not coordinator"); return; }

    const char *command = payload + 4;

    // Save cmd to SD so coordinator requests are auditable
    {
        char cmd_msg[128];
        snprintf(cmd_msg, sizeof(cmd_msg), "cmd:%s", command);
        um_storage_save_message(0, 0xFF, cmd_msg, senderMac);
    }
    char ack[220];
    snprintf(ack, sizeof(ack), "command received:%s", command);
    um_mesh.send(senderMac, MESH_TYPE_DATA, pkt->appId,
                 (const uint8_t *)ack, strlen(ack), 4);

    if (strcmp(command, "info") == 0 || strcmp(command, "info:long") == 0) {
        um_sendInfo(senderMac, pkt->appId);
        um_log_push("[CMD] info sent");
    } else if (strcmp(command, "reboot") == 0) {
        um_log_push("[CMD] Rebooting...");
        delay(100);
        ESP.restart();
    } else if (strcmp(command, "update") == 0) {
        um_otaRequested = true;
        um_log_push("[CMD] OTA requested");
    }
}

// -------------------------------------------------------
// Detail overlay
// -------------------------------------------------------
static void um_close_detail(void);

static void um_row_clicked_cb(lv_event_t *e)
{
    if (um_detail_cont) return;
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    int log_idx   = (int)(intptr_t)lv_obj_get_user_data(btn);

    um_detail_cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(um_detail_cont, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(um_detail_cont, um_col_surface_deep(), LV_PART_MAIN);
    lv_obj_set_style_border_color(um_detail_cont, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_border_width(um_detail_cont, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(um_detail_cont, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(um_detail_cont, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(um_detail_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(um_detail_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(um_detail_cont, 6, LV_PART_MAIN);
    lv_obj_clear_flag(um_detail_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *msg_lbl = lv_label_create(um_detail_cont);
    if (um_mutex && xSemaphoreTake(um_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        lv_label_set_text(msg_lbl, um_log_full[log_idx]);
        xSemaphoreGive(um_mutex);
    } else {
        lv_label_set_text(msg_lbl, "(unavailable)");
    }
    lv_obj_set_style_text_color(msg_lbl, um_col_green_bright(), LV_PART_MAIN);
    lv_obj_set_width(msg_lbl, lv_pct(100));
    lv_label_set_long_mode(msg_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(msg_lbl, 1);

    lv_obj_t *close_btn = lv_btn_create(um_detail_cont);
    lv_obj_set_width(close_btn, lv_pct(100));
    lv_obj_set_style_bg_color(close_btn, um_col_surface(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(close_btn, um_col_focus_green(),
                              (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_border_color(close_btn, um_col_border(), LV_PART_MAIN);
    lv_obj_set_style_border_width(close_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(close_btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(close_btn, [](lv_event_t *ev) { um_close_detail(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, LV_SYMBOL_LEFT "  Close");
    lv_obj_set_style_text_color(close_lbl, um_col_text_dim(), LV_PART_MAIN);
    lv_obj_center(close_lbl);

    lv_group_t *g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, close_btn);
        lv_group_focus_obj(close_btn);
        lv_group_focus_freeze(g, true);
    }
}

static void um_close_detail(void)
{
    if (!um_detail_cont) return;
    lv_group_t *g = lv_group_get_default();
    if (g) lv_group_focus_freeze(g, false);
    lv_obj_del(um_detail_cont);
    um_detail_cont = NULL;
    if (um_log_cont && lv_obj_get_child_count(um_log_cont) > 0) {
        lv_obj_t *first = lv_obj_get_child(um_log_cont, 0);
        if (first && g) lv_group_focus_obj(first);
    }
}

// -------------------------------------------------------
// Rebuild message rows
// -------------------------------------------------------
static void um_rebuild_rows(void)
{
    if (!um_log_cont || !um_mutex) return;

    lv_group_t *g = lv_group_get_default();
    lv_obj_clean(um_log_cont);

    if (xSemaphoreTake(um_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;

    int start = (UM_LOG_ROWS + um_logHead - um_logCount) % UM_LOG_ROWS;
    for (int i = um_logCount - 1; i >= 0; i--) {
        int idx = (start + i) % UM_LOG_ROWS;

        lv_obj_t *row = lv_btn_create(um_log_cont);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(row, 2, LV_PART_MAIN);
        lv_obj_set_style_pad_ver(row, 1, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(row, 3, LV_PART_MAIN);
        lv_obj_set_style_bg_color(row, um_col_focus_green(),
                                  (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER,
                                (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, um_log[idx]);
        lv_obj_set_width(lbl, lv_pct(100));
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(lbl, um_col_green(), LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, um_col_green_bright(),
                                    (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));

        lv_obj_set_user_data(row, (void *)(intptr_t)idx);
        lv_obj_add_event_cb(row, um_row_clicked_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(row, um_key_bsp_cb,     LV_EVENT_KEY,     NULL);
        lv_obj_add_event_cb(row, [](lv_event_t *ev) {
            lv_obj_scroll_to_view(lv_event_get_target_obj(ev), LV_ANIM_ON);
        }, LV_EVENT_FOCUSED, NULL);

        if (g) lv_group_add_obj(g, row);
    }

    if (g && lv_obj_get_child_count(um_log_cont) > 0) {
        lv_group_focus_obj(lv_obj_get_child(um_log_cont, 0));
    }

    xSemaphoreGive(um_mutex);
    um_log_dirty = false;
}

// -------------------------------------------------------
// Popup (long-press backspace)
// -------------------------------------------------------
static void um_popup_close()
{
    if (!um_popup_cont) return;
    lv_group_t *g = lv_group_get_default();
    lv_obj_del(um_popup_cont);
    um_popup_cont = NULL;
    // Restore focus to first log row
    if (g && um_log_cont && lv_obj_get_child_count(um_log_cont) > 0)
        lv_group_focus_obj(lv_obj_get_child(um_log_cont, 0));
}

static void um_rescan();

static void um_popup_open()
{
    if (um_popup_cont) return;

    um_popup_cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(um_popup_cont, 260, 100);
    lv_obj_center(um_popup_cont);
    lv_obj_set_style_bg_color(um_popup_cont, um_col_surface(), LV_PART_MAIN);
    lv_obj_set_style_border_color(um_popup_cont, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_border_width(um_popup_cont, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(um_popup_cont, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(um_popup_cont, 24, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(um_popup_cont, um_col_border_focus(), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(um_popup_cont, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_pad_all(um_popup_cont, 10, LV_PART_MAIN);
    lv_obj_clear_flag(um_popup_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(um_popup_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(um_popup_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(um_popup_cont, 8, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(um_popup_cont);
    lv_label_set_text(title, LV_SYMBOL_WIFI "  Find Coordinator");
    lv_obj_set_style_text_color(title, um_col_cyan_bright(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);

    lv_obj_t *btn_row = lv_obj_create(um_popup_cont);
    lv_obj_set_width(btn_row, lv_pct(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(btn_row, 8, LV_PART_MAIN);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Scan button
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
        um_popup_close();
        um_rescan();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *scan_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_lbl, LV_SYMBOL_REFRESH "  Rescan");
    lv_obj_set_style_text_color(scan_lbl, um_col_cyan_bright(), LV_PART_MAIN);
    lv_obj_center(scan_lbl);

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
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t *e) { um_popup_close(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, LV_SYMBOL_CLOSE "  Cancel");
    lv_obj_set_style_text_color(cancel_lbl, UM_COL(180,100,100, 150,35,35), LV_PART_MAIN);
    lv_obj_center(cancel_lbl);

    lv_group_t *g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, scan_btn);
        lv_group_add_obj(g, cancel_btn);
        lv_group_focus_obj(scan_btn);
    }
}

// Long-press timer callback
static void um_bsp_timer_cb(lv_timer_t *t)
{
    um_bsp_timer = NULL;
    if (!um_popup_cont && !um_detail_cont)
        um_popup_open();
}

// Key handler attached to every focusable object — detects backspace long-press
static void um_key_bsp_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_BACKSPACE || key == 8) {
        if (um_bsp_timer) { lv_timer_reset(um_bsp_timer); return; }
        um_bsp_timer = lv_timer_create(um_bsp_timer_cb, UM_BSP_LONG_PRESS_MS, NULL);
        lv_timer_set_repeat_count(um_bsp_timer, 1);
    } else {
        if (um_bsp_timer) { lv_timer_del(um_bsp_timer); um_bsp_timer = NULL; }
    }
}

// -------------------------------------------------------
// Rescan — reset mesh state and restart discovery
// -------------------------------------------------------
static void um_rescan()
{
    // Kill existing task if still running
    if (um_task) { vTaskDelete(um_task); um_task = NULL; }

    // Reset state
    um_state   = UM_DISCOVERING;
    um_channel = 0;
    memset(um_coordMac, 0, 6);
    um_logHead  = 0;
    um_logCount = 0;
    um_log_dirty = true;
    if (um_log_cont) lv_obj_clean(um_log_cont);

    um_log_push("Rescanning channels 1-13...");
    xTaskCreate(um_discovery_task, "um_disc", 4096, NULL, 5, &um_task);
}

// -------------------------------------------------------
// Discovery task (FreeRTOS)
// -------------------------------------------------------
static void um_discovery_task(void *param)
{
    char line[72];

    WiFi.mode(WIFI_STA);
    esp_err_t wifi_ret = esp_wifi_start();
    if (wifi_ret != ESP_OK) {
        snprintf(line, sizeof(line), "WiFi start failed: %d", wifi_ret);
        um_log_push(line);
        vTaskDelete(NULL);
        return;
    }

    wifi_mode_t mode;
    int wait_count = 0;
    bool wifi_ready = false;
    for (wait_count = 0; wait_count < 40; wait_count++) {
        esp_wifi_get_mode(&mode);
        if (mode == WIFI_MODE_STA) { wifi_ready = true; break; }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    snprintf(line, sizeof(line), "WiFi STA ready after %d checks", wait_count);
    um_log_push(line);
    if (!wifi_ready) {
        um_log_push("WiFi STA not ready!");
        vTaskDelete(NULL);
        return;
    }

    if (!um_mesh.begin(1)) {
        um_log_push("ESP-NOW init FAILED!");
        vTaskDelete(NULL);
        return;
    }
    um_log_push("ESP-NOW init OK");

    // Scan with up to 3 attempts — a missed PONG on the first pass is common
    // because the WiFi/ESP-NOW stack may not be fully settled on attempt 1.
    um_channel = 0;
    for (int attempt = 1; attempt <= 3 && um_channel == 0; attempt++) {
        if (attempt > 1) {
            snprintf(line, sizeof(line), "Retry scan (attempt %d/3)...", attempt);
            um_log_push(line);
            vTaskDelay(pdMS_TO_TICKS(300));
        } else {
            um_log_push("Scanning channels 1-13...");
        }
        um_channel = um_mesh.findCoordinatorChannel(NODE_NAME);
        snprintf(line, sizeof(line), "Scan attempt %d returned ch%d", attempt, um_channel);
        um_log_push(line);
    }

    if (um_channel == 0) {
        um_log_push("No coordinator found after 3 attempts");
        um_state = UM_NO_COORD;
        um_task  = NULL;
        vTaskDelete(NULL);
        return;
    }

    snprintf(line, sizeof(line), "Coordinator on ch%d", um_channel);
    um_log_push(line);

    um_mesh.getCoordinatorMac(um_coordMac);
    snprintf(line, sizeof(line), "Coord %02X:%02X:%02X:%02X:%02X:%02X",
             um_coordMac[0], um_coordMac[1], um_coordMac[2],
             um_coordMac[3], um_coordMac[4], um_coordMac[5]);
    um_log_push(line);

    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(um_channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
    um_mesh.setCoordinatorMac(um_coordMac);
    um_mesh.onReceive(um_on_receive);
    esp_wifi_get_mac(WIFI_IF_STA, um_myMac);

    snprintf(line, sizeof(line), "My MAC %02X:%02X:%02X:%02X:%02X:%02X",
             um_myMac[0], um_myMac[1], um_myMac[2],
             um_myMac[3], um_myMac[4], um_myMac[5]);
    um_log_push(line);

    um_mesh.send(um_coordMac, MESH_TYPE_PING, 0x00,
                 (const uint8_t *)NODE_NAME, strlen(NODE_NAME), 4);
    um_mesh.send(um_coordMac, MESH_TYPE_DATA, 0x06,
                 (const uint8_t *)NODE_NAME, strlen(NODE_NAME), 4);
    um_log_push("Announced. Listening...");

    um_lastHB   = millis() - UM_HB_INTERVAL;
    um_lastTemp = millis() - UM_TEMP_INTERVAL;
    um_state    = UM_CONNECTED;
    um_task     = NULL;
    vTaskDelete(NULL);
}

// -------------------------------------------------------
// Background mesh task — Core 0, runs forever once started.
// Calls update() and handles periodic sends so the LVGL
// timer (Core 1) never blocks on mesh work.
// -------------------------------------------------------
static void um_update_loop(void *param)
{
    for (;;) {
        if (um_mesh_paused) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (um_state == UM_CONNECTED) {
            um_mesh.update();
            taskYIELD(); // let the WiFi driver on Core 0 breathe immediately after

            unsigned long now = millis();

            if (now - um_lastHB >= UM_HB_INTERVAL) {
                um_lastHB = now;
                uint8_t hb = 0x01;
                um_mesh.send(um_coordMac, MESH_TYPE_DATA, 0x05, &hb, 1, 4);
                um_mesh.send(um_coordMac, MESH_TYPE_DATA, 0x06,
                             (const uint8_t *)NODE_NAME, strlen(NODE_NAME), 4);
                um_log_push("[HB] Heartbeat sent");
            }

            if (now - um_lastTemp >= UM_TEMP_INTERVAL) {
                um_lastTemp = now;
                float tempC = temperatureRead();
                JsonDocument doc;
                doc["name"] = NODE_NAME;
                doc["temp"] = serialized(String(tempC, 1));
                String payload;
                serializeJson(doc, payload);
                if (um_mesh.sendToCoordinator(0x01, payload)) {
                    char line[48];
                    snprintf(line, sizeof(line), "[TX] Temp: %.1f C", tempC);
                    um_log_push(line);
                } else {
                    um_log_push("[TX] Temp send failed");
                }
            }
        }
        // When the mesh screen is visible: poll at 100 ms (was 50 ms).
        // When in the background (menu/other screens): back off to 500 ms so
        // Core-0 memory-bus pressure doesn't stutter LVGL on Core 1.
        vTaskDelay(pdMS_TO_TICKS(um_screen_active ? UM_MESH_POLL_ACTIVE_MS : UM_MESH_POLL_BG_MS));
    }
}

// -------------------------------------------------------
// 500ms LVGL timer — UI labels only, no mesh work
// -------------------------------------------------------
static void um_timer_cb(lv_timer_t *t)
{

    if (um_status_lbl) {
        if (um_state == UM_DISCOVERING) {
            static const char *dots[] = {".", "..", "..."};
            char buf[24];
            snprintf(buf, sizeof(buf), "Scanning%s", dots[um_dotPhase % 3]);
            um_dotPhase++;
            lv_label_set_text(um_status_lbl, buf);
            lv_obj_set_style_text_color(um_status_lbl, um_col_warn(), LV_PART_MAIN);
        } else if (um_state == UM_NO_COORD) {
            lv_label_set_text(um_status_lbl, "No coordinator");
            lv_obj_set_style_text_color(um_status_lbl, um_col_err(), LV_PART_MAIN);
        } else {
            lv_label_set_text(um_status_lbl, "Connected");
            lv_obj_set_style_text_color(um_status_lbl, um_col_ok(), LV_PART_MAIN);
        }
    }

    if (um_info_lbl && um_state == UM_CONNECTED) {
        char buf[56];
        snprintf(buf, sizeof(buf), "Ch:%d  %02X:%02X:%02X:%02X:%02X:%02X",
                 um_channel,
                 um_myMac[0], um_myMac[1], um_myMac[2],
                 um_myMac[3], um_myMac[4], um_myMac[5]);
        lv_label_set_text(um_info_lbl, buf);
        lv_obj_set_style_text_color(um_info_lbl, um_col_text_dim(), LV_PART_MAIN);
    }

    if (um_log_dirty && !um_detail_cont) {
        um_rebuild_rows();
    }
}

// -------------------------------------------------------
// Public state query — used by menu topbar
// -------------------------------------------------------
bool um_mesh_has_coordinator()
{
    for (int i = 0; i < 6; i++)
        if (um_coordMac[i] != 0) return true;
    return false;
}

void um_mesh_suspend(bool suspend)
{
    um_mesh_paused = suspend;
    um_log_push(suspend ? "[MESH] Paused for WiFi task"
                        : "[MESH] Resumed");
}

// -------------------------------------------------------
// um_mesh_create / um_mesh_destroy  (called by nav)
// -------------------------------------------------------
void um_mesh_create()
{
    um_root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(um_root, lv_pct(100), lv_pct(100));
    lv_obj_center(um_root);
    lv_obj_set_style_bg_color(um_root, um_col_bg(), LV_PART_MAIN);
    lv_obj_set_style_border_width(um_root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(um_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(um_root, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(um_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(um_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(um_root, LV_OBJ_FLAG_SCROLLABLE);

    // Header row
    lv_obj_t *hdr = lv_obj_create(um_root);
    lv_obj_set_width(hdr, lv_pct(100));
    lv_obj_set_height(hdr, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(hdr, 2, LV_PART_MAIN);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hdr_title = lv_label_create(hdr);
    lv_label_set_text(hdr_title, LV_SYMBOL_WIFI " UniversalMesh");
    lv_obj_set_style_text_color(hdr_title, um_col_cyan_bright(), LV_PART_MAIN);

    um_status_lbl = lv_label_create(hdr);
    lv_label_set_text(um_status_lbl, "...");
    lv_obj_set_style_text_color(um_status_lbl, um_col_warn(), LV_PART_MAIN);

    // Back button (returns to menu)
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
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_HOME);
    lv_obj_set_style_text_color(back_lbl, um_col_cyan(), LV_PART_MAIN);
    lv_obj_center(back_lbl);

    lv_group_t *g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, back_btn);
        lv_group_focus_obj(back_btn);

        // Intercept backspace long-press from any focused object in this screen
        lv_group_set_focus_cb(g, [](lv_group_t *grp) {
            // Called when focus changes — no action needed here
        });
    }

    // Backspace long-press: attach key handler to every focusable object on this screen.
    // We use a shared lambda stored as a free function via a file-scope helper.
    // Applied to back_btn now; also applied to each log row in um_rebuild_rows.
    lv_obj_add_event_cb(back_btn, um_key_bsp_cb, LV_EVENT_KEY, NULL);

    // Divider
    lv_obj_t *div = lv_obj_create(um_root);
    lv_obj_set_size(div, lv_pct(100), 1);
    lv_obj_set_style_bg_color(div, um_col_divider(), LV_PART_MAIN);
    lv_obj_set_style_border_width(div, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(div, 0, LV_PART_MAIN);

    // Info line
    um_info_lbl = lv_label_create(um_root);
    lv_label_set_text(um_info_lbl, "Ch:--  --:--:--:--:--:--");
    lv_obj_set_style_text_color(um_info_lbl, um_col_text_hint(), LV_PART_MAIN);
    lv_obj_set_width(um_info_lbl, lv_pct(100));

    // Divider
    lv_obj_t *div2 = lv_obj_create(um_root);
    lv_obj_set_size(div2, lv_pct(100), 1);
    lv_obj_set_style_bg_color(div2, um_col_divider(), LV_PART_MAIN);
    lv_obj_set_style_border_width(div2, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(div2, 0, LV_PART_MAIN);

    // Scrollable message list
    um_log_cont = lv_obj_create(um_root);
    lv_obj_set_width(um_log_cont, lv_pct(100));
    lv_obj_set_flex_grow(um_log_cont, 1);
    lv_obj_set_style_bg_opa(um_log_cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(um_log_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(um_log_cont, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(um_log_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(um_log_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(um_log_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(um_log_cont, LV_DIR_VER);
    lv_obj_set_style_bg_color(um_log_cont, um_col_scrollbar(), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(um_log_cont, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(um_log_cont, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(um_log_cont, 2, LV_PART_SCROLLBAR);

    um_screen_active = true;

    // Start mesh only once
    if (!um_mesh_started) {
        um_mesh_started = true;
        if (!um_mutex) um_mutex = xSemaphoreCreateMutex();
        xTaskCreate(um_discovery_task, "um_disc", 4096, NULL, 5, &um_task);
        // Background update loop on Core 0 — keeps mesh work off the LVGL thread
        xTaskCreatePinnedToCore(um_update_loop, "um_update", 4096, NULL, 2,
                                &um_update_task, 0);
    }

    um_timer = lv_timer_create(um_timer_cb, UM_MESH_UI_TIMER_MS, NULL);
    lv_timer_ready(um_timer);

    // Repaint any buffered log lines
    if (um_logCount > 0) um_log_dirty = true;
}

void um_mesh_destroy()
{
    um_screen_active = false; // tell update loop to back off — LVGL needs Core 0 quiet
    if (um_timer)     { lv_timer_del(um_timer);     um_timer     = NULL; }
    if (um_bsp_timer) { lv_timer_del(um_bsp_timer); um_bsp_timer = NULL; }
    if (um_popup_cont)  { lv_obj_del(um_popup_cont);  um_popup_cont  = NULL; }
    if (um_detail_cont) { lv_obj_del(um_detail_cont); um_detail_cont = NULL; }

    lv_group_t *g = lv_group_get_default();
    if (g) lv_group_remove_all_objs(g);

    if (um_root) { lv_obj_del(um_root); um_root = NULL; }

    um_status_lbl = NULL;
    um_info_lbl   = NULL;
    um_log_cont   = NULL;
}
