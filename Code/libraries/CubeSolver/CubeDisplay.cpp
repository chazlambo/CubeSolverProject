// CubeDisplay.cpp
#include "CubeDisplay.h"
#include "CubeHardwareConfig.h"  // For menuEncoder

#include <math.h>

// Static instance pointer for callbacks
CubeDisplay* CubeDisplay::instance = nullptr;

CubeDisplay::CubeDisplay(int sck, int miso, int mosi, int dc, int cs, int reset,
                         int touchCs, int touchIrq)
    : PIN_SCK(sck), PIN_MISO(miso), PIN_MOSI(mosi), PIN_DC(dc), PIN_CS(cs),
      PIN_RESET(reset), PIN_TOUCH_CS(touchCs), PIN_TOUCH_IRQ(touchIrq),
      tft(nullptr), diff1(nullptr), diff2(nullptr), internal_fb(nullptr),
      lv_buf(nullptr), disp(nullptr), lbl_msg(nullptr), lbl_status(nullptr),
      lbl_title(nullptr), lbl_footer(nullptr),
      img_bg(nullptr), img_pane(nullptr),
      img_nextFrame(nullptr), img_nextLabel(nullptr),
      img_bandFill(nullptr), img_bandEdge(nullptr),
      box_desc(nullptr), lbl_desc(nullptr), menuGroup(nullptr),
      img_orb(nullptr), img_comma(nullptr),
      curTheme(MenuTheme::Inherit),
      mode(Mode::None), opActive(false),
      pendScreen(nullptr), pendDetail(nullptr),
      pendRows(0), pendSel(0), pendDir(1),
      curRows(0), curSel(0), transitioning(false)
{
    for (int i = 0; i < kRows; ++i) {
        barBox[i] = nullptr; img_bar[i] = nullptr; lbl_bar[i] = nullptr;
    }
    for (int i = 0; i < kPreviewLines; ++i) lbl_prev[i] = nullptr;
    for (int i = 0; i < 2; ++i) { ghost[i] = nullptr; img_sonar[i] = nullptr; }
    for (int i = 0; i < kRows; ++i) pendItems[i] = nullptr;
    for (int i = 0; i < kOpLines; ++i) { lbl_line[i] = nullptr; lbl_lineVal[i] = nullptr; }
    for (int b = 0; b < 2; ++b) {
        lbl_chipRow[b] = nullptr;
        for (int i = 0; i < kChipCount; ++i) chip[b][i] = nullptr;
    }
    for (int i = 0; i < kFaceCount; ++i) lbl_faceCap[i] = nullptr;
    bar_track = nullptr;
    bar_fill  = nullptr;
    img_net   = nullptr;
    for (int i = 0; i < 6; ++i) lbl_netFace[i] = nullptr;
    instance = this;  // Set static instance for callbacks
}

// ---------------------------------------------------------------------------
//  Themed menu geometry, in pixels on the 320x240 panel (rotation 1, landscape).
//
//  These mirror the values the LVGL preview computes at runtime
//  (docs/theme/melee-menu-lvgl-preview.html). Where an asset is involved the
//  numbers come from THEME_*_X / _W instead — the baker measures the rasterized
//  result and writes those out, so a re-bake of a changed design moves the
//  widgets without anyone editing this file.
// ---------------------------------------------------------------------------
namespace {
    // Zigzag row offsets: the master's [198,171,91,154,113] over the 3.75 scale
    // factor. Deliberately not a staircase — the irregularity is the design.
    const int ZIG_X[5] = { 53, 46, 24, 41, 30 };

    // A bar's box is sized by the largest thing that can appear inside it,
    // which is the sonar ring at full scale — NOT the bar image. Sizing the box
    // to the bar would clip the ring top and bottom, and LVGL clips children to
    // their parent without saying so.
    const int BOX_W     = THEME_BAR_UNSEL_W;                        // 167
    const int BOX_H     = THEME_SONAR_H;                            // 56
    const int BAR_IMG_Y = (BOX_H - THEME_BAR_UNSEL_H) / 2;          // 4

    // Socket centre within the box: the bar path's cap centre (141 - 22/2) is
    // at 130 in path space, and the asset's viewBox starts at -6.
    const int SOCKET_CX = 136;
    const int SOCKET_CY = BOX_H / 2;                                // 28

    // Text band inside the bar, from barSVG(): the chamfer ends at 7.5 and the
    // socket arc begins at capCx - r, both shifted by the same 6.
    const int LABEL_X = 13;
    const int LABEL_W = 113;
    const int LABEL_Y = 20;

    const int TITLE_X = 52,  TITLE_Y = 21;
    const int DESC_X  = 69,  DESC_Y  = 199, DESC_W = 182, DESC_H = 21;
    const int PANE_X  = 220, PANE_Y  = 69;
    const int PREV_X  = 226, PREV_Y  = 78,  PREV_STEP = 17;

    // Hard ceiling on preview text. The pane is a perspective trapezoid, so its
    // right border sits at about x=282 regardless of what the asset's bounding
    // box says — measure the border, not the bitmap width. Anything wider than
    // this spills out of the pane and onto the frame band behind it.
    const int PREV_W  = 54;
    const int GHOST_Y = 83,  GHOST_STEP = 33;
    const int NEXT_LABEL_X = 195, NEXT_LABEL_Y = 89;

    // Colours that belong to no theme.
    const uint32_t COL_LABEL_OFF = 0xF0A018;   // gold text on an unselected bar
    const uint32_t COL_LABEL_ON  = 0x151000;   // near-black on the yellow bar
    const uint32_t COL_TITLE     = 0xACAFBA;
    const uint32_t COL_DESC_TEXT = 0xF8FAFF;
    const uint32_t COL_DESC_BG   = 0x050412;
    const uint32_t COL_DESC_EDGE = 0xEEF0F8;
    const uint32_t COL_PREV_TEXT = 0xDFF2F4;
    const uint32_t COL_NEXT      = 0xCDD2DE;
    const uint32_t COL_FOOTER    = 0x9A9A9A;

    // Opacities the design states as fractions.
    const lv_opa_t OPA_TITLE      = 209;   // .82
    const lv_opa_t OPA_DESC_TEXT  = 242;   // .95
    const lv_opa_t OPA_DESC_BG    = 217;   // .85
    const lv_opa_t OPA_NEXT_FRAME = 158;   // .62
    const lv_opa_t OPA_NEXT_LABEL = 204;   // .80
    const lv_opa_t OPA_GHOST      = 128;   // .50

    // Band fill and edge per theme. The fill's own 0.55 opacity is already
    // baked into the mask's alpha, so both recolors go on at full strength.
    struct ThemePair { uint32_t fill, edge; };
    const ThemePair kPalette[6] = {
        { 0x14522A, 0x4ADE70 },   // Green
        { 0x1D2680, 0x6D7CF0 },   // Blue
        { 0x6E1A10, 0xF0603A },   // Red
        { 0x4A1A72, 0xAE6AF0 },   // Violet
        { 0x6E5A10, 0xE8CF3A },   // Yellow
        { 0x33176E, 0x8A62E8 },   // Purple
    };

    // ---- operation-screen layout -----------------------------------------
    // Inside the same frame the menu uses, so the two never disagree about
    // where the content area is.
    const int OP_HEAD_Y  = 58;
    const int OP_SUB_Y   = 82;
    const int OP_LINE_Y  = 84;    // body rows start here when there is no sub
    const int OP_LINE_STEP = 17;
    const int OP_LINE_X  = 52;
    const int OP_LINE_W  = 216;   // right edge of the frame's inner area

    // Calibration chips: two rows of six, centred under the headline.
    // Chips sit to the RIGHT of their row label, not on top of it: "BOARD 1"
    // at 9 px is about 34 px wide, so the strip starts clear of it.
    const int CHIP_W = 20, CHIP_H = 14, CHIP_GAP = 5;
    const int CHIP_X = 96, CHIP_Y = 108, CHIP_ROW_STEP = 22;

    // The scan's face row reuses the same chip objects at a different size and
    // position, so both layouts are set on every call rather than assumed.
    // Bigger than the calibration chips because there is only one row of them
    // and each carries a caption.
    const int FACE_W = 26, FACE_H = 18, FACE_GAP = 8;
    const int FACE_X = 62, FACE_Y = 100, FACE_CAP_Y = 121;

    // Face order for the scan row. Not the scan ORDER — the cube's own naming,
    // so the row reads U R F D L B however the machine happens to visit them.
    const char* const kFaceNames[6] = { "U", "R", "F", "D", "L", "B" };

    // ---- cube net --------------------------------------------------------
    //
    // An unfolded cube, 12 cells wide by 9 tall:
    //
    //          U
    //     L    F    R    B
    //          D
    //
    // Drawn by writing RGB565A8 straight into a static buffer rather than with
    // LVGL primitives. 54 stickers would be 54 objects — most of the pool for
    // one screen — and lv_canvas would need its buffer anyway. LVGL reads an
    // uncompressed variable-source image directly out of the buffer without
    // caching it (`use_directly` in lv_bin_decoder), so rewriting these bytes
    // and invalidating the object is all it takes to show a new state.
    //
    // The buffer MUST NOT come from LV_MEM: at 32 KB it is two thirds of the
    // whole pool. It is a plain static array, which on a Teensy 4.1 with 1 MB
    // of RAM is not a constraint.
    const int NET_CELL = 11;              // sticker 10 px plus a 1 px gap
    const int NET_STICKER = 10;
    const int NET_COLS = 12, NET_ROWS = 9;
    const int NET_W = NET_COLS * NET_CELL;   // 120
    const int NET_H = NET_ROWS * NET_CELL;   // 90
    const int NET_X = (320 - NET_W) / 2, NET_Y = 48;

    uint8_t  s_netBuf[NET_W * NET_H * 3];    // RGB565 plane, then the A8 plane
    lv_image_dsc_t s_netDsc;

    // Where each face sits in the net, in cells, and where it starts in the
    // facelet string. Standard order: U R F D L B.
    const struct { int col, row, base; } kNetFaces[6] = {
        { 3, 0,  0 },   // U
        { 6, 3,  9 },   // R
        { 3, 3, 18 },   // F
        { 3, 6, 27 },   // D
        { 0, 3, 36 },   // L
        { 9, 3, 45 },   // B
    };

    inline uint16_t rgb565(uint32_t c) {
        return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
    }

    // A face letter is printed ON its centre sticker, so it has to survive
    // being drawn over white, yellow, red, orange, green or blue. Pick by
    // perceived brightness rather than by a per-colour table, which would need
    // revisiting every time the palette moves.
    inline uint32_t inkFor(uint32_t bg) {
        const uint32_t lum = (299u * ((bg >> 16) & 0xFF) +
                              587u * ((bg >>  8) & 0xFF) +
                              114u * ( bg        & 0xFF)) / 1000u;
        return (lum > 145u) ? 0x101018 : 0xF4F6FF;
    }

    // Progress bar, and where the sub-line goes when step rows own the middle
    // of the screen instead of a headline.
    const int PBAR_X = 60, PBAR_Y = 150, PBAR_W = 200, PBAR_H = 8;
    const int OP_SUB_BELOW_STEPS_Y = 166;

    // The six cube colours, in the order setOpChips() expects its bits.
    const uint32_t kChipColors[6] = {
        0xF0F0F0,   // White
        0xF5C518,   // Yellow
        0xD03020,   // Red
        0xE07818,   // Orange
        0x2E9E4A,   // Green
        0x2050C0,   // Blue
    };

    // ---- animation timings, from the design ------------------------------
    //
    // The wheel turns about a pivot OFF-SCREEN to the right. With the bars at
    // x 24..53 that pivot is ~340 px away, so a 24 degree turn reads mostly as
    // a long vertical sweep with a little horizontal drift — which is why
    // moving each bar along its own arc reproduces the motion faithfully even
    // though the bars themselves do not tilt.
    const float PIVOT_X = 389.0f, PIVOT_Y = 123.0f;
    const int   WHEEL_OUT_MS = 160, WHEEL_IN_MS = 200;
    const float WHEEL_OUT_DEG = 24.0f, WHEEL_IN_DEG = 28.0f;
    const int   WHEEL_OUT_FADE_DELAY = 40, WHEEL_OUT_FADE_MS = 130;
    const int   WHEEL_IN_FADE_MS = 120;

    const int   DETAIL_FADE_MS = 110;   // caption / preview swap

    const int   SONAR_CYCLE_MS = 2100;  // rings converge over the first 52%
    const float SONAR_TRAVEL   = 0.52f;
    const float SONAR_PEAK     = 0.07f / 0.52f;   // .75 opacity at 7% of a cycle
    const lv_opa_t SONAR_OPA   = 191;             // .75
    const int   SONAR_OFFSET_MS = 160;  // second ring trails the first
    const int   COMMA_SPIN_MS  = 2600;

    const uint32_t COL_OP_HEAD = 0xF2F5FF;
    const uint32_t COL_OP_KEY   = 0xA8B0C4;   // the label half of a status row
    const uint32_t COL_OP_VALUE = 0xEFF3FF;   // the value half

    inline void hide(lv_obj_t* o) { if (o) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); }
    inline void show(lv_obj_t* o) { if (o) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN); }

    // lv_obj_create() comes with the default theme's panel look — background,
    // border, padding, scrollbars. Everything here is either a bare positioning
    // container or a surface styled from scratch, so strip it first.
    inline void makeBare(lv_obj_t* o) {
        lv_obj_remove_style_all(o);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    }

    // Recolour a white mask, which is what makes one asset serve all six
    // themes. The masks are RGB565A8 rather than A8 deliberately: LVGL reads an
    // uncompressed RGB565A8 straight out of flash, but copies every alpha-only
    // image into a RAM buffer first, and a 49 KB band does not fit the 32 KB
    // LV_MEM pool. It fails silently there — LV_USE_LOG is 0 — and the band
    // simply never appears. See Code/tools/bake_theme.py.
    inline void tint(lv_obj_t* img, uint32_t rgb, lv_opa_t opa = LV_OPA_COVER) {
        lv_obj_set_style_image_recolor(img, lv_color_hex(rgb), 0);
        lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
        lv_obj_set_style_image_opa(img, opa, 0);
    }
}

bool CubeDisplay::begin(uint32_t spiSpeed) {
    // Allocate buffers
    diff1 = new ILI9341_T4::DiffBuffStatic<8000>();
    diff2 = new ILI9341_T4::DiffBuffStatic<8000>();
    internal_fb = new uint16_t[LX * LY];
    lv_buf = new lv_color_t[LX * BUF_LINES];

    if (!diff1 || !diff2 || !internal_fb || !lv_buf) {
        Serial.println("ERROR: Failed to allocate display buffers!");
        return false;
    }

    // Create TFT driver
    tft = new ILI9341_T4::ILI9341Driver(
        PIN_CS, PIN_DC,
        PIN_SCK, PIN_MOSI, PIN_MISO,
        PIN_RESET, PIN_TOUCH_CS, PIN_TOUCH_IRQ
    );

    if (!tft) {
        Serial.println("ERROR: Failed to create TFT driver!");
        return false;
    }

    // Initialize TFT
    tft->output(&Serial);
    if (!tft->begin(spiSpeed)) {
        Serial.println("ERROR: TFT begin failed!");
        return false;
    }

    tft->setFramebuffer(internal_fb);
    tft->setDiffBuffers(diff1, diff2);
    tft->setRotation(1);  // Landscape
    tft->clear(0x0000);   // Black

    // Initialize LVGL
    lv_init();
    lv_tick_set_cb(tick_cb);

    disp = lv_display_create(LX, LY);
    if (!disp) {
        Serial.println("ERROR: Failed to create LVGL display!");
        return false;
    }

    lv_display_set_flush_cb(disp, flush_cb_wrapper);
    lv_display_set_buffers(
        disp,
        lv_buf,
        NULL,
        LX * BUF_LINES * sizeof(lv_color_t),
        LV_DISPLAY_RENDER_MODE_PARTIAL
    );

    buildUi();

    // Initial update
    lv_task_handler();

    // Report the pool. Every widget this class will ever use has been created
    // by now, so this figure is the floor — only draw buffers and animations
    // are added later. It matters because exhausting LV_MEM does not fail
    // softly: LV_USE_ASSERT_MALLOC halts in a while(1) and LV_USE_LOG is 0, so
    // the only symptom is a machine that stops with a blank screen. Seeing the
    // headroom here is much cheaper than diagnosing that.
    {
        lv_mem_monitor_t mon;
        lv_mem_monitor(&mon);
        Serial.print(F("LVGL pool after UI build: "));
        Serial.print((unsigned)(mon.total_size - mon.free_size));
        Serial.print('/');
        Serial.print((unsigned)mon.total_size);
        Serial.print(F(" bytes, "));
        Serial.print((unsigned)mon.free_size);
        Serial.println(F(" free"));
        if (mon.free_size < 8192) {
            Serial.println(F("WARNING: under 8 KB free - a draw layer may not fit."));
        }
    }

    Serial.println("Display initialized successfully!");
    return true;
}

// ---------------------------------------------------------------------------
//  UI construction. Runs once; nothing below ever creates or deletes a widget.
// ---------------------------------------------------------------------------
void CubeDisplay::buildUi() {
    lv_obj_t* scr = lv_screen_active();

    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_style_init(&white_style);
    lv_style_set_text_color(&white_style, lv_color_white());

    // The themed menu goes down first so that everything after it — the title,
    // the message-mode labels, the footer — draws on top.
    buildTheme(scr);

    // ---- title bar ----
    // Shared by both modes. The old opaque slate bar is gone: the themed
    // background already separates the title from the content, and a filled bar
    // across the top would cut through the frame band's notch, which is the one
    // piece of the design the title is supposed to sit inside.
    lbl_title = lv_label_create(scr);
    lv_obj_set_pos(lbl_title, TITLE_X, TITLE_Y);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(COL_TITLE), 0);
    lv_obj_set_style_text_opa(lbl_title, OPA_TITLE, 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_title_13, 0);
    lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_DOT);
    lv_label_set_text(lbl_title, "");
    hide(lbl_title);

    // ---- message-mode labels ----
    // Width-bounded and wrapping. The originals were auto-sized and centred,
    // so the longer fault strings ("Impossible cube, repair failed. Rescan.")
    // ran off both edges of a 320 px panel.
    lbl_msg = lv_label_create(scr);
    lv_obj_set_width(lbl_msg, LX - 24);
    lv_obj_set_pos(lbl_msg, 12, 60);
    lv_obj_add_style(lbl_msg, &white_style, LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_msg, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(lbl_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl_msg, LV_LABEL_LONG_WRAP);
    lv_label_set_text(lbl_msg, "Display Ready");

    lbl_status = lv_label_create(scr);
    lv_obj_set_width(lbl_status, LX - 24);
    lv_obj_set_pos(lbl_status, 12, 130);
    lv_obj_add_style(lbl_status, &white_style, LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(lbl_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl_status, LV_LABEL_LONG_WRAP);
    lv_label_set_text(lbl_status, "");

    // ---- footer ----
    // Message mode only. The themed menu states its controls nowhere: the
    // design has no room for a hint line, and the description box occupies the
    // space the old one used.
    lbl_footer = lv_label_create(scr);
    lv_obj_set_width(lbl_footer, LX - 40);
    lv_obj_set_pos(lbl_footer, 12, 214);
    lv_obj_set_style_text_color(lbl_footer, lv_color_hex(COL_FOOTER), 0);
    lv_obj_set_style_text_font(lbl_footer, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(lbl_footer, LV_LABEL_LONG_DOT);
    lv_label_set_text(lbl_footer, "");
    hide(lbl_footer);

    mode = Mode::Message;
}

// ---------------------------------------------------------------------------
//  Themed menu construction.
//
//  Creation order IS z-order in LVGL, and it matches the preview's DOM order:
//  background, preview pane, NEXT SCREEN outline, frame band, vertical label,
//  bars, description box. The pane going down BEFORE the band is deliberate —
//  the band's right edge crosses in front of it.
// ---------------------------------------------------------------------------
void CubeDisplay::buildTheme(lv_obj_t* scr) {
    img_bg = lv_image_create(scr);
    lv_image_set_src(img_bg, &theme_bg);
    lv_obj_set_pos(img_bg, 0, 0);

    img_pane = lv_image_create(scr);
    lv_image_set_src(img_pane, &theme_pane);
    lv_obj_set_pos(img_pane, PANE_X + THEME_PANE_X, PANE_Y + THEME_PANE_Y);

    // Preview text sits upright inside the tilted pane. LVGL cannot skew a
    // glyph run, and baking the text would freeze the item labels into an
    // asset, so the frame keeps its perspective and the type does not.
    for (int i = 0; i < kPreviewLines; ++i) {
        lbl_prev[i] = lv_label_create(scr);
        lv_obj_set_pos(lbl_prev[i], PREV_X, PREV_Y + i * PREV_STEP);
        lv_obj_set_width(lbl_prev[i], PREV_W);
        lv_obj_set_style_text_font(lbl_prev[i], &lv_font_prev_9, 0);
        lv_obj_set_style_text_color(lbl_prev[i], lv_color_hex(COL_PREV_TEXT), 0);
        lv_label_set_long_mode(lbl_prev[i], LV_LABEL_LONG_DOT);
        lv_label_set_text(lbl_prev[i], "");
        hide(lbl_prev[i]);
    }

    // Shown instead of the list for items that start an operation rather than
    // opening a screen — there is nothing to enumerate, so the design shows a
    // pair of blank plates.
    for (int i = 0; i < 2; ++i) {
        ghost[i] = lv_obj_create(scr);
        makeBare(ghost[i]);
        lv_obj_set_size(ghost[i], 42, 24);
        lv_obj_set_pos(ghost[i], PREV_X, GHOST_Y + i * GHOST_STEP);
        lv_obj_set_style_border_color(ghost[i], lv_color_hex(COL_PREV_TEXT), 0);
        lv_obj_set_style_border_opa(ghost[i], OPA_GHOST, 0);
        lv_obj_set_style_border_width(ghost[i], 1, 0);
        lv_obj_set_style_radius(ghost[i], 2, 0);
        hide(ghost[i]);
    }

    img_nextFrame = lv_image_create(scr);
    lv_image_set_src(img_nextFrame, &theme_next_frame);
    lv_obj_set_pos(img_nextFrame, THEME_NEXT_FRAME_X, THEME_NEXT_FRAME_Y);
    tint(img_nextFrame, COL_NEXT, OPA_NEXT_FRAME);

    img_bandFill = lv_image_create(scr);
    lv_image_set_src(img_bandFill, &theme_band_fill);
    lv_obj_set_pos(img_bandFill, THEME_BAND_FILL_X, THEME_BAND_FILL_Y);

    img_bandEdge = lv_image_create(scr);
    lv_image_set_src(img_bandEdge, &theme_band_edge);
    lv_obj_set_pos(img_bandEdge, THEME_BAND_EDGE_X, THEME_BAND_EDGE_Y);

    img_nextLabel = lv_image_create(scr);
    lv_image_set_src(img_nextLabel, &theme_next_label);
    lv_obj_set_pos(img_nextLabel,
                   NEXT_LABEL_X + THEME_NEXT_LABEL_X,
                   NEXT_LABEL_Y + THEME_NEXT_LABEL_Y);
    tint(img_nextLabel, COL_NEXT, OPA_NEXT_LABEL);

    // ---- bars ----
    // One parent for all five, so a screen change can move the whole group.
    menuGroup = lv_obj_create(scr);
    makeBare(menuGroup);
    lv_obj_set_pos(menuGroup, 0, 0);
    lv_obj_set_size(menuGroup, LX, LY);

    for (int i = 0; i < kRows; ++i) {
        barBox[i] = lv_obj_create(menuGroup);
        makeBare(barBox[i]);
        lv_obj_set_size(barBox[i], BOX_W, BOX_H);

        img_bar[i] = lv_image_create(barBox[i]);
        lv_image_set_src(img_bar[i], &theme_bar_unsel);
        lv_obj_set_pos(img_bar[i], 0, BAR_IMG_Y);

        // Fixed box plus centred alignment rather than measuring the string:
        // the label has to stay centred between the chamfer and the socket as
        // the text changes, and LV_LABEL_LONG_DOT needs a bounded width anyway.
        lbl_bar[i] = lv_label_create(barBox[i]);
        lv_obj_set_pos(lbl_bar[i], LABEL_X, LABEL_Y);
        lv_obj_set_width(lbl_bar[i], LABEL_W);
        lv_obj_set_style_text_font(lbl_bar[i], &lv_font_bar_12, 0);
        lv_obj_set_style_text_color(lbl_bar[i], lv_color_hex(COL_LABEL_OFF), 0);
        lv_obj_set_style_text_align(lbl_bar[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(lbl_bar[i], LV_LABEL_LONG_DOT);
        lv_label_set_text(lbl_bar[i], "");

        hide(barBox[i]);
    }

    // Cursor furniture: created after the bars so it draws over them, and
    // parented to the group so a screen transition carries it along.
    img_sonar[0] = lv_image_create(menuGroup);
    lv_image_set_src(img_sonar[0], &theme_sonar);
    hide(img_sonar[0]);

    img_sonar[1] = lv_image_create(menuGroup);
    lv_image_set_src(img_sonar[1], &theme_sonar);
    hide(img_sonar[1]);

    img_orb = lv_image_create(menuGroup);
    lv_image_set_src(img_orb, &theme_orb);
    hide(img_orb);

    img_comma = lv_image_create(menuGroup);
    lv_image_set_src(img_comma, &theme_comma);
    hide(img_comma);

    // ---- description box ----
    box_desc = lv_obj_create(scr);
    makeBare(box_desc);
    lv_obj_set_size(box_desc, DESC_W, DESC_H);
    lv_obj_set_pos(box_desc, DESC_X, DESC_Y);
    lv_obj_set_style_bg_color(box_desc, lv_color_hex(COL_DESC_BG), 0);
    lv_obj_set_style_bg_opa(box_desc, OPA_DESC_BG, 0);
    lv_obj_set_style_border_color(box_desc, lv_color_hex(COL_DESC_EDGE), 0);
    lv_obj_set_style_border_width(box_desc, 2, 0);
    lv_obj_set_style_radius(box_desc, 4, 0);

    lbl_desc = lv_label_create(box_desc);
    lv_obj_set_width(lbl_desc, DESC_W - 10);
    lv_obj_set_style_text_font(lbl_desc, &lv_font_desc_11, 0);
    lv_obj_set_style_text_color(lbl_desc, lv_color_hex(COL_DESC_TEXT), 0);
    lv_obj_set_style_text_opa(lbl_desc, OPA_DESC_TEXT, 0);
    lv_obj_set_style_text_align(lbl_desc, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl_desc, LV_LABEL_LONG_DOT);
    lv_label_set_text(lbl_desc, "");
    lv_obj_align(lbl_desc, LV_ALIGN_CENTER, 0, 0);

    hide(box_desc);

    buildOpUi(scr);
    startCursorAnims();
}

// ---------------------------------------------------------------------------
//  Operation-screen widgets.
//
//  Only the pieces the menu has no equivalent for. The headline, sub-line and
//  hint bar are lbl_msg, lbl_status and box_desc respectively, and the step
//  rows are the menu's own bars — all idle whenever an operation is on screen,
//  so reusing them costs nothing from a pool that is already two thirds gone.
// ---------------------------------------------------------------------------
void CubeDisplay::buildOpUi(lv_obj_t* scr) {
    for (int i = 0; i < kOpLines; ++i) {
        // A status row is two labels, not one string with padding: proportional
        // type cannot be column-aligned with spaces, and these rows exist to be
        // scanned down rather than read across.
        lbl_line[i] = lv_label_create(scr);
        lv_obj_set_pos(lbl_line[i], OP_LINE_X, OP_LINE_Y + i * OP_LINE_STEP);
        lv_obj_set_width(lbl_line[i], OP_LINE_W);
        lv_obj_set_style_text_font(lbl_line[i], &lv_font_bar_12, 0);
        lv_obj_set_style_text_color(lbl_line[i], lv_color_hex(COL_OP_KEY), 0);
        lv_label_set_long_mode(lbl_line[i], LV_LABEL_LONG_DOT);
        lv_label_set_text(lbl_line[i], "");
        hide(lbl_line[i]);

        lbl_lineVal[i] = lv_label_create(scr);
        lv_obj_set_pos(lbl_lineVal[i], OP_LINE_X, OP_LINE_Y + i * OP_LINE_STEP);
        lv_obj_set_width(lbl_lineVal[i], OP_LINE_W);
        lv_obj_set_style_text_font(lbl_lineVal[i], &lv_font_bar_12, 0);
        lv_obj_set_style_text_color(lbl_lineVal[i], lv_color_hex(COL_OP_VALUE), 0);
        lv_obj_set_style_text_align(lbl_lineVal[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_long_mode(lbl_lineVal[i], LV_LABEL_LONG_DOT);
        lv_label_set_text(lbl_lineVal[i], "");
        hide(lbl_lineVal[i]);
    }

    // Colour chips. Two rows because the two sensor boards see DIFFERENT
    // colours at each rotation — they finish the set at different moments, and
    // a single shared row would have to lie about one of them.
    for (int b = 0; b < 2; ++b) {
        lbl_chipRow[b] = lv_label_create(scr);
        lv_obj_set_pos(lbl_chipRow[b], OP_LINE_X, CHIP_Y + b * CHIP_ROW_STEP + 3);
        lv_obj_set_style_text_font(lbl_chipRow[b], &lv_font_prev_9, 0);
        lv_obj_set_style_text_color(lbl_chipRow[b], lv_color_hex(COL_OP_KEY), 0);
        lv_label_set_text(lbl_chipRow[b], b == 0 ? "BOARD 1" : "BOARD 2");
        hide(lbl_chipRow[b]);

        for (int i = 0; i < kChipCount; ++i) {
            chip[b][i] = lv_obj_create(scr);
            makeBare(chip[b][i]);
            lv_obj_set_size(chip[b][i], CHIP_W, CHIP_H);
            lv_obj_set_pos(chip[b][i],
                           CHIP_X + i * (CHIP_W + CHIP_GAP),
                           CHIP_Y + b * CHIP_ROW_STEP);
            lv_obj_set_style_radius(chip[b][i], 2, 0);
            lv_obj_set_style_bg_color(chip[b][i], lv_color_hex(kChipColors[i]), 0);
            lv_obj_set_style_border_color(chip[b][i], lv_color_hex(kChipColors[i]), 0);
            lv_obj_set_style_border_width(chip[b][i], 1, 0);
            hide(chip[b][i]);
        }
    }

    for (int i = 0; i < kFaceCount; ++i) {
        lbl_faceCap[i] = lv_label_create(scr);
        lv_obj_set_size(lbl_faceCap[i], FACE_W, 12);
        lv_obj_set_pos(lbl_faceCap[i], FACE_X + i * (FACE_W + FACE_GAP), FACE_CAP_Y);
        lv_obj_set_style_text_font(lbl_faceCap[i], &lv_font_prev_9, 0);
        lv_obj_set_style_text_color(lbl_faceCap[i], lv_color_hex(COL_OP_KEY), 0);
        lv_obj_set_style_text_align(lbl_faceCap[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(lbl_faceCap[i], kFaceNames[i]);
        hide(lbl_faceCap[i]);
    }

    bar_track = lv_obj_create(scr);
    makeBare(bar_track);
    lv_obj_set_size(bar_track, PBAR_W, PBAR_H);
    lv_obj_set_pos(bar_track, PBAR_X, PBAR_Y);
    lv_obj_set_style_radius(bar_track, 2, 0);
    lv_obj_set_style_bg_color(bar_track, lv_color_hex(0x050412), 0);
    lv_obj_set_style_bg_opa(bar_track, 190, 0);
    lv_obj_set_style_border_color(bar_track, lv_color_hex(COL_DESC_EDGE), 0);
    lv_obj_set_style_border_opa(bar_track, 150, 0);
    lv_obj_set_style_border_width(bar_track, 1, 0);
    hide(bar_track);

    // The net image points at the static buffer above and is never reallocated.
    lv_memset(s_netBuf, 0, sizeof(s_netBuf));
    s_netDsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    s_netDsc.header.cf     = LV_COLOR_FORMAT_RGB565A8;
    s_netDsc.header.flags  = 0;
    s_netDsc.header.w      = NET_W;
    s_netDsc.header.h      = NET_H;
    s_netDsc.header.stride = NET_W * 2;
    s_netDsc.data_size     = sizeof(s_netBuf);
    s_netDsc.data          = s_netBuf;

    img_net = lv_image_create(scr);
    lv_image_set_src(img_net, &s_netDsc);
    lv_obj_set_pos(img_net, NET_X, NET_Y);
    hide(img_net);

    // Face letters, one per face, sitting on the centre sticker. The net alone
    // relies on the reader knowing the unfolded-cross convention; the machine
    // has U R F D L B written on it, so saying the same thing here turns an
    // inferred instruction into a literal one.
    for (int f = 0; f < 6; ++f) {
        lbl_netFace[f] = lv_label_create(scr);
        lv_obj_set_size(lbl_netFace[f], NET_STICKER, NET_STICKER);
        lv_obj_set_pos(lbl_netFace[f],
                       NET_X + (kNetFaces[f].col + 1) * NET_CELL,
                       NET_Y + (kNetFaces[f].row + 1) * NET_CELL);
        lv_obj_set_style_text_font(lbl_netFace[f], &lv_font_prev_9, 0);
        lv_obj_set_style_text_align(lbl_netFace[f], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(lbl_netFace[f], kFaceNames[f]);
        hide(lbl_netFace[f]);
    }

    bar_fill = lv_obj_create(bar_track);
    makeBare(bar_fill);
    lv_obj_set_size(bar_fill, 0, PBAR_H - 4);
    lv_obj_set_pos(bar_fill, 1, 1);
    lv_obj_set_style_radius(bar_fill, 1, 0);
    lv_obj_set_style_bg_color(bar_fill, lv_color_hex(0xFBFF47), 0);   // the theme's cursor yellow
    lv_obj_set_style_bg_opa(bar_fill, LV_OPA_COVER, 0);
    hide(bar_fill);
}

void CubeDisplay::setOpProgress(int done, int total) {
    if (!bar_track) return;
    if (total <= 0) { hide(bar_track); hide(bar_fill); return; }
    if (done < 0)     done = 0;
    if (done > total) done = total;

    int w = (PBAR_W - 4) * done / total;
    if (w < 0) w = 0;
    lv_obj_set_width(bar_fill, w);
    // A zero-width child still draws its border, which would read as a sliver
    // of progress before anything has happened.
    if (w == 0) hide(bar_fill); else show(bar_fill);
    show(bar_track);
}

// (built in buildOpUi; declared here next to its only user)
MenuTheme CubeDisplay::themeForKind(OpKind kind) {
    switch (kind) {
    case OpKind::Scan:      return MenuTheme::Blue;
    case OpKind::Solve:     return MenuTheme::Green;
    case OpKind::Calibrate: return MenuTheme::Violet;
    case OpKind::Done:      return MenuTheme::Green;
    case OpKind::Error:     return MenuTheme::Red;
    case OpKind::Info:
    default:                return MenuTheme::Yellow;
    }
}

void CubeDisplay::clearOpExtras() {
    for (int i = 0; i < kOpLines; ++i) { hide(lbl_line[i]); hide(lbl_lineVal[i]); }
    for (int b = 0; b < 2; ++b) {
        hide(lbl_chipRow[b]);
        for (int i = 0; i < kChipCount; ++i) hide(chip[b][i]);
    }
    for (int i = 0; i < kFaceCount; ++i) hide(lbl_faceCap[i]);
    for (int i = 0; i < kRows; ++i) hide(barBox[i]);
    hide(img_orb);
    hide(img_comma);
    hide(img_sonar[0]);
    hide(img_sonar[1]);
    hide(bar_track);
    hide(bar_fill);
    hide(img_net);
    for (int f = 0; f < 6; ++f) hide(lbl_netFace[f]);
}

int8_t CubeDisplay::chipIndexForColor(char c) {
    switch (c) {
    case 'W': return 0;
    case 'Y': return 1;
    case 'R': return 2;
    case 'O': return 3;
    case 'G': return 4;
    case 'B': return 5;
    default:  return -1;    // 'U' from the sensors, 'X' from an unbuilt cube
    }
}

// ---------------------------------------------------------------------------
//  The cube as an unfolded net.
// ---------------------------------------------------------------------------
void CubeDisplay::setOpCubeNet(const char* facelets, bool labelFaces) {
    if (!img_net) return;
    if (!facelets) {
        hide(img_net);
        for (int f = 0; f < 6; ++f) hide(lbl_netFace[f]);
        return;
    }

    uint8_t* colour = s_netBuf;
    uint8_t* alpha  = s_netBuf + (NET_W * NET_H * 2);

    // Everything transparent first: the gaps between stickers are the panel's
    // own background showing through, not a drawn colour, so the net sits on
    // the themed backdrop instead of on a grey slab.
    lv_memset(alpha, 0x00, NET_W * NET_H);

    for (int f = 0; f < 6; ++f) {
        for (int k = 0; k < 9; ++k) {
            const int8_t ci = chipIndexForColor(facelets[kNetFaces[f].base + k]);

            const int cx = (kNetFaces[f].col + (k % 3)) * NET_CELL;
            const int cy = (kNetFaces[f].row + (k / 3)) * NET_CELL;

            // A sticker whose colour is not known is drawn as a hollow outline
            // rather than skipped, so a gap in the net reads as "this one is
            // wrong" instead of as an empty space.
            const uint16_t px = (ci >= 0) ? rgb565(kChipColors[ci]) : rgb565(0x30364A);

            for (int y = 0; y < NET_STICKER; ++y) {
                for (int x = 0; x < NET_STICKER; ++x) {
                    const bool edge = (ci < 0) &&
                                      (x != 0 && y != 0 &&
                                       x != NET_STICKER - 1 && y != NET_STICKER - 1);
                    if (edge) continue;               // hollow centre

                    const int i = (cy + y) * NET_W + (cx + x);
                    colour[i * 2 + 0] = (uint8_t)(px & 0xFF);
                    colour[i * 2 + 1] = (uint8_t)(px >> 8);
                    alpha[i] = 0xFF;
                }
            }
        }
    }

    // Letters take their ink from the centre sticker they land on, so a face
    // whose colour was not read (drawn hollow) gets the light one.
    for (int f = 0; f < 6; ++f) {
        if (!labelFaces) { hide(lbl_netFace[f]); continue; }
        const int8_t centre = chipIndexForColor(facelets[kNetFaces[f].base + 4]);
        lv_obj_set_style_text_color(
            lbl_netFace[f],
            lv_color_hex(inkFor(centre >= 0 ? kChipColors[centre] : 0x30364A)), 0);
        show(lbl_netFace[f]);
    }

    // The image reads this buffer at draw time, so the only thing needed to
    // show the new state is telling LVGL the area is dirty.
    lv_obj_invalidate(img_net);
    show(img_net);

    // The net owns the middle of the screen, so the sub-line moves below it
    // rather than being drawn through the cube.
    lv_obj_set_pos(lbl_status, 48, NET_Y + NET_H + 6);
    show(lbl_status);
}

// ---------------------------------------------------------------------------
//  The themed operation screen.
//
//  Deliberately built from the menu's own parts. An operator watching a scan is
//  looking at the same frame, in the same place, with the same hint bar along
//  the bottom — only the colour and the contents change. The alternative, which
//  is what this replaced, was white text on black that shared nothing with the
//  rest of the machine's UI.
// ---------------------------------------------------------------------------
void CubeDisplay::showOperation(OpKind kind, const char* title,
                                const char* headline, const char* hint) {
    if (title)    Serial.println(title);
    if (headline) Serial.println(headline);
    if (!lbl_msg) return;

    setMode(Mode::Message);
    opActive = true;
    clearOpExtras();

    // The frame comes back, the navigation furniture does not. The preview
    // pane and the NEXT SCREEN outline are both statements about where
    // selecting the current item would take you, and during an operation there
    // is no current item — leaving them up put the headline and the status rows
    // straight across a pane advertising a menu that is not on screen.
    show(img_bandFill);
    show(img_bandEdge);
    show(menuGroup);
    hide(img_pane);
    hide(img_nextFrame);
    hide(img_nextLabel);
    for (int i = 0; i < kPreviewLines; ++i) hide(lbl_prev[i]);
    for (int i = 0; i < 2; ++i) hide(ghost[i]);

    applyTheme(themeForKind(kind));

    if (title) { lv_label_set_text(lbl_title, title); show(lbl_title); }
    else       { hide(lbl_title); }

    lv_obj_set_style_text_font(lbl_msg, &lv_font_head_16, 0);
    lv_obj_set_style_text_color(lbl_msg, lv_color_hex(COL_OP_HEAD), 0);
    lv_obj_set_style_text_align(lbl_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_msg, LX - 96);
    lv_obj_set_pos(lbl_msg, 48, OP_HEAD_Y);
    lv_label_set_text(lbl_msg, headline ? headline : "");
    show(lbl_msg);

    lv_obj_set_style_text_font(lbl_status, &lv_font_bar_12, 0);
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(COL_OP_KEY), 0);
    lv_obj_set_style_text_align(lbl_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_status, LX - 96);
    lv_obj_set_pos(lbl_status, 48, OP_SUB_Y);
    lv_label_set_text(lbl_status, "");
    show(lbl_status);

    hide(lbl_footer);

    // The hint bar is the menu's description box, so a running operation and a
    // menu screen put their bottom line in exactly the same place.
    if (hint && *hint) {
        lv_label_set_text(lbl_desc, hint);
        lv_obj_align(lbl_desc, LV_ALIGN_CENTER, 0, 0);
        detailExec(this, 255);
        show(box_desc);
    } else {
        hide(box_desc);
    }
}

void CubeDisplay::setOpLines(const char* const* lines, int count) {
    if (!lbl_line[0]) return;
    if (count > kOpLines) count = kOpLines;
    if (count < 0)        count = 0;

    // Rows follow whatever is above them: a screen that is only a status list
    // starts high, one with a headline clears it, one with a sub-line clears
    // that too. Otherwise a short screen floats and a full one collides.
    const bool haveHead = (lbl_msg    && lv_label_get_text(lbl_msg)[0]    != '\0');
    const bool haveSub  = (lbl_status && lv_label_get_text(lbl_status)[0] != '\0');
    int top = haveHead ? OP_LINE_Y : 60;
    if (haveSub) top += OP_LINE_STEP;

    for (int i = 0; i < kOpLines; ++i) {
        if (i >= count || lines[i] == nullptr) {
            hide(lbl_line[i]);
            hide(lbl_lineVal[i]);
            continue;
        }

        const int y = top + i * OP_LINE_STEP;
        lv_obj_set_pos(lbl_line[i], OP_LINE_X, y);
        lv_obj_set_pos(lbl_lineVal[i], OP_LINE_X, y);

        const char* tab = nullptr;
        for (const char* c = lines[i]; *c; ++c) { if (*c == '\t') { tab = c; break; } }

        if (tab) {
            // Key on the left, value hard right. Copied rather than pointed at
            // because the caller's string is one buffer with a tab in it.
            char key[40];
            size_t n = (size_t)(tab - lines[i]);
            if (n >= sizeof(key)) n = sizeof(key) - 1;
            for (size_t k = 0; k < n; ++k) key[k] = lines[i][k];
            key[n] = '\0';

            lv_label_set_text(lbl_line[i], key);
            lv_label_set_text(lbl_lineVal[i], tab + 1);
            show(lbl_line[i]);
            show(lbl_lineVal[i]);
        } else {
            lv_label_set_text(lbl_line[i], lines[i]);
            lv_obj_set_style_text_color(lbl_line[i], lv_color_hex(COL_OP_VALUE), 0);
            show(lbl_line[i]);
            hide(lbl_lineVal[i]);
        }
    }
}

void CubeDisplay::setOpFaces(const int8_t* faces, int activeA, int activeB) {
    if (!chip[0][0] || !faces) return;

    // The face row and the calibration rows share these chip objects, so both
    // set size and position every time rather than trusting what was left.
    hide(lbl_chipRow[0]);
    hide(lbl_chipRow[1]);
    for (int i = 0; i < kChipCount; ++i) hide(chip[1][i]);

    for (int i = 0; i < kFaceCount; ++i) {
        lv_obj_t* c = chip[0][i];
        lv_obj_set_size(c, FACE_W, FACE_H);
        lv_obj_set_pos(c, FACE_X + i * (FACE_W + FACE_GAP), FACE_Y);

        const int8_t col = faces[i];
        const bool   got = (col >= 0 && col < 6);
        const bool   busy = (i == activeA || i == activeB);

        // A read face is filled with the colour its centre sticker actually
        // came back as — so the row is a readout, not just a tally. One that
        // has not been read yet is an empty outline.
        if (got) {
            lv_obj_set_style_bg_color(c, lv_color_hex(kChipColors[col]), 0);
            lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(c, lv_color_hex(kChipColors[col]), 0);
            lv_obj_set_style_border_opa(c, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(c, 1, 0);
        } else {
            lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_color(c, lv_color_hex(COL_OP_KEY), 0);
            lv_obj_set_style_border_opa(c, busy ? LV_OPA_COVER : 110, 0);
            lv_obj_set_style_border_width(c, busy ? 2 : 1, 0);
        }

        // The pair under the sensors right now gets a bright rim, which is what
        // the old step rows were really communicating.
        if (busy) {
            lv_obj_set_style_border_color(c, lv_color_hex(0xFBFF47), 0);
            lv_obj_set_style_border_opa(c, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(c, 2, 0);
        }

        lv_obj_set_pos(lbl_faceCap[i], FACE_X + i * (FACE_W + FACE_GAP), FACE_CAP_Y);
        lv_obj_set_style_text_color(lbl_faceCap[i],
                                    lv_color_hex(busy ? 0xFBFF47 : COL_OP_KEY), 0);
        show(c);
        show(lbl_faceCap[i]);
    }
}

void CubeDisplay::setOpChips(const uint8_t* bits, int boards) {
    if (!chip[0][0] || !bits) return;
    if (boards > 2) boards = 2;

    for (int b = 0; b < 2; ++b) {
        if (b >= boards) {
            hide(lbl_chipRow[b]);
            for (int i = 0; i < kChipCount; ++i) hide(chip[b][i]);
            continue;
        }
        show(lbl_chipRow[b]);
        for (int i = 0; i < kChipCount; ++i) {
            // Restore this row's own geometry: setOpFaces() borrows these same
            // objects at a different size and position.
            lv_obj_set_size(chip[b][i], CHIP_W, CHIP_H);
            lv_obj_set_pos(chip[b][i], CHIP_X + i * (CHIP_W + CHIP_GAP),
                           CHIP_Y + b * CHIP_ROW_STEP);
            lv_obj_set_style_bg_color(chip[b][i], lv_color_hex(kChipColors[i]), 0);
            lv_obj_set_style_border_color(chip[b][i], lv_color_hex(kChipColors[i]), 0);
            lv_obj_set_style_border_width(chip[b][i], 1, 0);

            const bool got = (bits[b] >> i) & 1u;
            // Captured colours are solid; the rest are just their own outline,
            // so the row reads as a checklist rather than as decoration.
            lv_obj_set_style_bg_opa(chip[b][i], got ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_opa(chip[b][i], got ? LV_OPA_COVER : 110, 0);
            show(chip[b][i]);
        }
    }
}

void CubeDisplay::barBoxPos(int i, int rows, int& x, int& y) {
    // Five items pack tighter than four or fewer; the design gives each count
    // its own start and pitch rather than centring one fixed ladder.
    const int startY = (rows >= 5) ? 70 : 76;
    const int step   = (rows >= 5) ? 27 : 29;
    x = ZIG_X[i % 5] - 6;                 // -6: the asset's glow margin
    y = startY + i * step - BOX_H / 2;
}

void CubeDisplay::applyTheme(MenuTheme t) {
    if (t == curTheme) return;            // avoid invalidating two big images
    curTheme = t;

    int idx = (int)t - 1;                 // Inherit is 0 and never reaches here
    if (idx < 0 || idx > 5) idx = 0;
    tint(img_bandFill, kPalette[idx].fill);
    tint(img_bandEdge, kPalette[idx].edge);
}

void CubeDisplay::setPreview(const MenuItem* item) {
    const int n = (item && item->preview) ? (int)item->previewCount : 0;

    for (int i = 0; i < kPreviewLines; ++i) {
        if (i < n && item->preview[i]) {
            lv_label_set_text(lbl_prev[i], item->preview[i]);
            show(lbl_prev[i]);
        } else {
            hide(lbl_prev[i]);
        }
    }
    for (int i = 0; i < 2; ++i) {
        if (n == 0) show(ghost[i]); else hide(ghost[i]);
    }
}

void CubeDisplay::placeCursor(int row, int rows) {
    int x, y;
    barBoxPos(row, rows, x, y);
    const int cx = x + SOCKET_CX;
    const int cy = y + SOCKET_CY;

    lv_obj_set_pos(img_orb,   cx - THEME_ORB_W / 2,   cy - THEME_ORB_H / 2);
    lv_obj_set_pos(img_comma, cx - THEME_COMMA_W / 2, cy - THEME_COMMA_H / 2);
    for (int i = 0; i < 2; ++i) {
        lv_obj_set_pos(img_sonar[i], cx - THEME_SONAR_W / 2, cy - THEME_SONAR_H / 2);
    }
    show(img_orb);
    show(img_comma);
    show(img_sonar[0]);
    show(img_sonar[1]);
}

// ---------------------------------------------------------------------------
//  Cursor animations: the sonar rings and the spinning comma.
//
//  Started ONCE, and left running for the life of the program. They drive the
//  cursor images, which merely change position as the selection moves, so there
//  is nothing to restart — and restarting would visibly reset the comma's angle
//  and the ring phase on every detent of the wheel. While the panel is showing
//  a message instead of the menu these keep ticking against hidden objects,
//  which costs a few float operations and invalidates nothing.
// ---------------------------------------------------------------------------
void CubeDisplay::startCursorAnims() {
    for (int i = 0; i < 2; ++i) {
        lv_image_set_pivot(img_sonar[i], THEME_SONAR_W / 2, THEME_SONAR_H / 2);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, img_sonar[i]);
        lv_anim_set_exec_cb(&a, sonarExec);
        lv_anim_set_values(&a, 0, 1000);
        lv_anim_set_duration(&a, SONAR_CYCLE_MS);
        lv_anim_set_delay(&a, i * SONAR_OFFSET_MS);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
    }

    lv_image_set_pivot(img_comma, THEME_COMMA_W / 2, THEME_COMMA_H / 2);
    lv_anim_t c;
    lv_anim_init(&c);
    lv_anim_set_var(&c, img_comma);
    lv_anim_set_exec_cb(&c, commaExec);
    lv_anim_set_values(&c, 0, 3600);          // LVGL angles are tenths of a degree
    lv_anim_set_duration(&c, COMMA_SPIN_MS);
    lv_anim_set_repeat_count(&c, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&c);
}

// A ring is born at the outer radius and travels inward, fading as it goes.
// The asset is baked at the LARGEST scale in the range and only ever shrinks,
// so the stroke thins the way a CSS transform:scale() would.
void CubeDisplay::sonarExec(void* var, int32_t v) {
    lv_obj_t* img = (lv_obj_t*)var;
    const float p = v / 1000.0f;

    if (p > SONAR_TRAVEL) {                   // the rest of the cycle is a pause
        lv_obj_set_style_image_opa(img, LV_OPA_TRANSP, 0);
        return;
    }

    const float t = p / SONAR_TRAVEL;         // 0..1 across the travel
    const float scale = 1.7f + (0.5f - 1.7f) * t;
    lv_image_set_scale(img, (uint32_t)lroundf(256.0f * scale / 1.7f));

    const float o = (t < SONAR_PEAK) ? (t / SONAR_PEAK)
                                     : (1.0f - (t - SONAR_PEAK) / (1.0f - SONAR_PEAK));
    lv_obj_set_style_image_opa(img, (lv_opa_t)lroundf(SONAR_OPA * o), 0);
}

void CubeDisplay::commaExec(void* var, int32_t v) {
    lv_image_set_rotation((lv_obj_t*)var, v);
}

void CubeDisplay::setMode(Mode m) {
    if (mode == m || lbl_msg == nullptr) return;
    mode = m;

    if (m == Mode::List) {
        opActive = false;
        clearOpExtras();
        // Put back what an operation screen hid.
        show(img_pane);
        show(img_nextFrame);
        show(img_nextLabel);
        hide(lbl_msg);
        hide(lbl_status);
        hide(lbl_footer);
        show(img_pane);
        show(img_nextFrame);
        show(img_bandFill);
        show(img_bandEdge);
        show(img_nextLabel);
        show(menuGroup);
        // Bars, preview lines and the cursor are shown selectively by
        // showList(); leave them alone here so a shorter screen does not
        // inherit stale rows from a longer one.
    } else {
        show(lbl_msg);
        show(lbl_status);
        hide(img_pane);
        hide(img_nextFrame);
        hide(img_bandFill);
        hide(img_bandEdge);
        hide(img_nextLabel);
        hide(menuGroup);
        hide(box_desc);
        for (int i = 0; i < kPreviewLines; ++i) hide(lbl_prev[i]);
        for (int i = 0; i < 2; ++i) hide(ghost[i]);

        // Cancel anything mid-flight and forget the frame. Without this, the
        // next return to the menu would either resume a transition against
        // content that has been replaced, or wheel in from wherever the bars
        // were abandoned.
        lv_anim_delete(this, nullptr);
        transitioning = false;
        curRows = 0;
    }
}

// Serial output happens BEFORE the widget guard in each of these.
//
// A machine whose panel failed to initialise is exactly the machine whose
// operator needs the text most, and it is the only remaining way to see what it
// is doing. Printing after an early return would make the console go quiet
// precisely when the screen did.
// Progress updates from inside a running operation.
//
// These are called from deep in CubeSystem, which knows what the machine is
// doing but nothing about how the panel is dressed. When an operation screen is
// already up they just replace its headline or sub-line, leaving the frame,
// title, hint bar and any step rows exactly as the caller that opened the
// screen arranged them. Rebuilding the screen here instead would make every
// progress tick flash the whole panel.
void CubeDisplay::setMessage(const char* msg) {
    Serial.println(msg ? msg : "");
    if (!lbl_msg) return;

    if (opActive && mode == Mode::Message) {
        lv_label_set_text(lbl_msg, msg ? msg : "");
        return;
    }

    setMode(Mode::Message);
    hide(lbl_title);
    hide(lbl_footer);
    lv_obj_set_style_text_align(lbl_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_msg, 12, 60);
    lv_label_set_text(lbl_msg, msg ? msg : "");
}

void CubeDisplay::setStatus(const char* msg) {
    Serial.println(msg ? msg : "");
    if (!lbl_status) return;

    if (opActive && mode == Mode::Message) {
        lv_label_set_text(lbl_status, msg ? msg : "");
        return;
    }

    setMode(Mode::Message);
    lv_label_set_text(lbl_status, msg ? msg : "");
}

void CubeDisplay::clearStatus() {
    if (lbl_status) {
        lv_label_set_text(lbl_status, "");
    }
}

// Kept for callers that have a title and one block of text and do not care
// about the rest. Routed through the themed screen so there is only ever one
// operation look on this panel.
void CubeDisplay::showMessage(const char* title, const char* body, const char* footer) {
    showOperation(OpKind::Info, title, body, footer);
}

// ---------------------------------------------------------------------------
//  The themed menu screen.
//
//  moreAbove/moreBelow are accepted and ignored: this design has no scroll
//  hints, because it has no scrolling screens. Every menu on the machine is
//  five items or fewer, which is the same limit that shaped the layout — and if
//  one ever grows past it, the honest fix is splitting the screen, not adding a
//  chevron the design has nowhere to put.
// ---------------------------------------------------------------------------
void CubeDisplay::showList(const MenuScreen*      screen,
                           const MenuItem* const* items,
                           int                    rows,
                           int                    selectedRow,
                           bool                   moreAbove,
                           bool                   moreBelow,
                           MenuNav                nav) {
    (void)moreAbove;
    (void)moreBelow;

    if (!screen || !items || !menuGroup) return;
    setMode(Mode::List);

    if (rows > kRows) rows = kRows;
    if (rows < 0)     rows = 0;
    if (selectedRow < 0)     selectedRow = 0;
    if (selectedRow >= rows) selectedRow = (rows > 0) ? rows - 1 : 0;
    if (rows == 0) return;

    const bool screenChange = (nav == MenuNav::Enter ||
                               nav == MenuNav::Back  ||
                               nav == MenuNav::Root);

    // A wheel only makes sense when there are bars on the panel to sweep away.
    // curRows is zeroed whenever the menu loses the panel, so coming back from
    // a scan or an error draws the screen outright instead of animating from
    // wherever the last transition happened to leave things.
    if (screenChange && curRows > 0 && !transitioning) {
        pendScreen = screen;
        for (int i = 0; i < rows; ++i) pendItems[i] = items[i];
        pendRows = (int8_t)rows;
        pendSel  = (int8_t)selectedRow;
        pendDir  = (nav == MenuNav::Back) ? -1 : 1;

        // Title, frame colour, caption and preview all change at the START of
        // the transition, not the end — the design is explicit about this, and
        // it is what makes the new screen feel like it is already arriving
        // while the old items are still clearing.
        lv_label_set_text(lbl_title, screen->title ? screen->title : "");
        applyTheme(CubeMenu::themeOf(screen, items[selectedRow]));
        swapDetail(items[selectedRow], false);

        startWheel(pendDir);
        return;
    }

    // A redraw landed mid-transition. Drop the animation rather than let it
    // finish over content it no longer matches.
    if (transitioning) {
        lv_anim_delete(this, nullptr);
        transitioning = false;
    }

    applyScreen(screen, items, rows, selectedRow);

    // Moving the cursor cross-fades the caption and preview; arriving at a
    // screen for the first time just shows them.
    swapDetail(items[selectedRow], nav != MenuNav::Move);
}

// Everything that is true of a frame the moment it is drawn. The caption and
// preview are NOT here: they run on their own fade.
void CubeDisplay::applyScreen(const MenuScreen*      screen,
                              const MenuItem* const* items,
                              int                    rows,
                              int                    selectedRow) {
    lv_label_set_text(lbl_title, screen->title ? screen->title : "");
    show(lbl_title);

    for (int i = 0; i < kRows; ++i) {
        if (i >= rows) {
            hide(barBox[i]);
            continue;
        }

        int x, y;
        barBoxPos(i, rows, x, y);
        lv_obj_set_pos(barBox[i], x, y);

        const bool sel = (i == selectedRow);

        // Selection swaps the whole bar image rather than restyling one: the
        // difference between the two states is a flat yellow body with a yellow
        // glow versus a near-black body with a gold one, plus the socket
        // furniture appearing or not. That is two bakes, not a style tweak.
        lv_image_set_src(img_bar[i], sel ? &theme_bar_sel : &theme_bar_unsel);
        lv_label_set_text(lbl_bar[i], items[i]->label ? items[i]->label : "");
        lv_obj_set_style_text_color(lbl_bar[i],
                                    lv_color_hex(sel ? COL_LABEL_ON : COL_LABEL_OFF), 0);

        // Undo whatever a transition left behind.
        lv_obj_set_style_image_opa(img_bar[i], LV_OPA_COVER, 0);
        lv_obj_set_style_text_opa(lbl_bar[i], LV_OPA_COVER, 0);
        show(barBox[i]);
    }

    // The frame takes the SELECTED item's colour, not the screen's — that is
    // what makes moving the cursor recolour the whole frame.
    applyTheme(CubeMenu::themeOf(screen, items[selectedRow]));
    placeCursor(selectedRow, rows);

    curRows = (int8_t)rows;
    curSel  = (int8_t)selectedRow;
    show(box_desc);
}

void CubeDisplay::applyDetail(const MenuItem* item) {
    lv_label_set_text(lbl_desc, (item && item->caption) ? item->caption : "");
    lv_obj_align(lbl_desc, LV_ALIGN_CENTER, 0, 0);
    setPreview(item);
}

// Fades the description and preview out, swaps them at the bottom, fades back.
// `instant` skips all of that, which is what a screen change wants: its chrome
// is already changing under a wheel.
void CubeDisplay::swapDetail(const MenuItem* item, bool instant) {
    lv_anim_delete(this, detailExec);

    if (instant) {
        applyDetail(item);
        detailExec(this, 255);
        return;
    }

    pendDetail = item;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, this);
    lv_anim_set_exec_cb(&a, detailExec);
    lv_anim_set_values(&a, 255, 0);
    lv_anim_set_duration(&a, DETAIL_FADE_MS);
    lv_anim_set_completed_cb(&a, detailFadedOut);
    lv_anim_start(&a);
}

void CubeDisplay::detailExec(void* var, int32_t v) {
    CubeDisplay* d = (CubeDisplay*)var;
    if (v < 0)   v = 0;
    if (v > 255) v = 255;

    // Scaled against each element's own resting opacity, so a full fade-in
    // lands back on the design's values rather than on flat 255.
    lv_obj_set_style_text_opa(d->lbl_desc, (lv_opa_t)(v * OPA_DESC_TEXT / 255), 0);
    for (int i = 0; i < kPreviewLines; ++i) {
        lv_obj_set_style_text_opa(d->lbl_prev[i], (lv_opa_t)v, 0);
    }
    for (int i = 0; i < 2; ++i) {
        lv_obj_set_style_border_opa(d->ghost[i], (lv_opa_t)(v * OPA_GHOST / 255), 0);
    }
}

void CubeDisplay::detailFadedOut(lv_anim_t* a) {
    CubeDisplay* d = (CubeDisplay*)a->var;
    d->applyDetail(d->pendDetail);

    lv_anim_t b;
    lv_anim_init(&b);
    lv_anim_set_var(&b, d);
    lv_anim_set_exec_cb(&b, detailExec);
    lv_anim_set_values(&b, 0, 255);
    lv_anim_set_duration(&b, DETAIL_FADE_MS);
    lv_anim_start(&b);
}

// ---------------------------------------------------------------------------
//  The wheel.
//
//  The design rotates the whole bar group about a pivot off-screen right. LVGL
//  cannot do that here: rotating a container renders it through a layer, and a
//  transformed layer is allocated whole — 320x240 at 16 bpp is 150 KB against a
//  32 KB pool, and even a single 167x56 bar box is ~19 KB. Both fail, and with
//  LV_USE_LOG at 0 they fail by drawing nothing.
//
//  So each bar is moved along the arc its centre would have travelled, without
//  tilting. That reproduces the paths exactly — including the way lower bars
//  sweep further, which is the most legible part of the effect — and costs two
//  trig calls and five lv_obj_set_pos() per frame. The bars do not rotate about
//  their own centres; at 24 degrees under a 130 ms fade that reads as a sweep
//  rather than as a missing effect.
// ---------------------------------------------------------------------------
void CubeDisplay::placeBarsAt(float deg, int rows, lv_opa_t opa) {
    const float rad = deg * 0.01745329252f;
    const float c = cosf(rad);
    const float s = sinf(rad);

    for (int i = 0; i < rows && i < kRows; ++i) {
        int bx, by;
        barBoxPos(i, rows, bx, by);

        const float dx = bx + BOX_W * 0.5f - PIVOT_X;
        const float dy = by + BOX_H * 0.5f - PIVOT_Y;

        lv_obj_set_pos(barBox[i],
                       (int)lroundf(PIVOT_X + dx * c - dy * s - BOX_W * 0.5f),
                       (int)lroundf(PIVOT_Y + dx * s + dy * c - BOX_H * 0.5f));

        lv_obj_set_style_image_opa(img_bar[i], opa, 0);
        lv_obj_set_style_text_opa(lbl_bar[i], opa, 0);
    }
}

void CubeDisplay::startWheel(int8_t dir) {
    (void)dir;
    transitioning = true;

    // The cursor does not ride the wheel: it belongs to the selected item, and
    // which item that is changes halfway through.
    hide(img_orb);
    hide(img_comma);
    hide(img_sonar[0]);
    hide(img_sonar[1]);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, this);
    lv_anim_set_exec_cb(&a, wheelOutExec);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, WHEEL_OUT_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&a, wheelOutDone);
    lv_anim_start(&a);
}

void CubeDisplay::wheelOutExec(void* var, int32_t v) {
    CubeDisplay* d = (CubeDisplay*)var;
    const float t = v / 1000.0f;

    float o = 1.0f - (t * WHEEL_OUT_MS - WHEEL_OUT_FADE_DELAY) / WHEEL_OUT_FADE_MS;
    if (o > 1.0f) o = 1.0f;
    if (o < 0.0f) o = 0.0f;

    d->placeBarsAt(d->pendDir * -WHEEL_OUT_DEG * t, d->curRows,
                   (lv_opa_t)lroundf(255.0f * o));
}

void CubeDisplay::wheelOutDone(lv_anim_t* a) {
    CubeDisplay* d = (CubeDisplay*)a->var;

    // The new screen is built pre-rotated and invisible, then wheels in.
    d->applyScreen(d->pendScreen, d->pendItems, d->pendRows, d->pendSel);
    d->placeBarsAt(d->pendDir * WHEEL_IN_DEG, d->pendRows, LV_OPA_TRANSP);
    hide(d->img_orb);
    hide(d->img_comma);
    hide(d->img_sonar[0]);
    hide(d->img_sonar[1]);

    lv_anim_t b;
    lv_anim_init(&b);
    lv_anim_set_var(&b, d);
    lv_anim_set_exec_cb(&b, wheelInExec);
    lv_anim_set_values(&b, 0, 1000);
    lv_anim_set_duration(&b, WHEEL_IN_MS);
    lv_anim_set_path_cb(&b, lv_anim_path_overshoot);
    lv_anim_set_completed_cb(&b, wheelInDone);
    lv_anim_start(&b);
}

void CubeDisplay::wheelInExec(void* var, int32_t v) {
    CubeDisplay* d = (CubeDisplay*)var;

    // The overshoot path deliberately runs past 1000, which carries the bars a
    // little beyond level before settling — that is the design's easing, not a
    // rounding error, so t is not clamped here.
    const float t = v / 1000.0f;

    float o = (t * WHEEL_IN_MS) / WHEEL_IN_FADE_MS;
    if (o > 1.0f) o = 1.0f;
    if (o < 0.0f) o = 0.0f;

    d->placeBarsAt(d->pendDir * WHEEL_IN_DEG * (1.0f - t), d->pendRows,
                   (lv_opa_t)lroundf(255.0f * o));
}

void CubeDisplay::wheelInDone(lv_anim_t* a) {
    CubeDisplay* d = (CubeDisplay*)a->var;
    d->transitioning = false;
    d->placeBarsAt(0.0f, d->pendRows, LV_OPA_COVER);
    d->placeCursor(d->pendSel, d->pendRows);
}

void CubeDisplay::update() {
    lv_task_handler();
}

void CubeDisplay::waitForSelect(const char* msg) {
    setMessage(msg);
    setStatus("Press SELECT");
    update();
    delay(5);

    // Wait for button release first
    while (menuEncoder.selectPressed()) {
        update();
        delay(10);
    }

    // Wait for button press
    while (!menuEncoder.selectPressed()) {
        update();
        delay(10);
    }

    clearStatus();

    // Wait for button release again
    while (menuEncoder.selectPressed()) {
        update();
        delay(10);
    }
}

void CubeDisplay::flush_cb_wrapper(lv_display_t* disp,
                                    const lv_area_t* area,
                                    uint8_t* px_map) {
    if (instance) {
        instance->flushDisplay(disp, area, px_map);
    }
}

void CubeDisplay::flushDisplay(lv_display_t* disp,
                                const lv_area_t* area,
                                uint8_t* px_map) {
    if (!tft) return;

    bool redraw_now = lv_disp_flush_is_last(disp);

    tft->updateRegion(
        redraw_now,
        (uint16_t*)px_map,
        area->x1, area->x2,
        area->y1, area->y2
    );

    lv_disp_flush_ready(disp);
}
