#pragma once
// LV_Helper stub for the desktop SDL2 simulator.
// On hardware this sets up the LilyGo display + indev drivers.
// In the sim, LVGL + SDL2 are initialised in main_sim.cpp instead.

#include "LilyGoLib.h"

template<typename T>
static inline void beginLvglHelper(T &) {}
