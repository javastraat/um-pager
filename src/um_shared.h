#pragma once
#include "um_theme.h"   // pulls in um_theme_t + all colour functions
#include "config.h"     // UM_MSG_SERVER_NAME_LEN and other constants

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

// True when a coordinator MAC has been found by the mesh screen.
// Safe to call from any screen — the mesh keeps running in the background.
bool um_mesh_has_coordinator();

// Load persisted settings from NVS into the runtime variables above.
// Call once during setup() before the UI is created.
void um_settings_load();
// Save current runtime settings to NVS. Called when leaving the settings screen.
void um_settings_save();
