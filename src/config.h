#pragma once
// ============================================================
// um-pager — central compile-time configuration
// ============================================================
// Build-flag overrides (NODE_NAME, OTA_SSID, OTA_PASSWORD) live
// in platformio.ini.  Values here are fallback defaults only.
// ============================================================

// -------------------------------------------------------
// Node identity
// Set via build flag: -D NODE_NAME=\"my-node\"
// -------------------------------------------------------
#ifndef NODE_NAME
#  define NODE_NAME "um-pager"
#endif

// -------------------------------------------------------
// Serial / debug
// -------------------------------------------------------
#define UM_SERIAL_BAUD  115200

// -------------------------------------------------------
// OTA over WiFi
// Credentials are set in platformio.ini [node_ota] build_flags.
// -------------------------------------------------------
#ifndef OTA_SSID
#  define OTA_SSID ""
#endif
#ifndef OTA_PASSWORD
#  define OTA_PASSWORD ""
#endif
#define UM_OTA_CONNECT_TIMEOUT_MS   30000UL   // max time to join WiFi
#define UM_OTA_UPLOAD_TIMEOUT_MS   120000UL   // max wait for upload to begin

// -------------------------------------------------------
// Power management defaults
// Written to NVS on first boot; can be changed via Settings screen.
// -------------------------------------------------------
#define UM_DEFAULT_SLEEP_TIMEOUT_MS   60000UL  // 60 s — 0 = never sleep
#define UM_DEFAULT_DIM_TIMEOUT_MS     30000UL  // 30 s — 0 = never dim
#define UM_DEFAULT_DIM_BRIGHTNESS     20        // ~8 % of full brightness (0-255)
#define UM_DEFAULT_KB_BRIGHTNESS      128       // keyboard backlight on boot (0-255)

// -------------------------------------------------------
// Settings UI
// -------------------------------------------------------
#define UM_SETTINGS_TIMEOUT_MAX_S   300   // slider upper bound for dim/sleep timers (seconds)
#define UM_SETTINGS_SLIDER_STEP       5   // encoder increment step for sliders

// -------------------------------------------------------
// Mesh protocol
// -------------------------------------------------------
#define UM_HB_INTERVAL    120000UL   // heartbeat broadcast period (ms)
#define UM_TEMP_INTERVAL  180000UL   // temperature broadcast period (ms)
#define UM_LOG_ROWS            20    // ring-buffer row count
#define UM_LOG_COL             72    // chars per short log row (including null)
#define UM_FULL_LEN           320    // max chars per full log message

// RIC used for network-wide time sync broadcasts (func=3, msg=YYMMDDHHmmss)
// Change this if your time-server uses a different RIC address.
#define UM_RIC_TIME_SYNC         224

// RIC used for messaging-server identification broadcasts (func=3, msg="<callsign>")
// Trailing digits are stripped and the name is uppercased for display.
// Max callsign length is 8 chars (e.g. pa999abc).
#define UM_RIC_MSG_SERVER          8
#define UM_MSG_SERVER_MAX_LEN      8   // max incoming callsign chars
#define UM_MSG_SERVER_NAME_LEN    16   // display-name buffer size (incl. null)

// This pager's own RIC address — messages addressed here trigger a toast notification.
#define UM_RIC_MY_PAGER       2041152

// Mesh background task poll rate
#define UM_MESH_POLL_ACTIVE_MS   100   // when mesh screen is visible
#define UM_MESH_POLL_BG_MS       500   // when running in background

// -------------------------------------------------------
// Mesh message inbox — RICs to exclude from being saved to SD.
// System broadcasts (time sync, server ident) plus known
// network-layer RICs that are not user messages.
// -------------------------------------------------------
#define UM_MSG_EXCLUDE_RICS { \
    UM_RIC_TIME_SYNC,  /* 224 — time broadcast          */ \
    UM_RIC_MSG_SERVER, /* 8   — server ident broadcast   */ \
    200u,              /* network layer                  */ \
    208u,              /* network layer                  */ \
    216u,              /* network layer                  */ \
    4512u,             /* network layer                  */ \
    4520u              /* network layer                  */ \
}
#define UM_MSG_EXCLUDE_COUNT 7

// Deduplication: suppress duplicate messages arriving within this window (seconds).
#define UM_MSG_DEDUP_WINDOW_S  30
// Number of recent (ric, hash) pairs to remember for dedup.
#define UM_MSG_DEDUP_SLOTS     16

// -------------------------------------------------------
// SD card paths
// The SD card is mounted at /sd by LilyGoLib.
// All application data lives under UM_SD_ROOT.
// -------------------------------------------------------
#define UM_SD_ROOT          "/sd"
// Paths for the Arduino SD library are card-relative (no /sd mount prefix)
#define UM_SD_BASE_DIR      "/UniversalMesh"
#define UM_SD_DIR_MESSAGES  "/UniversalMesh/messages"
#define UM_SD_DIR_OTA       "/UniversalMesh/ota"
#define UM_SD_DIR_LOGS      "/UniversalMesh/logs"
#define UM_SD_MAX_READ_LEN  65536   // max bytes returned by um_storage_read_file

// -------------------------------------------------------
// UI timing
// -------------------------------------------------------
#define UM_WELCOME_DURATION_MS      5000   // boot splash screen duration (ms)
#define UM_MENU_TOPBAR_INTERVAL_MS  1000   // topbar clock/coordinator refresh (ms)
#define UM_MESH_UI_TIMER_MS          500   // LVGL label refresh timer in mesh screen (ms)
#define UM_BSP_LONG_PRESS_MS        600    // backspace long-press threshold (ms)
#define UM_MAIN_LOOP_DELAY_MS         2    // main loop idle delay (ms)
#define UM_OTA_LOOP_DELAY_MS         20    // main loop delay while OTA is active (ms)

// -------------------------------------------------------
// Firmware version
// -------------------------------------------------------
#define UM_FW_VERSION  "v1.0.0"
#define UM_PAGER_FW_VERSION  "v1.0"
