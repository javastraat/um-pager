# ---------------------------------------------------------------------------
# Icon legend — codepoint → Material Icon name → UM_SYMBOL define
# ---------------------------------------------------------------------------
# --- MaterialIcons-Regular.ttf ---
# 0xe8bf  settings_input_antenna  UM_SYMBOL_ANTENNA
# 0xe1bb  nfc                     UM_SYMBOL_NFC
# 0xe63e  wifi                    UM_SYMBOL_WIFI
# 0xe1e2  wifi (mesh icon)        UM_SYMBOL_MESH
# 0xe623  sd_card                 UM_SYMBOL_SD_CARD
# 0xe158  markunread_mailbox      UM_SYMBOL_MAILBOX
# 0xe8b8  settings                UM_SYMBOL_SETTINGS
# 0xe88e  info                    UM_SYMBOL_INFO
# 0xe887  help                    UM_SYMBOL_HELP
# 0xe88a  home                    UM_SYMBOL_HOME
# 0xe554  mail (cool mailbox!)    UM_SYMBOL_ENVELOPE
# 0xe163  send                    UM_SYMBOL_SEND
# 0xe002  warning                 UM_SYMBOL_WARNING
# ---------------------------------------------------------------------------
# To add a new icon:
#   1. Find the codepoint at fonts.google.com/icons
#   2. Add it to RANGES and to the legend above
#   3. Add a UM_SYMBOL_* define in src/um_shared.h
#   4. Run: bash makefa.sh
# ---------------------------------------------------------------------------

FONT="MaterialIcons-Regular.ttf"
RANGES="0xe8bf,0xe1bb,0xe63e,0xe1e2,0xe623,0xe158,0xe8b8,0xe88e,0xe887,0xe88a,0xe554,0xe163,0xe002"
PATCH='s/#ifdef LV_LVGL_H_INCLUDE_SIMPLE/#ifdef __has_include\n    #if __has_include("lvgl.h")\n        #ifndef LV_LVGL_H_INCLUDE_SIMPLE\n            #define LV_LVGL_H_INCLUDE_SIMPLE\n        #endif\n    #endif\n#endif\n\n#ifdef LV_LVGL_H_INCLUDE_SIMPLE/'

# Size 48 — menu tile icons
lv_font_conv --font "$FONT" --range "$RANGES" \
  --size 48 --bpp 4 --no-compress --lv-font-name um_icons \
  -o um_icons.c --format lvgl
sed -i '' "$PATCH" um_icons.c

# Size 14 — topbar indicators
lv_font_conv --font "$FONT" --range "$RANGES" \
  --size 14 --bpp 4 --no-compress --lv-font-name um_icons_14 \
  -o um_icons_14.c --format lvgl
sed -i '' "$PATCH" um_icons_14.c
