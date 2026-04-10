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
    // Use lv_refr_now instead of lv_timer_handler: we only need to push
    // the updated widgets to the display.  lv_timer_handler also processes
    // indevs and internal timers which is unsafe while we are blocking the
    // main loop — it leaves dangling indev state that causes a
    // LoadProhibited crash (NULL group_p dereference at offset 0x4c).
    // The main loop resumes after startFwDownload() returns and handles
    // all interaction (close button etc.) via its normal lv_timer_handler.
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

    // Focus the close/restart button now so the encoder reaches it
    // as soon as the download finishes (or errors) and the button appears.
    {
        lv_group_t *g = lv_group_get_default();
        if (g) {
            lv_group_remove_all_objs(g);
            if (close_btn) lv_group_add_obj(g, close_btn);
        }
    }

    // ---- 1. Stop mesh & WiFi ----
    fw_ui(bar, status_lbl, close_btn, 0,
          "Stopping mesh...", false, dim_col());
    esp_now_deinit();
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    delay(200);
    esp_wifi_stop();
    delay(50);
    esp_wifi_start();
    delay(50);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    // ---- 2. Join WiFi ----
    if (strlen(OTA_SSID) == 0) {
        fw_ui(bar, status_lbl, close_btn, 0,
              "Error: OTA_SSID not configured", true, err_col());
        return;
    }

    char joining[48];
    snprintf(joining, sizeof(joining), "Joining  %s ...", OTA_SSID);
    fw_ui(bar, status_lbl, close_btn, 0, joining, false, dim_col());

    WiFi.begin(OTA_SSID, OTA_PASSWORD);
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - t < UM_OTA_CONNECT_TIMEOUT_MS) {
        delay(400);
        fw_ui(bar, status_lbl, close_btn, 0, joining, false, dim_col());
    }

    if (WiFi.status() != WL_CONNECTED) {
        fw_ui(bar, status_lbl, close_btn, 0,
              "WiFi connect failed", true, err_col());
        return;
    }

    // ---- 3. HTTP GET ----
    fw_ui(bar, status_lbl, close_btn, 0,
          "Connecting to GitHub...", false, dim_col());

    WiFiClientSecure client;
    client.setInsecure();   // no cert pinning — user's own repo, embedded device

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(15000);

    if (!http.begin(client, FW_DOWNLOAD_URL)) {
        fw_ui(bar, status_lbl, close_btn, 0,
              "HTTP init failed", true, err_col());
        return;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        char err[48];
        snprintf(err, sizeof(err), "HTTP error %d", code);
        fw_ui(bar, status_lbl, close_btn, 0, err, true, err_col());
        http.end();
        return;
    }

    int total = http.getSize();   // -1 if chunked transfer (GitHub uses this)

    // ---- 4. Open SD destination ----
    SD.remove(FW_DOWNLOAD_DEST);
    File f = SD.open(FW_DOWNLOAD_DEST, FILE_WRITE);
    if (!f) {
        fw_ui(bar, status_lbl, close_btn, 0,
              "SD: could not open " FW_DOWNLOAD_DEST, true, err_col());
        http.end();
        return;
    }

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
            downloaded += n;

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

    // ---- 6. Result ----
    if (downloaded < 65536) {   // sanity: real firmware must be > 64 KB
        char err[64];
        snprintf(err, sizeof(err),
                 "Download incomplete (%d KB)", downloaded / 1024);
        fw_ui(bar, status_lbl, close_btn, 0, err, true, err_col());
        SD.remove(FW_DOWNLOAD_DEST);
        return;
    }

    // Success — show instructions rather than auto-restarting
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
