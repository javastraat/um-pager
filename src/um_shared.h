#pragma once
// Shared flags and settings between screens and main loop
extern volatile bool     um_otaRequested;
extern volatile uint32_t um_sleep_timeout_ms; // 0 = never sleep
extern volatile uint32_t um_dim_timeout_ms;   // 0 = never dim
extern volatile uint8_t  um_dim_brightness;   // 0-255 target level when dimmed
