#include "um_nav.h"

// Shared navigation state across the whole firmware.
UMScreen um_current_screen = UM_SCREEN_MENU;

void um_nav_destroy_current()
{
    switch (um_current_screen) {
        case UM_SCREEN_MENU:     um_menu_destroy();     break;
        case UM_SCREEN_MESH:     um_mesh_destroy();     break;
        case UM_SCREEN_MESSAGES: um_messages_destroy(); break;
        case UM_SCREEN_NFC:      um_nfc_destroy();      break;
        case UM_SCREEN_SETTINGS: um_settings_destroy(); break;
        case UM_SCREEN_HELP:     um_help_destroy();     break;
        case UM_SCREEN_LORA:     um_lora_destroy();     break;
        case UM_SCREEN_INFO:     um_info_destroy();     break;
        case UM_SCREEN_SD:       um_sd_destroy();       break;
    }
}

void um_nav_go(UMScreen screen)
{
    um_nav_destroy_current();
    um_current_screen = screen;
    switch (screen) {
        case UM_SCREEN_MENU:     um_menu_create();     break;
        case UM_SCREEN_MESH:     um_mesh_create();     break;
        case UM_SCREEN_MESSAGES: um_messages_create(); break;
        case UM_SCREEN_NFC:      um_nfc_create();      break;
        case UM_SCREEN_SETTINGS: um_settings_create(); break;
        case UM_SCREEN_HELP:     um_help_create();     break;
        case UM_SCREEN_LORA:     um_lora_create();     break;
        case UM_SCREEN_INFO:     um_info_create();     break;
        case UM_SCREEN_SD:       um_sd_create();       break;
    }
}

void um_nav_back()
{
    um_nav_go(UM_SCREEN_MENU);
}
