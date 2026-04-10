#include <Arduino.h>
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <lvgl.h>
#include "um_nav.h"
#include "um_theme.h"

// -------------------------------------------------------
// NFC hardware — only available on real device
// -------------------------------------------------------
#ifndef SIM_BUILD
#include "nfc_include.h"
extern RfalNfcClass NFCReader;

enum NfcScreenState { NFC_IDLE, NFC_SCANNING, NFC_CARD_FOUND, NFC_WAIT_RESCAN };
static NfcScreenState  nfc_screen_state = NFC_IDLE;
static unsigned long   nfc_found_at_ms  = 0;

// String buffers filled in the activation callback
static char nfc_uid_str[64]     = "--";
static char nfc_type_str[48]    = "--";
static char nfc_product_str[48] = "";
static char nfc_detail1_str[72] = "";  // e.g. ATQA / PUPI / IDm / UID(V)
static char nfc_detail2_str[72] = "";  // e.g. SAK  / AFI  / PAD0 / DSFID
static char nfc_detail3_str[72] = "";  // e.g. -    / Proto/ PAD1 / Flags

// Map NFC-A SAK byte to a human-readable product name
static const char* nfc_mifare_name(uint8_t sak)
{
    switch (sak & 0x7F) {
        case 0x00: return "NTAG / Mifare Ultralight";
        case 0x01: return "Mifare TagIt";
        case 0x08: return "Mifare Classic 1K";
        case 0x09: return "Mifare Mini";
        case 0x10: return "Mifare Plus 2K (SL2)";
        case 0x11: return "Mifare Plus 4K (SL2)";
        case 0x18: return "Mifare Classic 4K";
        case 0x20: return "Mifare DESFire / ISO-DEP";
        case 0x28: return "Mifare Classic 4K (MF3D12)";
        case 0x38: return "Mifare Plus 4K (SL3)";
        case 0x40: return "P2P / NFC-DEP";
        case 0x60: return "Mifare Plus SL3";
        default:   return "Unknown";
    }
}

// Called by rfalNfcWorker() when a card reaches the ACTIVATED state
static void nfc_on_state(rfalNfcState st)
{
    if (st != RFAL_NFC_STATE_ACTIVATED) return;

    rfalNfcDevice *dev = nullptr;
    NFCReader.rfalNfcGetActiveDevice(&dev);
    if (!dev) return;

    // --- UID (generic — valid for all types) ---
    nfc_uid_str[0] = '\0';
    for (int i = 0; i < dev->nfcidLen; i++) {
        char hex[5];
        snprintf(hex, sizeof(hex), i ? ":%02X" : "%02X", dev->nfcid[i]);
        strncat(nfc_uid_str, hex, sizeof(nfc_uid_str) - strlen(nfc_uid_str) - 1);
    }

    // Clear per-type buffers
    nfc_product_str[0] = '\0';
    nfc_detail1_str[0] = '\0';
    nfc_detail2_str[0] = '\0';
    nfc_detail3_str[0] = '\0';

    // --- Type-specific details ---
    // When the pager acts as a poller (reader), discovered cards are
    // represented as LISTEN types (RFAL_NFC_LISTEN_TYPE_*)
    switch (dev->type) {

        case RFAL_NFC_LISTEN_TYPE_NFCA: {
            strncpy(nfc_type_str, "NFC-A  (ISO 14443-A)", sizeof(nfc_type_str) - 1);
            uint8_t atqa0 = dev->dev.nfca.sensRes.anticollisionInfo;
            uint8_t atqa1 = dev->dev.nfca.sensRes.platformInfo;
            uint8_t sak   = dev->dev.nfca.selRes.sak;
            snprintf(nfc_detail1_str, sizeof(nfc_detail1_str),
                     "ATQA: %02X %02X", atqa0, atqa1);
            snprintf(nfc_detail2_str, sizeof(nfc_detail2_str),
                     "SAK:  0x%02X", sak);
            strncpy(nfc_product_str, nfc_mifare_name(sak), sizeof(nfc_product_str) - 1);
            break;
        }

        case RFAL_NFC_LISTEN_TYPE_NFCB: {
            strncpy(nfc_type_str, "NFC-B  (ISO 14443-B)", sizeof(nfc_type_str) - 1);
            uint8_t *pupi = dev->dev.nfcb.sensbRes.nfcid0;
            snprintf(nfc_detail1_str, sizeof(nfc_detail1_str),
                     "PUPI: %02X:%02X:%02X:%02X",
                     pupi[0], pupi[1], pupi[2], pupi[3]);
            snprintf(nfc_detail2_str, sizeof(nfc_detail2_str),
                     "AFI:  0x%02X", dev->dev.nfcb.sensbRes.appData.AFI);
            snprintf(nfc_detail3_str, sizeof(nfc_detail3_str),
                     "Proto: BRC=%02X FSCI=%02X FWI=%02X",
                     dev->dev.nfcb.sensbRes.protInfo.BRC,
                     dev->dev.nfcb.sensbRes.protInfo.FsciProType,
                     dev->dev.nfcb.sensbRes.protInfo.FwiAdcFo);
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
            break;
        }

        case RFAL_NFC_LISTEN_TYPE_NFCV: {
            strncpy(nfc_type_str, "NFC-V  (ISO 15693)", sizeof(nfc_type_str) - 1);
            // ISO 15693 UID is stored LSB first — display MSB first
            uint8_t *uid = dev->dev.nfcv.InvRes.UID;
            snprintf(nfc_detail1_str, sizeof(nfc_detail1_str),
                     "UID: %02X%02X%02X%02X%02X%02X%02X%02X",
                     uid[7],uid[6],uid[5],uid[4],
                     uid[3],uid[2],uid[1],uid[0]);
            snprintf(nfc_detail2_str, sizeof(nfc_detail2_str),
                     "DSFID: 0x%02X", dev->dev.nfcv.InvRes.DSFID);
            snprintf(nfc_detail3_str, sizeof(nfc_detail3_str),
                     "Flags: 0x%02X", dev->dev.nfcv.InvRes.RES_FLAG);
            break;
        }

        case RFAL_NFC_LISTEN_TYPE_ST25TB: {
            strncpy(nfc_type_str, "ST25TB  (ST Prop.)", sizeof(nfc_type_str) - 1);
            break;
        }

        default:
            snprintf(nfc_type_str, sizeof(nfc_type_str),
                     "Unknown (type=%d)", (int)dev->type);
            break;
    }

    // Deactivate; for NFC-A put card to sleep so it's not re-selected immediately
    NFCReader.rfalNfcDeactivate(false);
    if (dev->type == RFAL_NFC_LISTEN_TYPE_NFCA) {
        NFCReader.rfalNfcaPollerSleep();
    }

    nfc_screen_state = NFC_CARD_FOUND;
}

// Kick off a new discovery cycle polling all four technologies
static void nfc_start_discovery()
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
    NFCReader.rfalNfcDiscover(&p);
}
#endif // !SIM_BUILD

// -------------------------------------------------------
// LVGL widget pointers (all screens)
// -------------------------------------------------------
static lv_obj_t   *nfc_root        = NULL;
static lv_obj_t   *nfc_status_lbl  = NULL;
static lv_obj_t   *nfc_uid_lbl     = NULL;
static lv_obj_t   *nfc_type_lbl    = NULL;
static lv_obj_t   *nfc_product_lbl = NULL;
static lv_obj_t   *nfc_detail1_lbl = NULL;
static lv_obj_t   *nfc_detail2_lbl = NULL;
static lv_obj_t   *nfc_detail3_lbl = NULL;
static lv_timer_t *nfc_poll_timer  = NULL;
static bool        nfc_hw_ok       = false;

// -------------------------------------------------------
// Poll timer — runs at 50 ms, drives the NFC state machine
// -------------------------------------------------------
#ifndef SIM_BUILD
static void nfc_poll_cb(lv_timer_t *)
{
    switch (nfc_screen_state) {

        case NFC_SCANNING:
            NFCReader.rfalNfcWorker();
            break;

        case NFC_CARD_FOUND:
            // Update every label with the data captured in the callback
            if (nfc_status_lbl) {
                lv_label_set_text(nfc_status_lbl,
                    LV_SYMBOL_OK "  Card detected — hold still");
                lv_obj_set_style_text_color(nfc_status_lbl,
                    lv_color_make(0, 220, 120), LV_PART_MAIN);
            }
            if (nfc_uid_lbl)
                lv_label_set_text_fmt(nfc_uid_lbl,     "UID:     %s", nfc_uid_str);
            if (nfc_type_lbl)
                lv_label_set_text_fmt(nfc_type_lbl,    "Type:    %s", nfc_type_str);
            if (nfc_product_lbl)
                lv_label_set_text_fmt(nfc_product_lbl, "Card:    %s",
                    nfc_product_str[0] ? nfc_product_str : "--");
            if (nfc_detail1_lbl)
                lv_label_set_text(nfc_detail1_lbl,
                    nfc_detail1_str[0] ? nfc_detail1_str : "");
            if (nfc_detail2_lbl)
                lv_label_set_text(nfc_detail2_lbl,
                    nfc_detail2_str[0] ? nfc_detail2_str : "");
            if (nfc_detail3_lbl)
                lv_label_set_text(nfc_detail3_lbl,
                    nfc_detail3_str[0] ? nfc_detail3_str : "");

            nfc_found_at_ms  = millis();
            nfc_screen_state = NFC_WAIT_RESCAN;
            break;

        case NFC_WAIT_RESCAN:
            // After 4 seconds restart scanning for the next card
            if (millis() - nfc_found_at_ms > 4000UL) {
                nfc_screen_state = NFC_SCANNING;
                if (nfc_status_lbl) {
                    lv_label_set_text(nfc_status_lbl,
                        LV_SYMBOL_REFRESH "  Scanning...");
                    lv_obj_set_style_text_color(nfc_status_lbl,
                        lv_color_make(0, 180, 200), LV_PART_MAIN);
                }
                nfc_start_discovery();
            }
            break;

        default:
            break;
    }
}
#endif // !SIM_BUILD

// -------------------------------------------------------
// Back key handler
// -------------------------------------------------------
static void nfc_back_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) um_nav_back();
}

// Helper: create one monospaced detail row inside the info box
static lv_obj_t* nfc_row(lv_obj_t *parent, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_width(lbl, lv_pct(100));
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, um_col_text_dim(), LV_PART_MAIN);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    return lbl;
}

// -------------------------------------------------------
// Screen create / destroy
// -------------------------------------------------------
void um_nfc_create()
{
    nfc_root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(nfc_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(nfc_root, um_col_bg(), LV_PART_MAIN);
    lv_obj_set_style_border_width(nfc_root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(nfc_root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(nfc_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(nfc_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(nfc_root, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(nfc_root, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_row(nfc_root, 6, LV_PART_MAIN);

    // ---- Icon ----
    lv_obj_t *ico = lv_label_create(nfc_root);
    lv_label_set_text(ico, LV_SYMBOL_LOOP);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_40, LV_PART_MAIN);
    lv_obj_set_style_text_color(ico, lv_color_make(0, 200, 160), LV_PART_MAIN);

    // ---- Title ----
    lv_obj_t *title = lv_label_create(nfc_root);
    lv_label_set_text(title, "NFC Reader");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, um_col_text(), LV_PART_MAIN);

    // ---- Status line ----
    nfc_status_lbl = lv_label_create(nfc_root);
    lv_obj_set_style_text_font(nfc_status_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(nfc_status_lbl, um_col_text_sub(), LV_PART_MAIN);
    lv_label_set_text(nfc_status_lbl, "Initialising...");

    // ---- Card info box ----
    lv_obj_t *box = lv_obj_create(nfc_root);
    lv_obj_set_width(box, lv_pct(95));
    lv_obj_set_height(box, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(box, um_col_surface(), LV_PART_MAIN);
    lv_obj_set_style_border_color(box, um_col_border(), LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(box, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(box, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(box, 3, LV_PART_MAIN);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // UID and Type rows — slightly brighter so they stand out
    nfc_uid_lbl     = nfc_row(box, "UID:     --");
    nfc_type_lbl    = nfc_row(box, "Type:    --");
    nfc_product_lbl = nfc_row(box, "Card:    --");
    lv_obj_set_style_text_color(nfc_uid_lbl,     um_col_text(), LV_PART_MAIN);
    lv_obj_set_style_text_color(nfc_type_lbl,    um_col_text(), LV_PART_MAIN);
    lv_obj_set_style_text_color(nfc_product_lbl, lv_color_make(0, 200, 160), LV_PART_MAIN);

    // Technology-specific detail rows
    nfc_detail1_lbl = nfc_row(box, "");
    nfc_detail2_lbl = nfc_row(box, "");
    nfc_detail3_lbl = nfc_row(box, "");

    // ---- Back button ----
    lv_obj_t *back_btn = lv_btn_create(nfc_root);
    lv_obj_set_width(back_btn, 160);
    lv_obj_set_style_bg_color(back_btn, um_col_surface(), LV_PART_MAIN);
    lv_obj_set_style_border_color(back_btn, um_col_border(), LV_PART_MAIN);
    lv_obj_set_style_border_width(back_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(back_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(back_btn, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(back_btn, [](lv_event_t *) { um_nav_back(); },
                        LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(back_btn, nfc_back_cb, LV_EVENT_KEY, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT "  Back");
    lv_obj_set_style_text_color(back_lbl, um_col_text_dim(), LV_PART_MAIN);
    lv_obj_center(back_lbl);

    lv_group_t *g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, back_btn);
        lv_group_focus_obj(back_btn);
    }

    // ---- NFC hardware init ----
#ifndef SIM_BUILD
    nfc_screen_state = NFC_SCANNING;
    nfc_found_at_ms  = 0;
    nfc_hw_ok = instance.initNFC();

    if (nfc_hw_ok) {
        lv_label_set_text(nfc_status_lbl,
            LV_SYMBOL_REFRESH "  Scanning...");
        lv_obj_set_style_text_color(nfc_status_lbl,
            lv_color_make(0, 180, 200), LV_PART_MAIN);
        nfc_start_discovery();
        nfc_poll_timer = lv_timer_create(nfc_poll_cb, 50, NULL);
    } else {
        lv_label_set_text(nfc_status_lbl,
            LV_SYMBOL_WARNING "  NFC hardware not found");
        lv_obj_set_style_text_color(nfc_status_lbl,
            lv_color_make(220, 80, 50), LV_PART_MAIN);
        nfc_screen_state = NFC_IDLE;
    }
#else
    lv_label_set_text(nfc_status_lbl, "NFC not available in simulator");
    lv_obj_set_style_text_color(nfc_status_lbl, um_col_text_inactive(), LV_PART_MAIN);
#endif
}

void um_nfc_destroy()
{
    if (!nfc_root) return;

#ifndef SIM_BUILD
    if (nfc_poll_timer) {
        lv_timer_del(nfc_poll_timer);
        nfc_poll_timer = NULL;
    }
    if (nfc_hw_ok) {
        NFCReader.rfalNfcDeactivate(false);
        nfc_hw_ok = false;
    }
    nfc_screen_state = NFC_IDLE;
#endif

    lv_group_t *g = lv_group_get_default();
    if (g) lv_group_remove_all_objs(g);
    lv_obj_del(nfc_root);

    nfc_root        = NULL;
    nfc_status_lbl  = NULL;
    nfc_uid_lbl     = NULL;
    nfc_type_lbl    = NULL;
    nfc_product_lbl = NULL;
    nfc_detail1_lbl = NULL;
    nfc_detail2_lbl = NULL;
    nfc_detail3_lbl = NULL;
}
