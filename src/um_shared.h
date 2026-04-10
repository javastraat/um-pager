#pragma once
#include "um_theme.h"   // pulls in um_theme_t + all colour functions

// Shared flags and settings between screens and main loop
extern volatile bool       um_otaRequested;
extern volatile uint32_t   um_sleep_timeout_ms; // 0 = never sleep
extern volatile uint32_t   um_dim_timeout_ms;   // 0 = never dim
extern volatile uint8_t    um_dim_brightness;   // 0-255 target level when dimmed
extern volatile um_theme_t um_active_theme;     // UM_THEME_DARK or UM_THEME_LIGHT

// True when a coordinator MAC has been found by the mesh screen.
// Safe to call from any screen — the mesh keeps running in the background.
bool um_mesh_has_coordinator();

// Load persisted settings from NVS into the runtime variables above.
// Call once during setup() before the UI is created.
void um_settings_load();
// Save current runtime settings to NVS. Called when leaving the settings screen.
void um_settings_save();
