#pragma once
// Shared flags and settings between screens and main loop
extern volatile bool     um_otaRequested;
extern volatile uint32_t um_sleep_timeout_ms; // 0 = never; dim at /2, sleep at full
