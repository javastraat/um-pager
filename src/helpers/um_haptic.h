#pragma once

// um_haptic — DRV2605 haptic feedback helpers
// All functions are no-ops in SIM_BUILD.

#ifndef SIM_BUILD
#include <LilyGoLib.h>

static inline void _um_haptic_play(uint8_t effect)
{
    instance.drv.setWaveform(0, effect);
    instance.drv.setWaveform(1, 0);
    instance.drv.run();
}
#endif

// Light tick — encoder rotate / item focus change
static inline void um_haptic_navigate()
{
#ifndef SIM_BUILD
    _um_haptic_play(6);   // Sharp Click 30%
#endif
}

// Firm click — encoder press / item selected
static inline void um_haptic_select()
{
#ifndef SIM_BUILD
    _um_haptic_play(1);   // Strong Click 100%
#endif
}

// Double bump — new message / notification
static inline void um_haptic_notify()
{
#ifndef SIM_BUILD
    _um_haptic_play(27);  // Short Double Click Strong 100%
#endif
}
