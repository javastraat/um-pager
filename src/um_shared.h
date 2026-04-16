#pragma once
#include "um_theme.h"   // pulls in um_theme_t + all colour functions
#include "config.h"     // UM_MSG_SERVER_NAME_LEN and other constants

// ---------------------------------------------------------------------------
// Custom Material Icons — rendered via um_icons / um_icons_14 font
// Add codepoints to src/font/makefa.sh and run bash makefa.sh to rebuild
// ---------------------------------------------------------------------------
#define UM_SYMBOL_ANTENNA  "\xEE\xA2\xBF"  // U+E8BF settings_input_antenna
#define UM_SYMBOL_NFC      "\xEE\x86\xBB"  // U+E1BB nfc
#define UM_SYMBOL_WIFI     "\xEE\x87\xA2"  // U+E1E2 wifi
#define UM_SYMBOL_SD_CARD  "\xEE\x98\xA3"  // U+E623 sd_card
#define UM_SYMBOL_MAILBOX   "\xEE\x85\x98"  // U+E158 mailbox
#define UM_SYMBOL_SETTINGS  "\xEE\xA2\xB8"  // U+E8B8 settings
#define UM_SYMBOL_INFO      "\xEE\xA2\x8E"  // U+E88E info
#define UM_SYMBOL_HELP      "\xEE\xA2\x87"  // U+E887 help

// Shared flags and settings between screens and main loop
extern volatile bool       um_otaRequested;
extern volatile uint32_t   um_sleep_timeout_ms; // 0 = never sleep
extern volatile uint32_t   um_dim_timeout_ms;   // 0 = never dim
extern volatile uint8_t    um_dim_brightness;   // 0-255 target level when dimmed
extern volatile um_theme_t um_active_theme;     // UM_THEME_DARK or UM_THEME_LIGHT

// True once a time-sync broadcast (ric=UM_RIC_TIME_SYNC, func=3) has been
// received and written to the RTC.  False on boot (RTC-only time, shown red).
extern volatile bool       um_time_synced;

// Messaging-server display name received via ric=UM_RIC_MSG_SERVER, func=3.
// Empty string until first broadcast is received.
// Trailing digits stripped, uppercased: "pd2emc1" -> "PD2EMC".
extern char                um_msg_server_name[UM_MSG_SERVER_NAME_LEN];

// Unread direct-message counter (incremented when ric==UM_RIC_MY_PAGER arrives).
// Reset to 0 when the Messages screen is opened.
extern volatile uint32_t um_unread_count;

// -------------------------------------------------------
// Firmware-over-SD download
// -------------------------------------------------------
// Set true by the Download button in um_info.cpp; cleared by um_main.cpp
// after startFwDownload() returns.
extern volatile bool um_fwDownloadRequested;

// LVGL widget handles for the download progress overlay.
// Set by um_info.cpp before raising the flag so um_main.cpp can pass
// them to startFwDownload().
struct um_fw_widgets_t {
    lv_obj_t *bar;
    lv_obj_t *status_lbl;
    lv_obj_t *close_btn;
};
extern um_fw_widgets_t um_fw_widgets;

// True when a coordinator MAC has been found by the mesh screen.
// Safe to call from any screen — the mesh keeps running in the background.
bool um_mesh_has_coordinator();

// True when the LoRa service is still running after leaving the LoRa screen.
// Used by the menu to indicate background LoRa receive activity.
bool um_lora_background_active();

// Pause/resume background mesh traffic before switching the radio over to
// infrastructure WiFi for OTA or firmware downloads.
void um_mesh_suspend(bool suspend);

// Load persisted settings from NVS into the runtime variables above.
// Call once during setup() before the UI is created.
void um_settings_load();
// Save current runtime settings to NVS. Called when leaving the settings screen.
void um_settings_save();
