#pragma once
// um_nav.h — simple screen navigation for UniversalMesh Pager
// Each screen module exposes um_xxx_create() and um_xxx_destroy().
// Screens get their own lv_obj on lv_scr_act(); nav deletes the old one.

typedef enum {
    UM_SCREEN_MENU = 0,
    UM_SCREEN_MESH,
    UM_SCREEN_MESSAGES,
    UM_SCREEN_NFC,
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
void um_nfc_create();
void um_nfc_destroy();
void um_nfc_loop();    // call from Arduino loop() while NFC screen is active
void um_sd_create();
void um_sd_destroy();

// -------------------------------------------------------
// Navigator — shared state and functions (implemented in um_nav.cpp)
// -------------------------------------------------------
extern UMScreen um_current_screen;
void um_nav_destroy_current();
void um_nav_go(UMScreen screen);
void um_nav_back();
