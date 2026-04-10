#pragma once
// LilyGoLib stub for the desktop SDL2 simulator.
// Provides the types and symbols that UI source files reference,
// as no-ops so they compile and link cleanly on the host.

#include <stdint.h>
#include <cstdlib>

#ifndef NODE_NAME
#define NODE_NAME "sim-node"
#endif

#ifndef DEVICE_MAX_BRIGHTNESS_LEVEL
#define DEVICE_MAX_BRIGHTNESS_LEVEL 255
#endif

#define NO_INIT_FATFS 0

typedef uint32_t WakeupSource_t;
#define WAKEUP_SRC_BOOT_BUTTON   ((WakeupSource_t)0x01)
#define WAKEUP_SRC_ROTARY_BUTTON ((WakeupSource_t)0x02)

struct LilyGoDevice {
    void begin(int) {}
    void loop()     {}
    void setBrightness(int) {}
    // sleep() quits the simulator instead of deep-sleeping the MCU
    void sleep(WakeupSource_t) { ::exit(0); }
};

inline LilyGoDevice instance;
