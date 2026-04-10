#include <Arduino.h>
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <lvgl.h>
#include "config.h"
#include "ota_update.h"
#include "helpers/fw_download.h"
#include "um_nav.h"
#include "um_shared.h"
#include "helpers/um_storage.h"

// Defaults — settings screen writes these at runtime
volatile uint32_t   um_sleep_timeout_ms = UM_DEFAULT_SLEEP_TIMEOUT_MS;
volatile uint32_t   um_dim_timeout_ms   = UM_DEFAULT_DIM_TIMEOUT_MS;
volatile uint8_t    um_dim_brightness   = UM_DEFAULT_DIM_BRIGHTNESS;
volatile um_theme_t um_active_theme     = UM_THEME_DARK;
volatile bool       um_time_synced      = false;  // set true when network time is received
char                um_msg_server_name[UM_MSG_SERVER_NAME_LEN] = {};  // set when msg-server ident received
volatile uint32_t   um_unread_count     = 0;      // direct pager messages not yet read

// -------------------------------------------------------
// Arduino entry points
// -------------------------------------------------------
void setup()
{
    Serial.begin(UM_SERIAL_BAUD);

    instance.begin(NO_INIT_FATFS);
    beginLvglHelper(instance);

    // Mount SD card and create standard directory tree
    um_storage_init();

    // Workaround: LV_Helper_v9 creates the default group AFTER
    // registering indevs, so encoder/keyboard are assigned to NULL group.
    lv_group_t *g = lv_group_get_default();
    if (g) {
        lv_indev_t *indev = NULL;
        while ((indev = lv_indev_get_next(indev)) != NULL) {
            lv_indev_set_group(indev, g);
        }
    }

    // Load persisted settings before the UI reads any of these values
    um_settings_load();

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
        delay(UM_OTA_LOOP_DELAY_MS);
        return;
    }

    if (um_fwDownloadRequested) {
        static bool fwStarted = false;
        if (!fwStarted) {
            fwStarted = true;
            startFwDownload(um_fw_widgets.bar,
                            um_fw_widgets.status_lbl,
                            um_fw_widgets.close_btn);
            // startFwDownload returns on error (or success).
            // The overlay + close_btn are still alive on screen.
            um_fwDownloadRequested = false;
            fwStarted = false;
        }
        // Keep running LVGL so the overlay and close/restart button remain interactive
        instance.loop();
        lv_timer_handler();
        delay(UM_MAIN_LOOP_DELAY_MS);
        return;
    }

    instance.loop();
    lv_timer_handler();

    // ---- Dim / sleep on inactivity ----
    {
        static bool    s_dimmed            = false;
        static uint8_t s_saved_brightness  = DEVICE_MAX_BRIGHTNESS_LEVEL;
        static uint8_t s_saved_kb_brightness = UM_DEFAULT_KB_BRIGHTNESS;

        uint32_t inactive = lv_display_get_inactive_time(NULL);

        // Dim
        if (um_dim_timeout_ms > 0) {
            if (!s_dimmed && inactive >= um_dim_timeout_ms) {
                s_saved_brightness    = instance.getBrightness();
                s_saved_kb_brightness = instance.kb.getBrightness();
                instance.setBrightness(um_dim_brightness);
                instance.kb.setBrightness(um_dim_brightness);
                s_dimmed = true;
            } else if (s_dimmed && inactive < um_dim_timeout_ms) {
                instance.setBrightness(s_saved_brightness);
                instance.kb.setBrightness(s_saved_kb_brightness);
                s_dimmed = false;
            }
        }

        // Sleep
        if (um_sleep_timeout_ms > 0 && inactive >= um_sleep_timeout_ms) {
            if (s_dimmed) {
                instance.setBrightness(s_saved_brightness);
                instance.kb.setBrightness(s_saved_kb_brightness);
            }
            s_dimmed = false;
            instance.sleep((WakeupSource_t)(WAKEUP_SRC_BOOT_BUTTON | WAKEUP_SRC_ROTARY_BUTTON));
        }
    }

    delay(UM_MAIN_LOOP_DELAY_MS);
}
