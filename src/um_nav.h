#pragma once
// um_nav.h — simple screen navigation for UniversalMesh Pager
// Each screen module exposes um_xxx_create() and um_xxx_destroy().
// Screens get their own lv_obj on lv_scr_act(); nav deletes the old one.

typedef enum {
    UM_SCREEN_MENU = 0,
    UM_SCREEN_MESH,
    UM_SCREEN_MESSAGES,
    UM_SCREEN_SETTINGS,
    UM_SCREEN_HELP,
    UM_SCREEN_LORA,
    UM_SCREEN_INFO,
    UM_SCREEN_SD,
} UMScreen;

// Forward declarations — implemented in each .cpp
void um_welcome_create();
void um_welcome_destroy();
void um_menu_create();
void um_menu_destroy();
void um_messages_create();
void um_messages_destroy();
void um_mesh_create();
void um_mesh_destroy();
void um_settings_create();
void um_settings_destroy();
void um_help_create();
void um_help_destroy();
void um_lora_create();
void um_lora_destroy();
void um_info_create();
void um_info_destroy();
void um_sd_create();
void um_sd_destroy();

// -------------------------------------------------------
// Navigator — go to a screen, or go back to menu
// -------------------------------------------------------
static UMScreen um_current_screen = UM_SCREEN_MENU;

static void um_nav_destroy_current()
{
    switch (um_current_screen) {
        case UM_SCREEN_MENU:     um_menu_destroy();     break;
        case UM_SCREEN_MESH:     um_mesh_destroy();     break;
        case UM_SCREEN_MESSAGES: um_messages_destroy(); break;
        case UM_SCREEN_SETTINGS: um_settings_destroy(); break;
        case UM_SCREEN_HELP:     um_help_destroy();     break;
        case UM_SCREEN_LORA:     um_lora_destroy();     break;
        case UM_SCREEN_INFO:     um_info_destroy();     break;
        case UM_SCREEN_SD:       um_sd_destroy();       break;
    }
}

static void um_nav_go(UMScreen screen)
{
    um_nav_destroy_current();
    um_current_screen = screen;
    switch (screen) {
        case UM_SCREEN_MENU:     um_menu_create();     break;
        case UM_SCREEN_MESH:     um_mesh_create();     break;
        case UM_SCREEN_MESSAGES: um_messages_create(); break;
        case UM_SCREEN_SETTINGS: um_settings_create(); break;
        case UM_SCREEN_HELP:     um_help_create();     break;
        case UM_SCREEN_LORA:     um_lora_create();     break;
        case UM_SCREEN_INFO:     um_info_create();     break;
        case UM_SCREEN_SD:       um_sd_create();       break;
    }
}

static inline void um_nav_back() { um_nav_go(UM_SCREEN_MENU); }
