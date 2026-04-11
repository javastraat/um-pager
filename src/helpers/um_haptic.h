#pragma once

// um_haptic — DRV2605 haptic feedback helpers
// Navigate + select feedback is handled by instance.attachKeyboardFeedback()
// in um_main.cpp (wired into LV_Helper_v9 encoder read callback).
// This header provides the notification buzz used for new messages.

#ifndef SIM_BUILD
#include <LilyGoLib.h>

// Play a double-bump notification effect (Short Double Click Strong 100%).
// Temporarily overrides the haptic effect, then restores it.
static inline void um_haptic_notify()
{
    uint8_t prev = instance.getHapticEffects();
    instance.setHapticEffects(27);   // Short Double Click Strong 100%
    instance.vibrator();
    instance.setHapticEffects(prev);
}

#else
static inline void um_haptic_notify() {}
#endif
