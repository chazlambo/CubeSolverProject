/**
 * @file lv_conf.h
 * Minimal configuration for LVGL v9.1.0 optimized for Teensy 4.1
 *
 * ---------------------------------------------------------------------------
 * PLACEMENT
 *   This file must sit ONE LEVEL ABOVE the lvgl/ library folder, i.e. at
 *   <sketchbook>/libraries/lv_conf.h. With this repo, set Arduino's sketchbook
 *   location to Code/ and this file resolves automatically.
 *
 *   LVGL will not compile without it, and the settings below are NOT defaults.
 *   Do not delete it and do not let it drift out of version control again.
 *
 * SETTINGS THAT ARE LOAD-BEARING  (changing these breaks the display)
 *   LV_COLOR_DEPTH 16   - must match what ILI9341_T4 expects. A mismatch does
 *                         not fail to build; it renders wrong colors, which
 *                         looks exactly like a hardware fault.
 *   LV_BIG_ENDIAN_SYSTEM 0
 *   LV_MEM_SIZE         - 48 KB static pool. LVGL allocates from this, not
 *                         from the heap, and running out of it does NOT
 *                         degrade gracefully: LV_USE_ASSERT_MALLOC is 1 and
 *                         LVGL's default assert handler is `while(1);`, so an
 *                         exhausted pool HALTS THE MACHINE, silently, because
 *                         LV_USE_LOG is 0.
 *
 *                         This was 32 KB, which was ~1 KB clear of the themed
 *                         menu's own peak. Adding the operation screens (the
 *                         status rows, the calibration chips, the progress
 *                         bar) pushed the measured peak to 27.3 KB, and the
 *                         transient draw layers on top of that took it over —
 *                         the firmware hung at startup with no output at all.
 *
 *                         Raised again to 64 KB when the screens kept coming:
 *                         the widget set reached 35 KB and the boot check below
 *                         warned that under 8 KB was left, which is less than a
 *                         draw layer needs. 64 KB puts ~28 KB back.
 *
 *                         Both figures were measurements, not guesses. Press M
 *                         in the desktop simulator for live usage, and watch the
 *                         line CubeDisplay::begin() prints at boot — it warns
 *                         below 8 KB free, which is the number that matters.
 *                         The Teensy 4.1 has 1 MB of RAM and the framebuffers
 *                         already account for ~195 KB, so this is not where the
 *                         pressure is.
 *
 *   - LV_USE_LOG 0 means LVGL failures are silent.
 * ---------------------------------------------------------------------------
 */

#if 1 /*Set it to "1" to enable content*/

#ifndef LV_CONF_H
#define LV_CONF_H

/*====================
   COLOR SETTINGS
 *====================*/
/* LOAD-BEARING. Must stay 16 to match ILI9341_T4.
 * NOTE: this sets the BUFFER format, not sizeof(lv_color_t). In LVGL v9,
 * lv_color_t is a 3-byte RGB888 struct regardless of this value; lv_color16_t
 * is the 16-bit type. Declaring a draw buffer as lv_color_t[N] therefore
 * allocates 3N bytes, and LVGL consumes it at 2 bytes/px -> 1.5N pixels.
 * See the BUF_LINES note in CubeDisplay.h. */
#define LV_COLOR_DEPTH 16

/*=========================
   STDLIB / MEMORY
 *=========================*/
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_BUILTIN

/* LOAD-BEARING. Static pool, separate from the ~200 KB of RAM2 heap that
 * CubeDisplay's framebuffer + lv_buf + diff buffers consume. Raise this if
 * widgets start failing to allocate (LV_USE_ASSERT_MALLOC below will catch it). */
#define LV_MEM_SIZE (64 * 1024U)    /*see the note above*/
#define LV_MEM_POOL_EXPAND_SIZE 0

/*====================
   HAL SETTINGS
 *====================*/
#define LV_DEF_REFR_PERIOD 33   /* ~30 FPS. pumpDelay() should call the LVGL
                                 * handler at least this often to stay smooth. */
#define LV_DPI_DEF 130

/*=================
 * OPERATING SYSTEM
 *=================*/
#define LV_USE_OS   LV_OS_NONE

/*========================
 * RENDERING CONFIGURATION
 *========================*/
#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_DRAW_BUF_ALIGN        4

#define LV_DRAW_LAYER_SIMPLE_BUF_SIZE (8 * 1024)

#define LV_USE_DRAW_SW 1
#if LV_USE_DRAW_SW
  #define LV_DRAW_SW_DRAW_UNIT_CNT 1
  #define LV_DRAW_SW_COMPLEX 1
  #define LV_DRAW_SW_SHADOW_CACHE_SIZE 0
  #define LV_DRAW_SW_CIRCLE_CACHE_SIZE 4
  #define LV_USE_DRAW_SW_ASM LV_DRAW_SW_ASM_NONE
#endif

/*Disable all GPU modules*/
#define LV_USE_DRAW_VGLITE 0
#define LV_USE_DRAW_PXP    0
#define LV_USE_DRAW_DAVE2D 0
#define LV_USE_DRAW_SDL    0

/*=======================
 * FEATURE CONFIGURATION
 *=======================*/

/* NOTE: with logging off, LVGL failures are silent — including a failed
 * lv_display_create(). Turn this on (LV_LOG_LEVEL_WARN) when bringing up or
 * debugging the display; off is right for a demo. */
#define LV_USE_LOG 0

/*Assertions (keep malloc check)*/
#define LV_USE_ASSERT_NULL    1
#define LV_USE_ASSERT_MALLOC  1
#define LV_USE_ASSERT_STYLE   0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ     0

/*No debug render overlays*/
#define LV_USE_REFR_DEBUG 0
#define LV_USE_LAYER_DEBUG 0
#define LV_USE_PARALLEL_DRAW_DEBUG 0

/*=====================
 *  COMPILER SETTINGS
 *====================*/
#define LV_BIG_ENDIAN_SYSTEM 0

/* DEAD SETTING (v8 spelling). LV_TICK_CUSTOM was removed in LVGL v9; the tick
 * source is now supplied at runtime via lv_tick_set_cb(millis), which the
 * sketch already does. Harmless, kept only to avoid churn. */
#define LV_TICK_CUSTOM 1

/* GOTCHA: with float support off, LVGL's built-in sprintf cannot format %f.
 * C-library sprintf("%.2f") from Teensyduino still works — that's a different
 * printf. But lv_label_set_text_fmt(label, "%f", x) will produce garbage.
 * Format floats with sprintf() first, then pass the string to
 * lv_label_set_text(). */
#define LV_USE_FLOAT 0   /*We don't need float support*/

/*==================
 *   FONT USAGE
 *===================*/
#define LV_FONT_MONTSERRAT_12 0
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_24 1   /* compiled in; only the test sketches use it.
                                   * Switching LV_FONT_DEFAULT to 24 costs no
                                   * extra flash and is far more legible from a
                                   * couple of feet away. */
#define LV_FONT_MONTSERRAT_28 0

#define LV_FONT_DEFAULT &lv_font_montserrat_14
#define LV_USE_FONT_COMPRESSED 0
#define LV_USE_FONT_PLACEHOLDER 1

/*=================
 *  TEXT SETTINGS
 *=================*/
#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS " ,.;:-_)]}"

#define LV_USE_BIDI 0
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/*==================
 * WIDGETS
 *================*/
/* WARNING — v8 vs v9 SPELLINGS.
 * LVGL v9 renamed several of these. The v8 names below are not recognised, so
 * v9 falls back to its own defaults (mostly ON):
 *     LV_USE_BTN        -> v9: LV_USE_BUTTON
 *     LV_USE_IMG        -> v9: LV_USE_IMAGE
 *     LV_USE_BTNMATRIX  -> v9: LV_USE_BUTTONMATRIX
 * The ones set to 1 are harmless (v9 defaults them on anyway). The ones set to
 * 0 are NOT saving the flash you'd expect — buttonmatrix in particular is
 * still being compiled in. Rename them if you want the savings.
 * Verify with: grep -rn "LV_USE_BUTTONMATRIX" <lvgl>/src/lv_conf_internal.h */
#define LV_WIDGETS_HAS_DEFAULT_VALUE 0
#define LV_USE_OBJ              1
#define LV_USE_LABEL            1
#define LV_USE_BTN              1   /* v8 name — see warning above */
#define LV_USE_IMG              1   /* v8 name — see warning above */

// OPTIONAL BUT KEEP OFF FOR NOW:
#define LV_USE_BTNMATRIX        0   /* v8 name — does NOT disable in v9 */
#define LV_USE_TEXTAREA         0
#define LV_USE_KEYBOARD         0
#define LV_USE_DROPDOWN         0
#define LV_USE_CALENDAR         0
#define LV_USE_CALENDAR_HEADER_ARROW 0
#define LV_USE_CALENDAR_HEADER_DROPDOWN 0
#define LV_USE_SPINBOX          0
#define LV_USE_ANIMIMG          0

/* NOTE for the menu UI work: lv_list, lv_bar, lv_roller and lv_slider are not
 * named in this file, so they take v9's defaults (enabled). If flash ever gets
 * tight, disable them explicitly with their v9 names rather than v8 ones. */

/*==================
 * THEMES
 *================*/
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 0
#define LV_THEME_DEFAULT_GROW 1
#define LV_THEME_DEFAULT_TRANSITION_TIME 80

#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO   0

/*==================
 * LAYOUTS
 *================*/
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/*======================
 * EXTERNAL LIBRARIES
 *======================*/
#define LV_USE_TJPGD 0
#define LV_USE_GIF   0
#define LV_USE_BMP   0
#define LV_USE_LIBPNG 0
#define LV_USE_LODEPNG 0
#define LV_USE_RLE   0
#define LV_USE_QRCODE 0
#define LV_USE_BARCODE 0

/*==================
 * OTHERS
 *==================*/
#define LV_USE_SNAPSHOT 0
#define LV_USE_SYSMON   0
#define LV_USE_PROFILER 0
#define LV_USE_MONKEY   0
#define LV_USE_GRIDNAV  0
#define LV_USE_FRAGMENT 0
#define LV_USE_IMGFONT  0
#define LV_USE_OBSERVER 1

/*==================
 * DEVICE DRIVERS
 *================*/
#define LV_USE_ST7735 0
#define LV_USE_ST7789 0
#define LV_USE_ST7796 0
#define LV_USE_ILI9341 0 /*You use ILI9341_T4 instead*/

#define LV_USE_SDL 0
#define LV_USE_X11 0
#define LV_USE_LINUX_FBDEV 0
#define LV_USE_LINUX_DRM   0

/*==================
 * DEMOS
 *================*/
#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_RENDER 0
#define LV_USE_DEMO_STRESS 0
#define LV_USE_DEMO_MUSIC 0
#define LV_USE_DEMO_FLEX_LAYOUT 0
#define LV_USE_DEMO_MULTILANG 0
#define LV_USE_DEMO_TRANSFORM 0
#define LV_USE_DEMO_SCROLL 0
#define LV_USE_DEMO_VECTOR_GRAPHIC 0

#endif /*LV_CONF_H*/
#endif /*Enable content*/
