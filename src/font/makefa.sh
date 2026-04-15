RANGES="0xe8bf,0xea71,0xe1e2,0xe623,0xe158,0xe8b8,0xe88e,0xe887"
FONT="MaterialIcons-Regular.ttf"
PATCH='s/#ifdef LV_LVGL_H_INCLUDE_SIMPLE/#ifdef __has_include\n    #if __has_include("lvgl.h")\n        #ifndef LV_LVGL_H_INCLUDE_SIMPLE\n            #define LV_LVGL_H_INCLUDE_SIMPLE\n        #endif\n    #endif\n#endif\n\n#ifdef LV_LVGL_H_INCLUDE_SIMPLE/'

# Size 40 — menu tile icons
lv_font_conv --font "$FONT" --range "$RANGES" \
  --size 40 --bpp 4 --no-compress --lv-font-name um_icons \
  -o um_icons.c --format lvgl
sed -i '' "$PATCH" um_icons.c

# Size 14 — topbar indicators
lv_font_conv --font "$FONT" --range "$RANGES" \
  --size 14 --bpp 4 --no-compress --lv-font-name um_icons_14 \
  -o um_icons_14.c --format lvgl
sed -i '' "$PATCH" um_icons_14.c
