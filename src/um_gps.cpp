
#include <Arduino.h>
#ifndef SIM_BUILD
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <TinyGPS++.h>
#endif
#include <lvgl.h>
#include "um_nav.h"
#include "um_theme.h"
#include "um_shared.h"
#include "helpers/um_haptic.h"

// -------------------------------------------------------
// State
// -------------------------------------------------------
static lv_obj_t   *gps_root     = NULL;
static lv_obj_t   *gps_fix_dot  = NULL;
static lv_obj_t   *gps_fix_lbl  = NULL;
static lv_obj_t   *gps_sat_lbl  = NULL;
static lv_obj_t   *gps_lat_lbl  = NULL;
static lv_obj_t   *gps_lon_lbl  = NULL;
static lv_obj_t   *gps_alt_lbl  = NULL;
static lv_obj_t   *gps_spd_lbl  = NULL;
static lv_obj_t   *gps_crs_lbl  = NULL;
static lv_obj_t   *gps_hdop_lbl = NULL;
static lv_obj_t   *gps_date_lbl = NULL;
static lv_obj_t   *gps_time_lbl = NULL;
static lv_obj_t   *gps_chars_lbl = NULL;
static lv_timer_t *gps_timer    = NULL;
static bool        gps_started  = false;

#define GPS_POLL_MS 1000

// -------------------------------------------------------
// Helper: data row — label on left, value label returned
// -------------------------------------------------------
static lv_obj_t *gps_make_row(lv_obj_t *parent, const char *caption)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(row, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(row, 3, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *cap = lv_label_create(row);
    lv_label_set_text(cap, caption);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(cap, um_col_text_dim(), LV_PART_MAIN);
    lv_obj_set_width(cap, 68);

    lv_obj_t *val = lv_label_create(row);
    lv_label_set_text(val, "\xe2\x80\x94");   // em dash placeholder
    lv_obj_set_style_text_font(val, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(val, um_col_text(), LV_PART_MAIN);
    lv_obj_set_flex_grow(val, 1);
    return val;
}

// -------------------------------------------------------
// Timer — poll GPS and update labels
// -------------------------------------------------------
static void gps_timer_cb(lv_timer_t *)
{
#ifndef SIM_BUILD
    bool fix  = instance.gps.location.isValid();
    bool hasSat = instance.gps.satellites.isValid();

    // Fix dot colour
    if (gps_fix_dot)
        lv_obj_set_style_bg_color(gps_fix_dot,
            fix ? um_col_ok() : um_col_err(), LV_PART_MAIN);

    if (gps_fix_lbl)
        lv_label_set_text(gps_fix_lbl, fix ? "FIX" : "NO FIX");
    if (gps_fix_lbl)
        lv_obj_set_style_text_color(gps_fix_lbl,
            fix ? um_col_ok() : um_col_err(), LV_PART_MAIN);

    if (gps_sat_lbl) {
        if (hasSat)
            lv_label_set_text_fmt(gps_sat_lbl, "%u sats", instance.gps.satellites.value());
        else
            lv_label_set_text(gps_sat_lbl, "— sats");
    }

    // Coordinates
    if (gps_lat_lbl) {
        if (fix) lv_label_set_text_fmt(gps_lat_lbl, "%.6f\xc2\xb0", instance.gps.location.lat());
        else     lv_label_set_text(gps_lat_lbl, "\xe2\x80\x94");
    }
    if (gps_lon_lbl) {
        if (fix) lv_label_set_text_fmt(gps_lon_lbl, "%.6f\xc2\xb0", instance.gps.location.lng());
        else     lv_label_set_text(gps_lon_lbl, "\xe2\x80\x94");
    }

    // Altitude
    if (gps_alt_lbl) {
        if (instance.gps.altitude.isValid())
            lv_label_set_text_fmt(gps_alt_lbl, "%.1f m", instance.gps.altitude.meters());
        else
            lv_label_set_text(gps_alt_lbl, "\xe2\x80\x94");
    }

    // Speed
    if (gps_spd_lbl) {
        if (instance.gps.speed.isValid())
            lv_label_set_text_fmt(gps_spd_lbl, "%.1f km/h", instance.gps.speed.kmph());
        else
            lv_label_set_text(gps_spd_lbl, "\xe2\x80\x94");
    }

    // Course
    if (gps_crs_lbl) {
        if (instance.gps.course.isValid())
            lv_label_set_text_fmt(gps_crs_lbl, "%.1f\xc2\xb0 %s",
                                  instance.gps.course.deg(),
                                  TinyGPSPlus::cardinal(instance.gps.course.deg()));
        else
            lv_label_set_text(gps_crs_lbl, "\xe2\x80\x94");
    }

    // HDOP
    if (gps_hdop_lbl) {
        if (instance.gps.hdop.isValid())
            lv_label_set_text_fmt(gps_hdop_lbl, "%.2f", instance.gps.hdop.hdop());
        else
            lv_label_set_text(gps_hdop_lbl, "\xe2\x80\x94");
    }

    // Date / time
    if (gps_date_lbl) {
        if (instance.gps.date.isValid())
            lv_label_set_text_fmt(gps_date_lbl, "%04u-%02u-%02u",
                                  instance.gps.date.year(),
                                  instance.gps.date.month(),
                                  instance.gps.date.day());
        else
            lv_label_set_text(gps_date_lbl, "\xe2\x80\x94");
    }
    if (gps_time_lbl) {
        if (instance.gps.time.isValid())
            lv_label_set_text_fmt(gps_time_lbl, "%02u:%02u:%02u UTC",
                                  instance.gps.time.hour(),
                                  instance.gps.time.minute(),
                                  instance.gps.time.second());
        else
            lv_label_set_text(gps_time_lbl, "\xe2\x80\x94");
    }

    // Raw chars received
    if (gps_chars_lbl)
        lv_label_set_text_fmt(gps_chars_lbl, "%lu chars", instance.gps.charsProcessed());

#else
    // Simulator placeholder
    if (gps_fix_dot)  lv_obj_set_style_bg_color(gps_fix_dot, um_col_text_inactive(), LV_PART_MAIN);
    if (gps_fix_lbl)  { lv_label_set_text(gps_fix_lbl, "SIM"); lv_obj_set_style_text_color(gps_fix_lbl, um_col_text_dim(), LV_PART_MAIN); }
    if (gps_sat_lbl)  lv_label_set_text(gps_sat_lbl,  "— sats");
    if (gps_lat_lbl)  lv_label_set_text(gps_lat_lbl,  "\xe2\x80\x94");
    if (gps_lon_lbl)  lv_label_set_text(gps_lon_lbl,  "\xe2\x80\x94");
    if (gps_alt_lbl)  lv_label_set_text(gps_alt_lbl,  "\xe2\x80\x94");
    if (gps_spd_lbl)  lv_label_set_text(gps_spd_lbl,  "\xe2\x80\x94");
    if (gps_crs_lbl)  lv_label_set_text(gps_crs_lbl,  "\xe2\x80\x94");
    if (gps_hdop_lbl) lv_label_set_text(gps_hdop_lbl, "\xe2\x80\x94");
    if (gps_date_lbl) lv_label_set_text(gps_date_lbl, "\xe2\x80\x94");
    if (gps_time_lbl) lv_label_set_text(gps_time_lbl, "\xe2\x80\x94");
    if (gps_chars_lbl)lv_label_set_text(gps_chars_lbl,"— chars");
#endif
}

// -------------------------------------------------------
// Create / destroy
// -------------------------------------------------------
void um_gps_create()
{
    // Root
    gps_root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(gps_root, lv_pct(100), lv_pct(100));
    lv_obj_center(gps_root);
    lv_obj_set_style_bg_color(gps_root, um_col_bg(), LV_PART_MAIN);
    lv_obj_set_style_border_width(gps_root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(gps_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(gps_root, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(gps_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(gps_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(gps_root, LV_OBJ_FLAG_SCROLLABLE);

    // ---- Header ----
    lv_obj_t *hdr = lv_obj_create(gps_root);
    lv_obj_set_width(hdr, lv_pct(100));
    lv_obj_set_height(hdr, 36);
    lv_obj_set_style_bg_color(hdr, um_col_surface(), LV_PART_MAIN);
    lv_obj_set_style_border_width(hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(hdr, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(hdr, 6, LV_PART_MAIN);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Home button
    lv_obj_t *home_btn = lv_btn_create(hdr);
    lv_obj_set_size(home_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(home_btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(home_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(home_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(home_btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(home_btn, [](lv_event_t *) {
        um_haptic_select();
        um_nav_back();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *home_lbl = lv_label_create(home_btn);
    lv_label_set_text(home_lbl, LV_SYMBOL_HOME);
    lv_obj_set_style_text_color(home_lbl, um_col_text_dim(), LV_PART_MAIN);

    // Title
    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, UM_SYMBOL_GPS "  GPS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, um_accent_gps(), LV_PART_MAIN);
    lv_obj_set_style_pad_left(title, 8, LV_PART_MAIN);
    lv_obj_set_flex_grow(title, 1);

    // Sat count in topbar
    gps_sat_lbl = lv_label_create(hdr);
    lv_label_set_text(gps_sat_lbl, "— sats");
    lv_obj_set_style_text_font(gps_sat_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(gps_sat_lbl, um_col_text_dim(), LV_PART_MAIN);

    // Accent separator line
    lv_obj_t *sep = lv_obj_create(gps_root);
    lv_obj_set_width(sep, lv_pct(100));
    lv_obj_set_height(sep, 2);
    lv_obj_set_style_bg_color(sep, um_accent_gps(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sep, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_border_width(sep, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sep, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(sep, 0, LV_PART_MAIN);

    // ---- Fix status strip ----
    lv_obj_t *status = lv_obj_create(gps_root);
    lv_obj_set_width(status, lv_pct(100));
    lv_obj_set_height(status, 30);
    lv_obj_set_style_bg_color(status, um_col_surface(), LV_PART_MAIN);
    lv_obj_set_style_border_width(status, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(status, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(status, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(status, 0, LV_PART_MAIN);
    lv_obj_clear_flag(status, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status, 8, LV_PART_MAIN);

    gps_fix_dot = lv_obj_create(status);
    lv_obj_set_size(gps_fix_dot, 10, 10);
    lv_obj_set_style_radius(gps_fix_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(gps_fix_dot, um_col_text_inactive(), LV_PART_MAIN);
    lv_obj_set_style_border_width(gps_fix_dot, 0, LV_PART_MAIN);

    gps_fix_lbl = lv_label_create(status);
    lv_label_set_text(gps_fix_lbl, "NO FIX");
    lv_obj_set_style_text_font(gps_fix_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(gps_fix_lbl, um_col_err(), LV_PART_MAIN);
    lv_obj_set_flex_grow(gps_fix_lbl, 1);

    gps_chars_lbl = lv_label_create(status);
    lv_label_set_text(gps_chars_lbl, "— chars");
    lv_obj_set_style_text_font(gps_chars_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(gps_chars_lbl, um_col_text_hint(), LV_PART_MAIN);

    // ---- Two-column data grid ----
    lv_obj_t *grid = lv_obj_create(gps_root);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(grid, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(grid, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_column(grid, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(grid, 0, LV_PART_MAIN);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // Left column
    lv_obj_t *col_l = lv_obj_create(grid);
    lv_obj_set_flex_grow(col_l, 1);
    lv_obj_set_height(col_l, lv_pct(100));
    lv_obj_set_style_bg_opa(col_l, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(col_l, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(col_l, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(col_l, 0, LV_PART_MAIN);
    lv_obj_clear_flag(col_l, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(col_l, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col_l, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // Right column
    lv_obj_t *col_r = lv_obj_create(grid);
    lv_obj_set_flex_grow(col_r, 1);
    lv_obj_set_height(col_r, lv_pct(100));
    lv_obj_set_style_bg_opa(col_r, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(col_r, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(col_r, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(col_r, 0, LV_PART_MAIN);
    lv_obj_clear_flag(col_r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(col_r, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col_r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // Left: Latitude, Longitude, Altitude, Speed, Course
    gps_lat_lbl = gps_make_row(col_l, "Latitude");
    gps_lon_lbl = gps_make_row(col_l, "Longitude");
    gps_alt_lbl = gps_make_row(col_l, "Altitude");
    gps_spd_lbl = gps_make_row(col_l, "Speed");
    gps_crs_lbl = gps_make_row(col_l, "Course");

    // Right: HDOP, Date, Time
    gps_hdop_lbl = gps_make_row(col_r, "HDOP");
    gps_date_lbl = gps_make_row(col_r, "Date");
    gps_time_lbl = gps_make_row(col_r, "Time UTC");

    // Init GPS hardware on first open
#ifndef SIM_BUILD
    if (!gps_started) {
        gps_started = instance.initGPS();
    }
#endif

    // Focus group
    lv_group_t *g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, home_btn);
        lv_group_focus_obj(home_btn);
    }

    // Start polling timer — run once immediately then every second
    gps_timer = lv_timer_create(gps_timer_cb, GPS_POLL_MS, NULL);
    lv_timer_ready(gps_timer);
}

void um_gps_destroy()
{
    if (!gps_root) return;

    if (gps_timer) { lv_timer_del(gps_timer); gps_timer = NULL; }

    lv_group_t *g = lv_group_get_default();
    if (g) lv_group_remove_all_objs(g);

    lv_obj_del(gps_root);
    gps_root      = NULL;
    gps_fix_dot   = NULL;
    gps_fix_lbl   = NULL;
    gps_sat_lbl   = NULL;
    gps_lat_lbl   = NULL;
    gps_lon_lbl   = NULL;
    gps_alt_lbl   = NULL;
    gps_spd_lbl   = NULL;
    gps_crs_lbl   = NULL;
    gps_hdop_lbl  = NULL;
    gps_date_lbl  = NULL;
    gps_time_lbl  = NULL;
    gps_chars_lbl = NULL;
}
