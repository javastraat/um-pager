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

// Mesh background task poll rate
#define UM_MESH_POLL_ACTIVE_MS   100   // when mesh screen is visible
#define UM_MESH_POLL_BG_MS       500   // when running in background

// -------------------------------------------------------
// UI timing
// -------------------------------------------------------
#define UM_WELCOME_DURATION_MS      5000   // boot splash screen duration (ms)
#define UM_MENU_TOPBAR_INTERVAL_MS 30000   // topbar clock/coordinator refresh (ms)
#define UM_MESH_UI_TIMER_MS         500    // LVGL label refresh timer in mesh screen (ms)
#define UM_BSP_LONG_PRESS_MS        600    // backspace long-press threshold (ms)
#define UM_MAIN_LOOP_DELAY_MS         2    // main loop idle delay (ms)
#define UM_OTA_LOOP_DELAY_MS         20    // main loop delay while OTA is active (ms)

// -------------------------------------------------------
// Firmware version
// -------------------------------------------------------
#define UM_FW_VERSION  "v1.0"
