#pragma once
// lock_callback_t was removed from USBMSC.h in Arduino ESP32 core 3.x
// LilyGoLib's USB_MSC.cpp still requires it
#ifndef lock_callback_t
typedef void (*lock_callback_t)(void);
#endif
