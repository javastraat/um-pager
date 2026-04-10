#pragma once
// -------------------------------------------------------
// fw_download.h — download latest firmware.bin from GitHub
// Saves to UM_SD_DIR_OTA/firmware.bin on the SD card.
// Call startFwDownload() from the main Arduino loop after
// um_fwDownloadRequested is set.
// On success the device restarts (mesh was stopped for WiFi).
// On error, startFwDownload() returns so the caller can expose
// an error close/restart button to the user.
// -------------------------------------------------------

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <SD.h>
#include <lvgl.h>
#include "config.h"
#include "um_shared.h"

#define FW_DOWNLOAD_URL  "https://github.com/javastraat/um-pager/raw/refs/heads/main/firmware/firmware.bin"
#define FW_DOWNLOAD_DEST UM_SD_DIR_OTA "/firmware.bin"
#define FW_CHUNK_SIZE    4096

// -------------------------------------------------------
// Internal progress helper — updates LVGL bar + label and
// renders one frame.  Passing nullptr for any widget is safe.
// -------------------------------------------------------
static void fw_ui(lv_obj_t *bar, lv_obj_t *lbl, lv_obj_t *close_btn,
                  int pct, const char *status, bool show_close,
                  lv_color_t col)
{
    if (bar) {
        lv_bar_set_value(bar, pct, LV_ANIM_OFF);
    }
    if (lbl) {
        lv_label_set_text(lbl, status);
        lv_obj_set_style_text_color(lbl, col, LV_PART_MAIN);
    }
    if (show_close && close_btn) {
        lv_obj_remove_flag(close_btn, LV_OBJ_FLAG_HIDDEN);
    }
    // Use lv_refr_now instead of lv_timer_handler here: we only need to push
    // the updated widgets to the panel while the download routine is blocking.
    // Normal input/timer processing resumes in the main loop after this helper
    // returns.
    lv_refr_now(lv_display_get_default());
}

// -------------------------------------------------------
// startFwDownload
// bar, status_lbl, close_btn may be nullptr.
// -------------------------------------------------------
static void startFwDownload(lv_obj_t *bar, lv_obj_t *status_lbl,
                            lv_obj_t *close_btn)
{
    auto ok_col  = []() { return lv_color_make(  0, 210,  85); };
    auto err_col = []() { return lv_color_make(220,  55,  55); };
    auto dim_col = []() { return lv_color_make(125, 128, 142); };
    auto txt_col = []() { return lv_color_make(220, 220, 230); };

    // Reset all indevs before touching the group: the encoder still has
    // dl_btn as its "last pressed" object.  lv_group_remove_all_objs()
    // sets dl_btn->group_p = NULL; any subsequent indev processing that
    // does obj->group_p->... would crash at offset 0x4c.  Resetting
    // the indevs first clears those stale object references safely.
    {
        lv_indev_t *indev = NULL;
        while ((indev = lv_indev_get_next(indev)) != NULL) {
            lv_indev_reset(indev, NULL);
        }
    }

    // Add close_btn to the group and pre-focus it so the encoder lands on
    // it as soon as the download finishes.  Do NOT call lv_group_remove_all_objs:
    // that sets group_p = NULL on every info-screen object; lv_refr_now then
    // renders those objects and dereferences the NULL group_p → LoadProhibited
    // crash at offset 0x4c.  Leaving existing objects in the group is safe
    // because we only call lv_refr_now (not lv_timer_handler) while blocking,
    // so the encoder never processes input until startFwDownload() returns.
    {
        lv_group_t *g = lv_group_get_default();
        if (g && close_btn) {
            lv_group_add_obj(g, close_btn);
            lv_group_focus_obj(close_btn);
        }
    }

    // ---- 1. Stop mesh & WiFi ----
    Serial.println("[FW-DL] Pausing mesh background task...");
    um_mesh_suspend(true);
    delay(UM_MESH_POLL_BG_MS + 50);

    Serial.println("[FW-DL] Stopping mesh & WiFi...");
    fw_ui(bar, status_lbl, close_btn, 0,
          "Stopping mesh...", false, dim_col());
    // Do not call esp_now_deinit() here: on this ESP32-S3 stack it can panic
    // if the background mesh task was using ESP-NOW moments earlier. Pausing
    // the mesh worker and shutting the WiFi radio down directly is sufficient.
    wifi_mode_t wifi_mode = WIFI_MODE_NULL;
    esp_err_t wifi_mode_err = esp_wifi_get_mode(&wifi_mode);
    if (wifi_mode_err == ESP_OK && wifi_mode != WIFI_MODE_NULL) {
        Serial.printf("[FW-DL] Existing WiFi mode: %d, resetting radio...\n", (int)wifi_mode);
        WiFi.disconnect(true, true);
        WiFi.mode(WIFI_OFF);
        delay(200);
        esp_wifi_stop();
        delay(50);
        esp_wifi_start();
        delay(50);
    } else {
        Serial.println("[FW-DL] WiFi not initialized yet, starting fresh STA mode");
    }
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    // ---- 2. Join WiFi ----
    if (strlen(OTA_SSID) == 0) {
        Serial.println("[FW-DL] ERROR: OTA_SSID not configured");
        fw_ui(bar, status_lbl, close_btn, 0,
              "Error: OTA_SSID not configured", true, err_col());
        return;
    }

    char joining[48];
    snprintf(joining, sizeof(joining), "Joining  %s ...", OTA_SSID);
    Serial.printf("[FW-DL] Joining WiFi: %s\n", OTA_SSID);
    fw_ui(bar, status_lbl, close_btn, 0, joining, false, dim_col());

    WiFi.begin(OTA_SSID, OTA_PASSWORD);
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - t < UM_OTA_CONNECT_TIMEOUT_MS) {
        delay(400);
        Serial.print('.');
        fw_ui(bar, status_lbl, close_btn, 0, joining, false, dim_col());
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[FW-DL] ERROR: WiFi connect failed");
        fw_ui(bar, status_lbl, close_btn, 0,
              "WiFi connect failed", true, err_col());
        return;
    }
    Serial.printf("[FW-DL] WiFi connected, IP: %s\n", WiFi.localIP().toString().c_str());

    // ---- 3. HTTP GET ----
    Serial.printf("[FW-DL] Connecting to: %s\n", FW_DOWNLOAD_URL);
    fw_ui(bar, status_lbl, close_btn, 0,
          "Connecting to GitHub...", false, dim_col());

    WiFiClientSecure client;
    client.setInsecure();   // no cert pinning — user's own repo, embedded device

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(15000);

    if (!http.begin(client, FW_DOWNLOAD_URL)) {
        Serial.println("[FW-DL] ERROR: HTTP init failed");
        fw_ui(bar, status_lbl, close_btn, 0,
              "HTTP init failed", true, err_col());
        return;
    }

    int code = http.GET();
    Serial.printf("[FW-DL] HTTP GET response: %d\n", code);
    if (code != HTTP_CODE_OK) {
        char err[48];
        snprintf(err, sizeof(err), "HTTP error %d", code);
        Serial.printf("[FW-DL] ERROR: %s\n", err);
        fw_ui(bar, status_lbl, close_btn, 0, err, true, err_col());
        http.end();
        return;
    }

    int total = http.getSize();   // -1 if chunked transfer (GitHub uses this)
    Serial.printf("[FW-DL] Content-Length: %d bytes\n", total);

    // ---- 4. Open SD destination ----
    Serial.println("[FW-DL] Opening SD destination: " FW_DOWNLOAD_DEST);
    SD.remove(FW_DOWNLOAD_DEST);
    File f = SD.open(FW_DOWNLOAD_DEST, FILE_WRITE);
    if (!f) {
        Serial.println("[FW-DL] ERROR: could not open " FW_DOWNLOAD_DEST);
        fw_ui(bar, status_lbl, close_btn, 0,
              "SD: could not open " FW_DOWNLOAD_DEST, true, err_col());
        http.end();
        return;
    }
    Serial.println("[FW-DL] SD file open OK, streaming...");

    // ---- 5. Stream to SD ----
    WiFiClient *stream = http.getStreamPtr();
    static uint8_t chunk[FW_CHUNK_SIZE];
    int downloaded = 0;

    while (http.connected() &&
           (total < 0 || downloaded < total)) {
        int avail = stream->available();
        if (avail > 0) {
            int n = stream->readBytes(chunk,
                        min(avail, (int)FW_CHUNK_SIZE));
            f.write(chunk, n);
            int prev_kb = downloaded / 65536;
            downloaded += n;
            if (downloaded / 65536 > prev_kb) {
                Serial.printf("[FW-DL] %d KB received...\n", downloaded / 1024);
            }

            int pct = (total > 0) ? (downloaded * 100 / total) : 50;
            char status[48];
            if (total > 0) {
                snprintf(status, sizeof(status),
                         "Downloading...  %d / %d KB",
                         downloaded / 1024, total / 1024);
            } else {
                snprintf(status, sizeof(status),
                         "Downloading...  %d KB", downloaded / 1024);
            }
            fw_ui(bar, status_lbl, close_btn, pct, status, false, txt_col());
        } else if (!stream->connected()) {
            break;
        }
        delay(1);
    }

    f.flush();
    f.close();
    http.end();
    Serial.printf("[FW-DL] Stream done, %d bytes written\n", downloaded);

    // ---- 6. Result ----
    if (downloaded < 65536) {   // sanity: real firmware must be > 64 KB
        char err[64];
        snprintf(err, sizeof(err),
                 "Download incomplete (%d KB)", downloaded / 1024);
        Serial.printf("[FW-DL] ERROR: %s\n", err);
        fw_ui(bar, status_lbl, close_btn, 0, err, true, err_col());
        SD.remove(FW_DOWNLOAD_DEST);
        return;
    }

    // Success — show instructions rather than auto-restarting
    Serial.printf("[FW-DL] SUCCESS: firmware.bin saved (%d KB) to " FW_DOWNLOAD_DEST "\n",
                  downloaded / 1024);
    char done[128];
    snprintf(done, sizeof(done),
             LV_SYMBOL_OK "  firmware.bin saved (%d KB)\n\n"
             "Saved to:  " FW_DOWNLOAD_DEST "\n\n"
             "To install: use the  " LV_SYMBOL_DOWNLOAD "  OTA Update button\n"
             "to flash it wirelessly, or connect via USB.",
             downloaded / 1024);
    fw_ui(bar, status_lbl, close_btn, 100, done, true, ok_col());
    // Returns — close_btn (labelled "Restart") is now visible and focussed.
}
