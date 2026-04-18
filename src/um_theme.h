#pragma once
// ============================================================
// um_theme.h — dark / light colour palette
// ============================================================
// Include this header in every screen file.  All colour
// functions check um_active_theme at draw time, so simply
// switching the variable and rebuilding the screen is enough
// to change the entire look of the UI.
// ============================================================

#include <lvgl.h>

typedef enum { UM_THEME_DARK = 0, UM_THEME_LIGHT = 1 } um_theme_t;

// Defined in um_main.cpp (hardware) / sim/um_mesh_stub.cpp (simulator)
extern volatile um_theme_t um_active_theme;

static inline bool _um_dark() { return um_active_theme == UM_THEME_DARK; }

// Convenience macro for one-off colours not worth a named function
#define UM_COL(dr,dg,db, lr,lg,lb) \
    (_um_dark() ? lv_color_make(dr,dg,db) : lv_color_make(lr,lg,lb))

// -------------------------------------------------------
// Backgrounds
// -------------------------------------------------------
static inline lv_color_t um_col_bg()
    { return _um_dark() ? lv_color_make(  4,  6, 10) : lv_color_make(240,242,248); }

static inline lv_color_t um_col_surface()
    { return _um_dark() ? lv_color_make( 14, 18, 28) : lv_color_make(226,228,240); }

static inline lv_color_t um_col_surface_tile()
    { return _um_dark() ? lv_color_make( 10, 12, 18) : lv_color_make(210,213,232); }

static inline lv_color_t um_col_surface_tile_focus()
    { return _um_dark() ? lv_color_make( 18, 28, 38) : lv_color_make(210,215,235); }

static inline lv_color_t um_col_surface_deep()
    { return _um_dark() ? lv_color_make(  8,  8,  8) : lv_color_make(218,220,235); }

// -------------------------------------------------------
// Dividers & borders
// -------------------------------------------------------
static inline lv_color_t um_col_divider()
    { return _um_dark() ? lv_color_make( 40, 40, 55) : lv_color_make(200,202,218); }

static inline lv_color_t um_col_border()
    { return _um_dark() ? lv_color_make( 50, 50, 65) : lv_color_make(172,175,198); }

static inline lv_color_t um_col_border_focus()
    { return _um_dark() ? lv_color_make(  0,130,160) : lv_color_make(  0, 90,145); }

// Thin accent separator line (between topbar and content area)
static inline lv_color_t um_col_accent_line()
    { return _um_dark() ? lv_color_make(  0, 60, 90) : lv_color_make(  0, 90,145); }

// -------------------------------------------------------
// Text
// -------------------------------------------------------
static inline lv_color_t um_col_text()
    { return _um_dark() ? lv_color_make(220,220,230) : lv_color_make( 15, 15, 25); }

static inline lv_color_t um_col_text_sub()
    { return _um_dark() ? lv_color_make(190,190,205) : lv_color_make( 50, 55, 75); }

// Buttons, subtitles, secondary labels
static inline lv_color_t um_col_text_dim()
    { return _um_dark() ? lv_color_make(148,150,165) : lv_color_make(105,110,130); }

// Hint bar, node name, placeholder text
static inline lv_color_t um_col_text_hint()
    { return _um_dark() ? lv_color_make(185,187,200) : lv_color_make( 65, 67, 85); }

// Inactive icons / greyed-out indicators (visible neutral; colored when active)
static inline lv_color_t um_col_text_inactive()
    { return _um_dark() ? lv_color_make(170,172,188) : lv_color_make( 80, 82,100); }

// -------------------------------------------------------
// Accent colours — adjusted for legibility on each theme
// -------------------------------------------------------
static inline lv_color_t um_col_cyan()
    { return _um_dark() ? lv_color_make(  0,178,218) : lv_color_make(  0,125,172); }

static inline lv_color_t um_col_cyan_bright()
    { return _um_dark() ? lv_color_make(  0,210,255) : lv_color_make(  0,148,205); }

static inline lv_color_t um_col_green()
    { return _um_dark() ? lv_color_make(  0,200,  0) : lv_color_make(  0,152,  0); }

static inline lv_color_t um_col_green_bright()
    { return _um_dark() ? lv_color_make(  0,230,120) : lv_color_make(  0,172, 80); }

static inline lv_color_t um_col_orange()
    { return _um_dark() ? lv_color_make(255,120,  0) : lv_color_make(198, 85,  0); }

static inline lv_color_t um_col_yellow()
    { return _um_dark() ? lv_color_make(200,160,  0) : lv_color_make(155,118,  0); }

static inline lv_color_t um_col_red()
    { return _um_dark() ? lv_color_make(220, 50, 50) : lv_color_make(185, 25, 25); }

static inline lv_color_t um_col_purple()
    { return _um_dark() ? lv_color_make(120, 80,220) : lv_color_make( 88, 52,185); }

// -------------------------------------------------------
// Status colours
// -------------------------------------------------------
static inline lv_color_t um_col_ok()
    { return _um_dark() ? lv_color_make(  0,218,  0) : lv_color_make(  0,162,  0); }

static inline lv_color_t um_col_warn()
    { return _um_dark() ? lv_color_make(255,160,  0) : lv_color_make(192,118,  0); }

static inline lv_color_t um_col_err()
    { return _um_dark() ? lv_color_make(255, 60, 60) : lv_color_make(195, 35, 35); }

// -------------------------------------------------------
// Focus backgrounds (for focused list rows / action buttons)
// -------------------------------------------------------
static inline lv_color_t um_col_focus_green()
    { return _um_dark() ? lv_color_make(  0, 70, 10) : lv_color_make(188,238,205); }

static inline lv_color_t um_col_focus_cyan()
    { return _um_dark() ? lv_color_make(  0, 60,100) : lv_color_make(188,220,242); }

static inline lv_color_t um_col_focus_red()
    { return _um_dark() ? lv_color_make( 80, 28, 28) : lv_color_make(242,195,195); }

// -------------------------------------------------------
// Scrollbar
// -------------------------------------------------------
static inline lv_color_t um_col_scrollbar()
    { return _um_dark() ? lv_color_make(  0,118,  0) : lv_color_make(  0,158, 75); }
