#pragma once
// Minimal Arduino API stub for the desktop SDL2 simulator.
// Only covers what the UI source files actually use.

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <time.h>

using String = std::string;

static inline uint32_t esp_random() { return (uint32_t)rand(); }

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

static inline uint32_t millis()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000UL + ts.tv_nsec / 1000000UL);
}

static inline void delay(uint32_t ms)
{
    struct timespec ts = { (time_t)(ms / 1000), (long)((ms % 1000) * 1000000L) };
    nanosleep(&ts, nullptr);
}

struct _SerialStub {
    void begin(int) {}
    void print(const char *s)   { fputs(s, stdout); }
    void println(const char *s) { puts(s); }
    void println()              { putchar('\n'); }
    template<typename... Args>
    void printf(const char *fmt, Args... args) { ::printf(fmt, args...); }
};
extern _SerialStub Serial;
