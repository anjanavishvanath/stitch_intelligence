/*
 * lv_conf.h — minimal LVGL v9 configuration for the Stitch Intelligence UI.
 *
 * LVGL refuses to compile without this file. It intentionally has no default
 * because every project has different memory/features/font choices. Only the
 * things the dashboard actually uses are enabled; the rest of LVGL's very
 * long feature list is left at its defaults.
 *
 * If you later want the theme picker, filesystem, animations, etc., mirror
 * the corresponding block from lv_conf_template.h in the managed component
 * (managed_components/lvgl__lvgl/lv_conf_template.h).
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>


/* =====================================================================
 * 1. Colour / display
 * ===================================================================== */
#define LV_COLOR_DEPTH               16     /* ST7789 native */
#define LV_COLOR_16_SWAP             0

#define LV_DPI_DEF                   130    /* used for size unit conversions */


/* =====================================================================
 * 2. Memory
 * ===================================================================== */
/* Use ESP-IDF's malloc — safer than LVGL's own pool on ESP32 */
#define LV_USE_STDLIB_MALLOC         LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING         LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF        LV_STDLIB_CLIB

/* Draw buffer alignment (1 = no special alignment). */
#define LV_DRAW_BUF_STRIDE_ALIGN     1
#define LV_DRAW_BUF_ALIGN            4


/* =====================================================================
 * 3. Timing
 * ===================================================================== */
/* We set the tick callback via lv_tick_set_cb() in gui_task; disable auto. */
#define LV_TICK_CUSTOM               0
#define LV_DEF_REFR_PERIOD           30     /* ms between screen refreshes */
#define LV_INDEV_DEF_READ_PERIOD     30


/* =====================================================================
 * 4. Fonts
 * ===================================================================== */
#define LV_FONT_MONTSERRAT_12        0
#define LV_FONT_MONTSERRAT_14        1
#define LV_FONT_MONTSERRAT_16        0
#define LV_FONT_MONTSERRAT_18        1
#define LV_FONT_MONTSERRAT_20        0
#define LV_FONT_MONTSERRAT_22        0
#define LV_FONT_MONTSERRAT_24        1
#define LV_FONT_MONTSERRAT_28        0
#define LV_FONT_MONTSERRAT_32        0
#define LV_FONT_MONTSERRAT_36        0
#define LV_FONT_MONTSERRAT_40        0
#define LV_FONT_MONTSERRAT_48        0

#define LV_FONT_DEFAULT              &lv_font_montserrat_14


/* =====================================================================
 * 5. Widgets we actually use
 * ===================================================================== */
#define LV_USE_LABEL                 1
#define LV_USE_BUTTON                1
#define LV_USE_OBJ                   1   /* the generic container */

/* Rest disabled to keep the binary small. Re-enable as needed. */
#define LV_USE_ARC                   0
#define LV_USE_BAR                   0
#define LV_USE_CANVAS                0
#define LV_USE_CHART                 0
#define LV_USE_CHECKBOX              0
#define LV_USE_DROPDOWN              0
#define LV_USE_IMAGE                 1  /* needed by lv_scale if it stays enabled */
#define LV_USE_LINE                  1  /* needed by lv_scale if it stays enabled */
#define LV_USE_ROLLER                0
#define LV_USE_SLIDER                0
#define LV_USE_SWITCH                0
#define LV_USE_TABLE                 0
#define LV_USE_TEXTAREA              0
#define LV_USE_MSGBOX                0
#define LV_USE_SPINBOX               0
#define LV_USE_SPINNER               0
#define LV_USE_KEYBOARD              0
#define LV_USE_LIST                  0
#define LV_USE_MENU                  0
#define LV_USE_METER                 0
#define LV_USE_SCALE                 0   /* v9's gauge widget; needs LV_USE_LINE + LV_USE_IMAGE */
#define LV_USE_LED                   0
#define LV_USE_QRCODE                0
#define LV_USE_TABVIEW               0
#define LV_USE_TILEVIEW              0
#define LV_USE_WIN                   0
#define LV_USE_ANIMIMG               0
#define LV_USE_CALENDAR              0
#define LV_USE_COLORWHEEL            0
#define LV_USE_IMGBTN                0
#define LV_USE_SPAN                  0


/* =====================================================================
 * 6. Themes
 * ===================================================================== */
#define LV_USE_THEME_DEFAULT         1
#define LV_THEME_DEFAULT_DARK        1
#define LV_THEME_DEFAULT_GROW        0
#define LV_USE_THEME_BASIC           0
#define LV_USE_THEME_MONO            0


/* =====================================================================
 * 7. Filesystem / images / features we don't need
 * ===================================================================== */
#define LV_USE_FS_FATFS              0
#define LV_USE_FS_STDIO              0
#define LV_USE_FS_POSIX              0
#define LV_USE_FS_WIN32              0
#define LV_USE_SYSMON                0
#define LV_USE_PROFILER              0
#define LV_USE_PERF_MONITOR          0
#define LV_USE_MEM_MONITOR           0

#define LV_USE_ANIMATION             1   /* button press effects rely on this */
#define LV_USE_SHADOW                1
#define LV_USE_OUTLINE               1


/* =====================================================================
 * 8. Logging
 * ===================================================================== */
#define LV_USE_LOG                   1
#define LV_LOG_LEVEL                 LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF                1   /* route via printf — shows in idf.py monitor */


/* =====================================================================
 * 9. Examples and demos — do NOT build into production firmware.
 *    LVGL ships a lot of bundled demo code that references every widget;
 *    if enabled, it'll try to compile lv_slider_*, lv_meter_*, etc. and
 *    fail against our slimmed-down widget set.
 * ===================================================================== */
#define LV_BUILD_EXAMPLES                    0
#define LV_USE_DEMO_WIDGETS                  0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER       0
#define LV_USE_DEMO_BENCHMARK                0
#define LV_USE_DEMO_STRESS                   0
#define LV_USE_DEMO_MUSIC                    0
#define LV_USE_DEMO_FLEX_LAYOUT              0
#define LV_USE_DEMO_MULTILANG                0
#define LV_USE_DEMO_TRANSFORM                0
#define LV_USE_DEMO_SCROLL                   0
#define LV_USE_DEMO_VECTOR_GRAPHIC           0


/* =====================================================================
 * 10. Assertions (leave conservative — cheap and catches misuse early)
 * ===================================================================== */
#define LV_USE_ASSERT_NULL           1
#define LV_USE_ASSERT_MALLOC         1
#define LV_USE_ASSERT_STYLE          0
#define LV_USE_ASSERT_MEM_INTEGRITY  0
#define LV_USE_ASSERT_OBJ            0


#endif  /* LV_CONF_H */
