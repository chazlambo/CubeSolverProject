/**
 * @file lv_conf.h
 * Configuration for LVGL v9.4 on the Teensy 4.1 (the desktop simulator pins the same release)
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
 *   LV_MEM_SIZE         - 64 KB static pool. LVGL allocates from this, not
 *                         from the heap, and running out of it does NOT
 *                         degrade gracefully: LV_USE_ASSERT_MALLOC is 1 and
 *                         LVGL's default assert handler is `while(1);`, so an
 *                         exhausted pool HALTS THE MACHINE, silently, because
 *                         LV_USE_LOG is 0.
 *
 *                         64 KB is a measurement, not a guess: the widget set
 *                         alone reached 35 KB and a transient draw layer needs
 *                         ~8 KB on top, which is why the boot check in
 *                         CubeDisplay::begin() warns below 8 KB free. At 32 KB
 *                         the firmware hung at startup with no output at all.
 *                         Press M in the desktop simulator for live usage. The
 *                         Teensy 4.1 has 1 MB of RAM and the framebuffers
 *                         already take ~195 KB, so this is not where the
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

/* LOAD-BEARING on hardware. Teensy 4.x places everything that is not
 * explicitly assigned into RAM1, .rodata included — so plain `const` font
 * data is copied to DTCM at boot rather than left in flash. Undecorated, the
 * five baked fonts plus LVGL's built-in Montserrats put ~33 KB of glyph
 * bitmaps into the same 512 KB of RAM1 that must also hold ITCM code, and the
 * firmware overflowed at link time — "program exceeds memory space", after a
 * clean compile, which reads like nothing that is wrong in the source.
 *
 * PROGMEM moves them to flash. Flash is memory-mapped on this part, so LVGL
 * still reads the glyphs in place, with no accessor and no copy. This is the
 * same idiom utility/theme_progmem.h already applies to the baked images; the
 * fonts were simply never given it. The desktop simulator has no PROGMEM, and
 * there the definition is skipped so LVGL's own empty default applies.
 *
 * The __ASSEMBLER__ guard is not decoration: lv_conf.h reaches the assembler
 * too, by way of the core's startup sources, and an unguarded #include of a C
 * header there fails as a page of "bad instruction" errors pointing at
 * <machine/_default_types.h> — nothing that names LVGL or this file. */
#ifndef __ASSEMBLER__
#  if defined(__has_include)
#    if __has_include(<avr/pgmspace.h>)
#      include <avr/pgmspace.h>
#    endif
#  endif
#  ifdef PROGMEM
#    define LV_ATTRIBUTE_LARGE_CONST PROGMEM
#  endif
#endif

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
/* v9 spellings only. LV_USE_BTN / LV_USE_IMG / LV_USE_BTNMATRIX are v8 names
 * that v9 silently ignores — do not reintroduce them. */
#define LV_WIDGETS_HAS_DEFAULT_VALUE 0
#define LV_USE_OBJ              1
#define LV_USE_LABEL            1

#define LV_USE_ARC              1   /* the dial on the idle screen */

#define LV_USE_TEXTAREA         0
#define LV_USE_KEYBOARD         0
#define LV_USE_DROPDOWN         0
#define LV_USE_CALENDAR         0
#define LV_USE_CALENDAR_HEADER_ARROW 0
#define LV_USE_CALENDAR_HEADER_DROPDOWN 0
#define LV_USE_SPINBOX          0
#define LV_USE_ANIMIMG          0

/* The rest of v9's widget set, off. It was RAM, not flash, that got tight:
 * these compile into ITCM, which shares RAM1 with every variable, so an
 * unused widget costs RAM even though it is code.
 *
 * The whole firmware creates four kinds of object: lv_obj, lv_label, lv_image
 * and lv_arc. Verify with:
 *     grep -rhoE "lv_[a-z0-9]+_create" Code/libraries/CubeSolver "Code/Main Code"
 * Anything named here that a screen starts using fails to COMPILE, loudly, so
 * this list cannot rot into a silent rendering fault. */
#define LV_USE_BUTTON           0
#define LV_USE_BUTTONMATRIX     0
#define LV_USE_BAR              0
#define LV_USE_SLIDER           0
#define LV_USE_CHART            0
#define LV_USE_TABLE            0
#define LV_USE_ROLLER           0
#define LV_USE_LIST             0
#define LV_USE_CANVAS           0
#define LV_USE_CHECKBOX         0
#define LV_USE_SWITCH           0
#define LV_USE_LINE             0
#define LV_USE_LED              0
#define LV_USE_MENU             0
#define LV_USE_MSGBOX           0
#define LV_USE_SPAN             0
#define LV_USE_SPINNER          0
#define LV_USE_TABVIEW          0
#define LV_USE_TILEVIEW         0
#define LV_USE_WIN              0
#define LV_USE_SCALE            0
#define LV_USE_IMAGEBUTTON      0

/*=========================
 * SOFTWARE DRAW: PIXEL FORMATS
 *=========================*/
/* One blender is compiled per source colour format, and they are big: the
 * eight formats turned off here were 27 KB of ITCM between them, for art this
 * project does not have. Every baked asset is RGB565A8 or RGB565 — confirm
 * with:
 *     grep -rhoE "LV_COLOR_FORMAT_[A-Z0-9_]+" Code/libraries/CubeSolver
 *
 * Leave the four below ON. RGB565 and RGB565A8 are the asset formats; A8 is
 * the mask format glyphs render through; ARGB8888 is what LVGL builds a
 * transient draw layer in when it composites with opacity, so it is needed
 * even though no asset uses it. Turning one of those off does not fail to
 * build — it drops the drawing, which looks like a corrupt screen. */
#define LV_DRAW_SW_SUPPORT_RGB565               1
#define LV_DRAW_SW_SUPPORT_RGB565A8             1
#define LV_DRAW_SW_SUPPORT_A8                   1
#define LV_DRAW_SW_SUPPORT_ARGB8888             1

#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED       0
#define LV_DRAW_SW_SUPPORT_RGB888               0
#define LV_DRAW_SW_SUPPORT_XRGB8888             0
#define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 0
#define LV_DRAW_SW_SUPPORT_L8                   0
#define LV_DRAW_SW_SUPPORT_AL88                 0
#define LV_DRAW_SW_SUPPORT_I1                   0

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
