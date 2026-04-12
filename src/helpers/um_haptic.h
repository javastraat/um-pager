#pragma once

// um_haptic — DRV2605 haptic feedback helpers
// All functions call instance.vibrator() directly (same path as hardware init),
// temporarily overriding _effects so the caller controls the waveform.

#ifndef SIM_BUILD
#include <LilyGoLib.h>

// Light tick — encoder rotate / item focus change (Sharp Click 30%)
static inline void um_haptic_navigate()
{
    uint8_t prev = instance.getHapticEffects();
    instance.setHapticEffects(6);
    instance.vibrator();
    instance.setHapticEffects(prev);
}

// Firm click — encoder center button press (Strong Click 100%)
static inline void um_haptic_select()
{
    uint8_t prev = instance.getHapticEffects();
    instance.setHapticEffects(1);
    instance.vibrator();
    instance.setHapticEffects(prev);
}

// Double bump — new message / notification (Short Double Click Strong 100%)
static inline void um_haptic_notify()
{
    uint8_t prev = instance.getHapticEffects();
    instance.setHapticEffects(27);
    instance.vibrator();
    instance.setHapticEffects(prev);
}

#else
static inline void um_haptic_navigate() {}
static inline void um_haptic_select()   {}
static inline void um_haptic_notify()   {}
#endif
