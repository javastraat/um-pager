#pragma once
#include <stdint.h>

// -------------------------------------------------------
// um_toast — lightweight on-screen notification
//
// Shows a small banner at the bottom of the display for
// UM_TOAST_DURATION_MS milliseconds, then auto-dismisses.
// Safe to call from any FreeRTOS task (uses lv_async_call).
// At most one toast is shown at a time; a newer one replaces
// the existing one.
// -------------------------------------------------------

// Show a toast with icon + text (copies the strings internally).
void um_toast_show(const char *icon, const char *text);
