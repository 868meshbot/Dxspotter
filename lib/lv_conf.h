/* DXSpotter LVGL 8.3 Configuration
 * T-Deck Plus: 320x240, ESP32-S3, 8MB OPI PSRAM
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*=====================
   COLOR SETTINGS
 *=====================*/
#define LV_COLOR_DEPTH 16

/*=========================
   MEMORY
 *=========================*/
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <Arduino.h>
#define LV_MEM_CUSTOM_ALLOC   ps_malloc
#define LV_MEM_CUSTOM_FREE    free
#define LV_MEM_SIZE          (32U * 1024)

/*=====================
   HAL SETTINGS
 *=====================*/
#define LV_DISP_DEF_REFR_PERIOD  33
#define LV_INDEV_DEF_READ_PERIOD 30
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE <Arduino.h>
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

/*=======================
   FEATURES
 *=======================*/
#define LV_USE_LOG 0

/* Widgets */
#define LV_USE_ARC       1
#define LV_USE_BAR       1
#define LV_USE_BTN       1
#define LV_USE_BTNMATRIX 0
#define LV_USE_CANVAS    0
#define LV_USE_CHECKBOX  1
#define LV_USE_DROPDOWN  1
#define LV_USE_IMG       0
#define LV_USE_LABEL     1
#define LV_USE_LINE      0
#define LV_USE_LIST      1
#define LV_USE_MENU      0
#define LV_USE_MSGBOX    0
#define LV_USE_ROLLER    0
#define LV_USE_SCALE     0
#define LV_USE_SLIDER    1
#define LV_USE_SPAN      0
#define LV_USE_SPINBOX   0
#define LV_USE_SPINNER   1
#define LV_USE_SWITCH    1
#define LV_USE_TABVIEW   0
#define LV_USE_TABLE     0
#define LV_USE_TEXTAREA  1
#define LV_USE_TILEVIEW  0
#define LV_USE_WIN       0

/* Extra widgets — explicitly disable those with unmet dependencies or unused */
#define LV_USE_ANIMIMG    0   /* requires LV_USE_IMG */
#define LV_USE_IMGBTN     0   /* requires LV_USE_IMG */
#define LV_USE_KEYBOARD   0   /* requires LV_USE_BTNMATRIX */
#define LV_USE_CALENDAR   0
#define LV_USE_CHART      0
#define LV_USE_COLORWHEEL 0
#define LV_USE_LED        0
#define LV_USE_METER      0

/* Themes */
#define LV_USE_THEME_DEFAULT 1
#define LV_USE_THEME_SIMPLE  1

/* Fonts */
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_36 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* Groups (needed for encoder/trackball navigation) */
#define LV_USE_GROUP 1

/* Font placeholder for missing glyphs */
#define LV_USE_FONT_PLACEHOLDER 1

/* Disable hardware-specific draw backends */
#define LV_USE_DRAW_ARM2D    0
#define LV_USE_DRAW_HELIUM   0
#define LV_USE_DRAW_NEMA_GFX 0
#define LV_USE_DRAW_SDL      0
#define LV_USE_DRAW_VGLITE   0
#define LV_USE_DRAW_PXP      0
#define LV_USE_DRAW_EVE      0
#define LV_USE_DRAW_NANOVG   0
#define LV_USE_DRAW_OPENGLES 0
#define LV_USE_DRAW_DMA2D    0

#endif /* LV_CONF_H */
