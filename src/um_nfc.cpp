#include <Arduino.h>
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <lvgl.h>
#include "um_nav.h"
#include "um_shared.h"

#include "helpers/um_haptic.h"

// -------------------------------------------------------
// NFC hardware — only on real device, not simulator
// -------------------------------------------------------
#ifndef SIM_BUILD
#include "nfc_include.h"
extern RfalNfcClass NFCReader;

enum NfcScreenState { NFC_IDLE, NFC_SCANNING, NFC_CARD_FOUND, NFC_WAIT_RESCAN };
static NfcScreenState  nfc_screen_state = NFC_IDLE;
static unsigned long   nfc_found_at_ms  = 0;

static char nfc_uid_str[68]     = "--";
static char nfc_type_str[48]    = "--";
static char nfc_product_str[48] = "--";
static char nfc_detail1_str[72] = "--";
static char nfc_detail2_str[72] = "--";
static char nfc_detail3_str[72] = "--";

static const char* nfc_mifare_name(uint8_t sak)
{
    switch (sak & 0x7F) {
        case 0x00: return "NTAG / Ultralight";
        case 0x08: return "Mifare Classic 1K";
        case 0x09: return "Mifare Mini";
        case 0x10: return "Mifare Plus 2K (SL2)";
        case 0x11: return "Mifare Plus 4K (SL2)";
        case 0x18: return "Mifare Classic 4K";
        case 0x20: return "Mifare DESFire / ISO-DEP";
        case 0x28: return "Mifare Classic 4K (MF3D12)";
        case 0x38: return "Mifare Plus 4K (SL3)";
        case 0x40: return "NFC-DEP / P2P";
        case 0x60: return "Mifare Plus SL3";
        default:   return "Unknown";
    }
}

// Called synchronously from rfalNfcWorker() when a card is activated
static void nfc_on_state(rfalNfcState st)
{
    Serial.printf("[NFC] state callback: st=%d\n", (int)st);

    if (st != RFAL_NFC_STATE_ACTIVATED) return;

    rfalNfcDevice *dev = nullptr;
    NFCReader.rfalNfcGetActiveDevice(&dev);
    if (!dev) {
        Serial.println("[NFC] ERROR: getActiveDevice returned null");
        return;
    }

    // Generic UID
    nfc_uid_str[0] = '\0';
    for (int i = 0; i < dev->nfcidLen; i++) {
        char hex[5];
        snprintf(hex, sizeof(hex), i ? ":%02X" : "%02X", dev->nfcid[i]);
        strncat(nfc_uid_str, hex, sizeof(nfc_uid_str) - strlen(nfc_uid_str) - 1);
    }

    Serial.printf("[NFC] Card found! type=%d  uid=%s\n", (int)dev->type, nfc_uid_str);

    nfc_product_str[0] = '\0';
    nfc_detail1_str[0] = '\0';
    nfc_detail2_str[0] = '\0';
    nfc_detail3_str[0] = '\0';

    switch (dev->type) {
        case RFAL_NFC_LISTEN_TYPE_NFCA: {
            strncpy(nfc_type_str, "NFC-A  (ISO 14443-A)", sizeof(nfc_type_str) - 1);
            uint8_t atqa0 = dev->dev.nfca.sensRes.anticollisionInfo;
            uint8_t atqa1 = dev->dev.nfca.sensRes.platformInfo;
            uint8_t sak   = dev->dev.nfca.selRes.sak;
            snprintf(nfc_detail1_str, sizeof(nfc_detail1_str),
                     "ATQA: %02X %02X", atqa0, atqa1);
            snprintf(nfc_detail2_str, sizeof(nfc_detail2_str),
                     "SAK: 0x%02X", sak);
            strncpy(nfc_product_str, nfc_mifare_name(sak), sizeof(nfc_product_str) - 1);
            Serial.printf("[NFC] NFC-A  ATQA=%02X%02X  SAK=%02X  product=%s\n",
                          atqa0, atqa1, sak, nfc_product_str);
            break;
        }
        case RFAL_NFC_LISTEN_TYPE_NFCB: {
            strncpy(nfc_type_str, "NFC-B  (ISO 14443-B)", sizeof(nfc_type_str) - 1);
            uint8_t *pupi = dev->dev.nfcb.sensbRes.nfcid0;
            snprintf(nfc_detail1_str, sizeof(nfc_detail1_str),
                     "PUPI: %02X:%02X:%02X:%02X",
                     pupi[0], pupi[1], pupi[2], pupi[3]);
            snprintf(nfc_detail2_str, sizeof(nfc_detail2_str),
                     "AFI: 0x%02X", dev->dev.nfcb.sensbRes.appData.AFI);
            snprintf(nfc_detail3_str, sizeof(nfc_detail3_str),
                     "Proto: BRC=%02X FsciPro=%02X FWI=%02X",
                     dev->dev.nfcb.sensbRes.protInfo.BRC,
                     dev->dev.nfcb.sensbRes.protInfo.FsciProType,
                     dev->dev.nfcb.sensbRes.protInfo.FwiAdcFo);
            Serial.printf("[NFC] NFC-B  PUPI=%02X:%02X:%02X:%02X\n",
                          pupi[0], pupi[1], pupi[2], pupi[3]);
            break;
        }
        case RFAL_NFC_LISTEN_TYPE_NFCF: {
            strncpy(nfc_type_str, "NFC-F  (FeliCa / T3T)", sizeof(nfc_type_str) - 1);
            uint8_t *idm = dev->dev.nfcf.sensfRes.NFCID2;
            snprintf(nfc_detail1_str, sizeof(nfc_detail1_str),
                     "IDm: %02X%02X%02X%02X%02X%02X%02X%02X",
                     idm[0],idm[1],idm[2],idm[3],
                     idm[4],idm[5],idm[6],idm[7]);
            snprintf(nfc_detail2_str, sizeof(nfc_detail2_str),
                     "PAD0: %02X %02X",
                     dev->dev.nfcf.sensfRes.PAD0[0],
                     dev->dev.nfcf.sensfRes.PAD0[1]);
            snprintf(nfc_detail3_str, sizeof(nfc_detail3_str),
                     "PAD1: %02X %02X",
                     dev->dev.nfcf.sensfRes.PAD1[0],
                     dev->dev.nfcf.sensfRes.PAD1[1]);
            Serial.printf("[NFC] NFC-F  IDm=%s\n", nfc_detail1_str);
            break;
        }
        case RFAL_NFC_LISTEN_TYPE_NFCV: {
            strncpy(nfc_type_str, "NFC-V  (ISO 15693)", sizeof(nfc_type_str) - 1);
            uint8_t *uid = dev->dev.nfcv.InvRes.UID;
            snprintf(nfc_detail1_str, sizeof(nfc_detail1_str),
                     "UID: %02X%02X%02X%02X%02X%02X%02X%02X",
                     uid[7],uid[6],uid[5],uid[4],
                     uid[3],uid[2],uid[1],uid[0]);
            snprintf(nfc_detail2_str, sizeof(nfc_detail2_str),
                     "DSFID: 0x%02X", dev->dev.nfcv.InvRes.DSFID);
            snprintf(nfc_detail3_str, sizeof(nfc_detail3_str),
                     "Flags: 0x%02X", dev->dev.nfcv.InvRes.RES_FLAG);
            Serial.printf("[NFC] NFC-V  %s  DSFID=%02X\n",
                          nfc_detail1_str, dev->dev.nfcv.InvRes.DSFID);
            break;
        }
        case RFAL_NFC_LISTEN_TYPE_ST25TB:
            strncpy(nfc_type_str, "ST25TB  (ST proprietary)", sizeof(nfc_type_str) - 1);
            Serial.println("[NFC] ST25TB card");
            break;
        default:
            snprintf(nfc_type_str, sizeof(nfc_type_str),
                     "Unknown (type=%d)", (int)dev->type);
            Serial.printf("[NFC] Unknown type=%d\n", (int)dev->type);
            break;
    }

    NFCReader.rfalNfcDeactivate(false);
    if (dev->type == RFAL_NFC_LISTEN_TYPE_NFCA) {
        NFCReader.rfalNfcaPollerSleep();
    }

    nfc_screen_state = NFC_CARD_FOUND;
}

static ReturnCode nfc_start_discovery()
{
    rfalNfcDiscoverParam p = {};
    p.devLimit      = 1;
    p.techs2Find    = RFAL_NFC_POLL_TECH_A
                    | RFAL_NFC_POLL_TECH_B
                    | RFAL_NFC_POLL_TECH_V
                    | RFAL_NFC_POLL_TECH_F;
    p.totalDuration = 1000U;
    p.notifyCb      = nfc_on_state;
    p.wakeupEnabled = false;
    p.GBLen         = 0;    // no P2P general bytes; non-zero with GB=NULL triggers ST_ERR_PARAM
    p.nfcfBR        = RFAL_BR_212;  // required when POLL_TECH_F is set, else ST_ERR_PARAM
    ReturnCode err  = NFCReader.rfalNfcDiscover(&p);
    Serial.printf("[NFC] rfalNfcDiscover returned %d\n", (int)err);
    return err;
}
#endif // !SIM_BUILD

// -------------------------------------------------------
// LVGL widget pointers
// -------------------------------------------------------
static lv_obj_t   *nfc_root        = NULL;
static lv_obj_t   *nfc_status_lbl  = NULL;
static lv_obj_t   *nfc_uid_val     = NULL;
static lv_obj_t   *nfc_type_val    = NULL;
static lv_obj_t   *nfc_product_val = NULL;
static lv_obj_t   *nfc_detail1_val = NULL;
static lv_obj_t   *nfc_detail2_val = NULL;
static lv_obj_t   *nfc_detail3_val = NULL;
static lv_timer_t *nfc_ui_timer    = NULL;
static bool        nfc_hw_ok       = false;

// -------------------------------------------------------
// LVGL UI timer — 100 ms, only updates labels
// rfalNfcWorker() is driven from um_nfc_loop() in main loop
// -------------------------------------------------------
#ifndef SIM_BUILD
static void nfc_ui_cb(lv_timer_t *)
{
    switch (nfc_screen_state) {

        case NFC_CARD_FOUND:
            if (nfc_status_lbl) {
                lv_label_set_text(nfc_status_lbl, LV_SYMBOL_OK "  Card detected");
                lv_obj_set_style_text_color(nfc_status_lbl,
                    lv_color_make(0, 220, 100), LV_PART_MAIN);
            }
            if (nfc_uid_val)     lv_label_set_text(nfc_uid_val,     nfc_uid_str);
            if (nfc_type_val)    lv_label_set_text(nfc_type_val,    nfc_type_str);
            if (nfc_product_val) lv_label_set_text(nfc_product_val,
                                     nfc_product_str[0] ? nfc_product_str : "--");
            if (nfc_detail1_val) lv_label_set_text(nfc_detail1_val,
                                     nfc_detail1_str[0] ? nfc_detail1_str : "--");
            if (nfc_detail2_val) lv_label_set_text(nfc_detail2_val,
                                     nfc_detail2_str[0] ? nfc_detail2_str : "--");
            if (nfc_detail3_val) lv_label_set_text(nfc_detail3_val,
                                     nfc_detail3_str[0] ? nfc_detail3_str : "--");
            nfc_found_at_ms  = millis();
            nfc_screen_state = NFC_WAIT_RESCAN;
            break;

        case NFC_WAIT_RESCAN:
            if (millis() - nfc_found_at_ms > 4000UL) {
                if (nfc_status_lbl) {
                    lv_label_set_text(nfc_status_lbl,
                        LV_SYMBOL_REFRESH "  Scanning — hold card close");
                    lv_obj_set_style_text_color(nfc_status_lbl,
                        lv_color_make(0, 180, 200), LV_PART_MAIN);
                }
                Serial.println("[NFC] restarting discovery");
                nfc_screen_state = NFC_SCANNING;
                nfc_start_discovery();
            }
            break;

        default:
            break;
    }
}
#endif

// -------------------------------------------------------
// um_nfc_loop() — called from Arduino loop() every iteration
// Only active while the NFC screen is open (nfc_hw_ok gate)
// -------------------------------------------------------
void um_nfc_loop()
{
#ifndef SIM_BUILD
    if (!nfc_hw_ok) return;
    if (nfc_screen_state == NFC_SCANNING) {
        NFCReader.rfalNfcWorker();
    }
#endif
}

// -------------------------------------------------------
// UI helpers
// -------------------------------------------------------
static void nfc_key_cb(lv_event_t *e)
{
    uint32_t k = lv_event_get_key(e);
    if (k == LV_KEY_ESC || k == LV_KEY_BACKSPACE || k == LV_KEY_ENTER)
        um_nav_back();
}

static lv_obj_t* nfc_info_row(lv_obj_t *parent, lv_group_t *grp,
                               const char *symbol,
                               const char *key,
                               const char *value)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(row, um_col_focus_cyan(),
        (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER,
        (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(row, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(row, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, 6, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(row, [](lv_event_t *ev) {
        lv_obj_scroll_to_view(lv_event_get_target_obj(ev), LV_ANIM_ON);
        um_haptic_navigate();
    }, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(row, [](lv_event_t *e) {
        uint32_t k = lv_event_get_key(e);
        if (k == LV_KEY_ENTER) um_haptic_select();
        if (k == LV_KEY_ESC || k == LV_KEY_BACKSPACE) um_nav_back();
    }, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(row, [](lv_event_t *e) {
        um_haptic_select();
    }, LV_EVENT_CLICKED, NULL);
    if (grp) lv_group_add_obj(grp, row);

    lv_obj_t *sym = lv_label_create(row);
    lv_label_set_text(sym, symbol);
    lv_obj_set_style_text_font(sym, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(sym, um_col_text_hint(), LV_PART_MAIN);
    lv_obj_set_width(sym, 16);

    lv_obj_t *key_lbl = lv_label_create(row);
    lv_label_set_text(key_lbl, key);
    lv_obj_set_style_text_font(key_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(key_lbl, um_col_text_sub(), LV_PART_MAIN);
    lv_obj_set_flex_grow(key_lbl, 1);

    lv_obj_t *val_lbl = lv_label_create(row);
    lv_label_set_text(val_lbl, value);
    lv_obj_set_style_text_font(val_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(val_lbl, um_col_text(), LV_PART_MAIN);
    lv_obj_set_style_text_align(val_lbl, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_width(val_lbl, lv_pct(55));
    lv_label_set_long_mode(val_lbl, LV_LABEL_LONG_WRAP);
    return val_lbl;
}

static void nfc_section(lv_obj_t *parent, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_width(lbl, lv_pct(100));
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_make(0, 200, 160), LV_PART_MAIN);
    lv_obj_set_style_pad_top(lbl, 6, LV_PART_MAIN);
}

static void nfc_divider(lv_obj_t *parent)
{
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_set_size(d, lv_pct(100), 1);
    lv_obj_set_style_bg_color(d, um_col_divider(), LV_PART_MAIN);
    lv_obj_set_style_border_width(d, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(d, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(d, 0, LV_PART_MAIN);
}

// -------------------------------------------------------
// Screen create / destroy
// -------------------------------------------------------
void um_nfc_create()
{
    // Root — column, no scroll (matches um_messages)
    nfc_root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(nfc_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(nfc_root, um_col_bg(), LV_PART_MAIN);
    lv_obj_set_style_border_width(nfc_root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(nfc_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(nfc_root, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(nfc_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(nfc_root, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(nfc_root, LV_OBJ_FLAG_SCROLLABLE);

    // ---- Top bar (same as um_messages) ----
    lv_obj_t *hdr = lv_obj_create(nfc_root);
    lv_obj_set_width(hdr, lv_pct(100));
    lv_obj_set_height(hdr, 36);
    lv_obj_set_style_bg_color(hdr, um_col_surface(), LV_PART_MAIN);
    lv_obj_set_style_border_width(hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(hdr, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(hdr, 6, LV_PART_MAIN);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, LV_SYMBOL_LOOP "  NFC Reader");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, um_accent_nfc(), LV_PART_MAIN);
    lv_obj_set_flex_grow(title, 1);

    // Home button (top-right)
    lv_obj_t *home_btn = lv_btn_create(hdr);
    lv_obj_set_size(home_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(home_btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(home_btn, um_col_focus_cyan(),
        (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_bg_opa(home_btn, LV_OPA_COVER,
        (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_border_width(home_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(home_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(home_btn, 2, LV_PART_MAIN);
    lv_obj_add_event_cb(home_btn, [](lv_event_t *e) {
        um_haptic_select();
        um_nav_back();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(home_btn, [](lv_event_t *e) {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_ENTER) um_haptic_select();
        nfc_key_cb(e);
    }, LV_EVENT_KEY, NULL);
    lv_obj_t *home_lbl = lv_label_create(home_btn);
    lv_label_set_text(home_lbl, LV_SYMBOL_HOME);
    lv_obj_set_style_text_color(home_lbl, um_col_cyan(), LV_PART_MAIN);
    lv_obj_center(home_lbl);

    // ---- Scrollable content area ----
    lv_obj_t *scroll = lv_obj_create(nfc_root);
    lv_obj_set_width(scroll, lv_pct(100));
    lv_obj_set_flex_grow(scroll, 1);
    lv_obj_set_style_bg_color(scroll, um_col_bg(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP,
        (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_border_width(scroll, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(scroll, 0, LV_PART_MAIN);  // no focus ring on the area
    lv_obj_set_style_radius(scroll, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(scroll, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(scroll, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(scroll, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scroll, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_ACTIVE);
    // ESC/BACKSPACE from the scroll area → go back
    lv_obj_add_event_cb(scroll, [](lv_event_t *e) {
        uint32_t k = lv_event_get_key(e);
        if (k == LV_KEY_ESC || k == LV_KEY_BACKSPACE) um_nav_back();
    }, LV_EVENT_KEY, NULL);

    lv_group_t *g = lv_group_get_default();

    // Status label is the first focusable item so navigating back up
    // past the UID row lands here and scroll_to_view pulls it into view.
    nfc_status_lbl = lv_label_create(scroll);
    lv_obj_set_style_text_font(nfc_status_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(nfc_status_lbl, um_col_text_sub(), LV_PART_MAIN);
    lv_label_set_text(nfc_status_lbl, "Initialising...");
    lv_obj_add_flag(nfc_status_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(nfc_status_lbl, um_col_focus_cyan(),
        (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_set_style_bg_opa(nfc_status_lbl, LV_OPA_COVER,
        (lv_style_selector_t)((int)LV_STATE_FOCUSED | (int)LV_PART_MAIN));
    lv_obj_add_event_cb(nfc_status_lbl, [](lv_event_t *ev) {
        lv_obj_scroll_to_view(lv_event_get_target_obj(ev), LV_ANIM_ON);
    }, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(nfc_status_lbl, [](lv_event_t *e) {
        uint32_t k = lv_event_get_key(e);
        if (k == LV_KEY_ESC || k == LV_KEY_BACKSPACE) um_nav_back();
    }, LV_EVENT_KEY, NULL);
    if (g) lv_group_add_obj(g, nfc_status_lbl);

    nfc_divider(scroll);

    nfc_section(scroll, LV_SYMBOL_EYE_OPEN "  CARD IDENTITY");
    nfc_uid_val     = nfc_info_row(scroll, g, LV_SYMBOL_CHARGE, "UID",     "--");
    nfc_type_val    = nfc_info_row(scroll, g, LV_SYMBOL_WIFI,   "Type",    "--");
    nfc_product_val = nfc_info_row(scroll, g, LV_SYMBOL_LIST,   "Product", "--");

    nfc_divider(scroll);

    nfc_section(scroll, LV_SYMBOL_SETTINGS "  PROTOCOL DETAILS");
    nfc_detail1_val = nfc_info_row(scroll, g, LV_SYMBOL_RIGHT, "Detail 1", "--");
    nfc_detail2_val = nfc_info_row(scroll, g, LV_SYMBOL_RIGHT, "Detail 2", "--");
    nfc_detail3_val = nfc_info_row(scroll, g, LV_SYMBOL_RIGHT, "Detail 3", "--");

    // home_btn goes last: status→uid→type→…→detail3→home
    if (g) {
        lv_group_add_obj(g, home_btn);
        // Start focus on status label so the top of the page is always visible
        lv_group_focus_obj(nfc_status_lbl);
    }

    // ---- NFC power + init + discovery ----
#ifndef SIM_BUILD
    nfc_screen_state = NFC_SCANNING;
    nfc_found_at_ms  = 0;

    Serial.println("[NFC] powering on NFC chip...");
    instance.powerControl(POWER_NFC, true);
    delay(10);   // allow ST25R3916 to stabilise after power-on

    Serial.println("[NFC] calling rfalNfcInitialize...");
    ReturnCode initErr = NFCReader.rfalNfcInitialize();
    Serial.printf("[NFC] rfalNfcInitialize returned %d (%s)\n",
                  (int)initErr, initErr == ST_ERR_NONE ? "OK" : "FAIL");

    nfc_hw_ok = (initErr == ST_ERR_NONE);

    if (nfc_hw_ok) {
        ReturnCode discErr = nfc_start_discovery();
        if (discErr != ST_ERR_NONE) {
            char msg[48];
            snprintf(msg, sizeof(msg),
                     LV_SYMBOL_WARNING "  Discover err %d", (int)discErr);
            lv_label_set_text(nfc_status_lbl, msg);
            lv_obj_set_style_text_color(nfc_status_lbl,
                lv_color_make(220, 80, 50), LV_PART_MAIN);
            Serial.printf("[NFC] discovery failed: %d\n", (int)discErr);
            nfc_hw_ok = false;
        } else {
            lv_label_set_text(nfc_status_lbl,
                LV_SYMBOL_REFRESH "  Scanning — hold card close");
            lv_obj_set_style_text_color(nfc_status_lbl,
                lv_color_make(0, 180, 200), LV_PART_MAIN);
            nfc_ui_timer = lv_timer_create(nfc_ui_cb, 100, NULL);
            Serial.println("[NFC] discovery started OK, scanning...");
        }
    } else {
        lv_label_set_text(nfc_status_lbl,
            LV_SYMBOL_WARNING "  NFC hardware not found");
        lv_obj_set_style_text_color(nfc_status_lbl,
            lv_color_make(220, 80, 50), LV_PART_MAIN);
        nfc_screen_state = NFC_IDLE;
        instance.powerControl(POWER_NFC, false);
    }
#else
    lv_label_set_text(nfc_status_lbl, "NFC not available in simulator");
    lv_obj_set_style_text_color(nfc_status_lbl,
        um_col_text_inactive(), LV_PART_MAIN);
#endif
}

void um_nfc_destroy()
{
    if (!nfc_root) return;

#ifndef SIM_BUILD
    if (nfc_ui_timer) {
        lv_timer_del(nfc_ui_timer);
        nfc_ui_timer = NULL;
    }
    if (nfc_hw_ok) {
        NFCReader.rfalNfcDeactivate(false);
        nfc_hw_ok = false;
    }
    nfc_screen_state = NFC_IDLE;
    Serial.println("[NFC] powering off NFC chip");
    instance.powerControl(POWER_NFC, false);
#endif

    lv_group_t *g = lv_group_get_default();
    if (g) lv_group_remove_all_objs(g);
    lv_obj_del(nfc_root);

    nfc_root        = NULL;
    nfc_status_lbl  = NULL;
    nfc_uid_val     = NULL;
    nfc_type_val    = NULL;
    nfc_product_val = NULL;
    nfc_detail1_val = NULL;
    nfc_detail2_val = NULL;
    nfc_detail3_val = NULL;
}
