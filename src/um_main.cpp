#include <Arduino.h>
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <lvgl.h>
#include "ota_update.h"
#include "um_nav.h"
#include "um_shared.h"

// 60 s default; settings screen writes this at runtime
volatile uint32_t um_sleep_timeout_ms = 60000;

// -------------------------------------------------------
// Arduino entry points
// -------------------------------------------------------
void setup()
{
    Serial.begin(115200);

    instance.begin(NO_INIT_FATFS);
    beginLvglHelper(instance);

    // Workaround: LV_Helper_v9 creates the default group AFTER
    // registering indevs, so encoder/keyboard are assigned to NULL group.
    lv_group_t *g = lv_group_get_default();
    if (g) {
        lv_indev_t *indev = NULL;
        while ((indev = lv_indev_get_next(indev)) != NULL) {
            lv_indev_set_group(indev, g);
        }
    }

    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    // Show boot splash; it auto-advances to the menu after 3 s
    um_welcome_create();
    um_current_screen = UM_SCREEN_MENU; // nav state starts at menu (welcome handles its own timer)
}

void loop()
{
    if (um_otaRequested) {
        static bool otaStarted = false;
        if (!otaStarted) {
            otaStarted = true;
            startOtaUpdate();
        }
        delay(20);
        return;
    }

    instance.loop();
    lv_timer_handler();

    // ---- Dim / sleep on inactivity ----
    if (um_sleep_timeout_ms > 0) {
        static bool    s_dimmed           = false;
        static uint8_t s_saved_brightness = DEVICE_MAX_BRIGHTNESS_LEVEL;

        uint32_t inactive = lv_display_get_inactive_time(NULL);
        uint32_t dim_ms   = um_sleep_timeout_ms / 2;

        if (!s_dimmed && inactive >= dim_ms) {
            s_saved_brightness = instance.getBrightness();
            instance.setBrightness(8);
            s_dimmed = true;
        } else if (s_dimmed && inactive < dim_ms) {
            // user interacted — restore brightness
            instance.setBrightness(s_saved_brightness);
            s_dimmed = false;
        }

        if (inactive >= um_sleep_timeout_ms) {
            instance.setBrightness(s_saved_brightness);
            s_dimmed = false;
            instance.sleep((WakeupSource_t)(WAKEUP_SRC_BOOT_BUTTON | WAKEUP_SRC_ROTARY_BUTTON));
        }
    }

    delay(2);
}
