// Desktop SDL2 simulator entry point for the UniversalMesh Pager UI.
// Initialises LVGL + SDL2 at 222×480 (matching the real hardware display),
// creates a keyboard indev for navigation, then boots the normal UI stack.

#include <SDL.h>
#include <lvgl.h>
#include <signal.h>
#include "um_nav.h"

// --- stub definitions required by the stubs headers ------
#include "Arduino.h"
_SerialStub Serial;

// --- shared runtime variables (defined in um_main.cpp on hardware) --------
#include "config.h"
volatile bool       um_time_synced         = false;
char                um_msg_server_name[UM_MSG_SERVER_NAME_LEN] = {};

// --- quit flag -------------------------------------------
static volatile bool g_quit = false;
static void on_signal(int) { g_quit = true; }

// --- display dimensions (match real hardware) ------------
#define SIM_W 480
#define SIM_H 222

int main(int argc, char *argv[])
{
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    // ---- LVGL init ----
    lv_init();

    // ---- SDL2 display (222×480) ----
    lv_display_t *disp = lv_sdl_window_create(SIM_W, SIM_H);
    lv_display_set_default(disp);

    // ---- Input devices ----
    lv_indev_t *mouse      = lv_sdl_mouse_create();
    lv_indev_t *mousewheel = lv_sdl_mousewheel_create();
    lv_indev_t *kb         = lv_sdl_keyboard_create();

    // ---- Default group (receives keyboard / scroll events) ----
    lv_group_t *g = lv_group_create();
    lv_group_set_default(g);
    lv_indev_set_group(kb,         g);
    lv_indev_set_group(mousewheel, g);

    // ---- Boot the UI (same flow as the real hardware) ----
    // welcome screen auto-advances to menu after 3 s
    um_welcome_create();
    um_current_screen = UM_SCREEN_MENU;

    // ---- Main loop ----
    while (!g_quit) {
        lv_timer_handler();

        // Check for SDL_QUIT (window close button) without racing
        // with LVGL's own event processing
        SDL_PumpEvents();
        SDL_Event e;
        if (SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_QUIT, SDL_QUIT) > 0) break;

        SDL_Delay(5);
    }

    lv_deinit();
    SDL_Quit();
    return 0;
}
