// CubeDisplay.cpp
#include "CubeDisplay.h"
#include "CubeHardwareConfig.h"  // For menuEncoder

#include <math.h>
#include <string.h>

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
      mode(Mode::None), opActive(false), opKind(OpKind::Info),
      pendScreen(nullptr), pendDetail(nullptr),
      pendRows(0), pendSel(0), pendDir(1),
      curRows(0), transitioning(false)
{
    for (int i = 0; i < kRows; ++i) {
        barBox[i] = nullptr; img_bar[i] = nullptr; lbl_bar[i] = nullptr;
    }
    for (int i = 0; i < kPreviewLines; ++i) lbl_prev[i] = nullptr;
    for (int i = 0; i < 2; ++i) { ghost[i] = nullptr; img_sonar[i] = nullptr; }
    for (int i = 0; i < kRows; ++i) pendItems[i] = nullptr;
    for (int i = 0; i < kOpLines; ++i) {
        lbl_line[i] = nullptr; lbl_lineVal[i] = nullptr; row_dot[i] = nullptr;
    }
    for (int b = 0; b < 2; ++b) {
        lbl_chipRow[b] = nullptr;
        for (int i = 0; i < kChipMax; ++i) chip[b][i] = nullptr;
    }
    for (int i = 0; i < kChipMax; ++i) lbl_faceCap[i] = nullptr;
    for (int i = 0; i < kRibbonSlots; ++i) lbl_ribbon[i] = nullptr;
    bar_track = nullptr;
    bar_fill  = nullptr;
    dial_arc    = nullptr;
    lbl_dial    = nullptr;
    lbl_dialCap = nullptr;
    img_net     = nullptr;
    img_prevNet = nullptr;
    badge_armed = nullptr;
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

    // Hard ceiling on the title, for the same reason PREV_W exists below:
    // measure the border, not the text.
    //
    // The title sits in a NOTCH in the frame band — a step down in the band's
    // top edge, and the one piece of the design it is meant to sit inside. From
    // the band path in docs/theme/melee-menu-lvgl-preview.html, in its 1200x900
    // viewBox scaled by 320/1200:
    //     L 168 139 -> L 515 139 -> L 570 86
    // the shelf runs x=45..137 and then rises diagonally to x=152. Text drawn
    // at y=21 spans about 13 px down, and the diagonal has only reached x=140
    // by the bottom of that, so 140 is the first thing a long title touches.
    //
    // Without a width an LVGL label auto-sizes to its content and LV_LABEL_
    // LONG_DOT never fires — it has nothing to clip against. That is what let
    // "Motor Calibration" and "Color Calibration" run out of the notch and
    // across the band. Same bug the message-mode labels had further down, and
    // it was fixed there the same way.
    //
    // A title too wide for this does NOT ellipsize and does NOT wrap — it
    // drops to the smaller Anton instead. See setTitleText(). The width stays
    // as a hard backstop for a title too long even for that, where clipping is
    // the least bad outcome, but the ladder is what is meant to handle it.
    const int TITLE_W = 86;
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

    // Colors that belong to no theme.
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
    // The dial. Centred on the content area and sized to the space between the
    // sub-line and the hint bar — big enough to read the arc from across the
    // room, which is the only reason to have it rather than a number.
    // Sized to what is actually free rather than to what looks generous: the
    // sub-line ends at ~94 and the hint box starts at 199, so 78 across at
    // y=96 leaves room for a caption UNDER the arc and nothing to spare.
    const int DIAL_D  = 78;
    const int DIAL_X  = (320 - DIAL_D) / 2;
    const int DIAL_Y  = 96;
    const int DIAL_W  = 8;    // arc thickness

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

    // The boolean block that stands in for a status row's value. Sized to the
    // row rather than to the text it replaces: a mark you have to look twice
    // at is worse than the word it was meant to improve on, and this one is
    // read from bench distance. Parked hard right, where the value would be,
    // so a table of them lines up down one edge.
    const int DOT_W = 16, DOT_H = 12;

    // The two-group sensor grid: a caption and a 3x3 per board, stacked, the
    // way the operator describes the boards. The vertical budget is what fixes
    // the cell size — the usable area runs from about y=54 to the hint box at
    // 199, and two captions plus six rows of cells is already 138 of those 145
    // px, which is why the cells are wider than they are tall.
    const int GRID_W = 24, GRID_H = 16, GRID_GAP = 3;
    const int GRID_SPAN   = 3 * GRID_W + 2 * GRID_GAP;   // 78
    const int GRID_X      = (320 - GRID_SPAN) / 2;       // centred, like the chip row
    const int GRID_CAP_H  = 12;                          // caption line, above its cells
    const int GRID_TOP[2] = { 54, 126 };

    // The ARMED badge, right-aligned to the same x=268 the status rows end at,
    // on the title's line. That strip is the one place on an operation screen
    // that is always free — the preview pane and the NEXT SCREEN outline are
    // both hidden by showOperation() — and the title already proves text is
    // legible over the band there.
    const int ARMED_W = 56, ARMED_H = 17;
    const int ARMED_X = 268 - ARMED_W, ARMED_Y = 18;
    const int ARMED_MS = 900;              // one half of a breath
    const int ARMED_DIM = 130;             // how far down it breathes

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
    // The buffer MUST NOT come from LV_MEM: at 38 KB it is more than half the
    // whole pool. So it is a static array — but DMAMEM, not plain static.
    // The Teensy 4.1's 1 MB is not one pool: RAM1 is 512 KB shared between
    // variables and the code the linker puts in ITCM, and an undecorated
    // static lands there. This buffer was 38 KB of a RAM1 overflow that
    // stopped the firmware linking at all. DMAMEM puts it in RAM2, where the
    // framebuffer and the diff buffers already are and where there is room to
    // spare. LVGL reads it in place, once per invalidate, so the slower bus
    // does not matter. DMAMEM is not zero-initialised — the lv_memset in
    // begin() below is doing real work, not being tidy.
    const int NET_CELL = 11;              // sticker 10 px plus a 1 px gap
    const int NET_STICKER = 10;
    const int NET_COLS = 12, NET_ROWS = 9;
    const int NET_W = NET_COLS * NET_CELL;   // 120
    const int NET_H = NET_ROWS * NET_CELL;   // 90
    // Vertically centred in the frame's usable area rather than pushed to the
    // top. That area runs from below the band's top horizontal run (master
    // y=147, so 39 here) down to the description box at 199 — 160 px — and the
    // net plus its gap and sub-line is 119, which centres the block at y=60.
    const int NET_X = (320 - NET_W) / 2, NET_Y = 60;

    DMAMEM uint8_t s_netBuf[NET_W * NET_H * 3];    // RGB565 plane, then the A8 plane
    lv_image_dsc_t s_netDsc;

    // The same net at pane size, for a menu preview. 3 px stickers are small,
    // but a pattern is recognised by its arrangement rather than by reading
    // individual squares, and the pane has only ~62 px of usable width.
    const int PNET_CELL = 4, PNET_STICKER = 3;
    const int PNET_W = NET_COLS * PNET_CELL;   // 48
    const int PNET_H = NET_ROWS * PNET_CELL;   // 36
    const int PNET_X = 228, PNET_Y = 100;

    DMAMEM uint8_t s_pnetBuf[PNET_W * PNET_H * 3];
    lv_image_dsc_t s_pnetDsc;

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

    // Draw an unfolded cube into an RGB565A8 buffer. Shared by both sizes: the
    // only difference between the panel net and the pane preview is how big a
    // cell is, and duplicating this to change one number is how the two would
    // end up disagreeing about what a cube looks like.
    void drawNet(uint8_t* buf, int w, int h, int cell, int sticker,
                 const char* facelets);

    inline uint16_t rgb565(uint32_t c) {
        return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
    }

    // A face letter is printed ON its centre sticker, so it has to survive
    // being drawn over white, yellow, red, orange, green or blue. Pick by
    // perceived brightness rather than by a per-color table, which would need
    // revisiting every time the palette moves.
    inline uint32_t inkFor(uint32_t bg) {
        const uint32_t lum = (299u * ((bg >> 16) & 0xFF) +
                              587u * ((bg >>  8) & 0xFF) +
                              114u * ( bg        & 0xFF)) / 1000u;
        return (lum > 145u) ? 0x101018 : 0xF4F6FF;
    }

    // The move ribbon: seven fixed slots so the tokens do not shuffle sideways
    // as the cursor advances. A move is one to three characters, and a slot
    // wide enough for the longest keeps the row still.
    const int RIB_SLOT = 32, RIB_Y = 112;

    // Progress bar.
    const int PBAR_X = 60, PBAR_Y = 150, PBAR_W = 200, PBAR_H = 8;

    // The six cube colors, in the order setOpChips() expects its bits.
    //
    // SIX, while CubeDisplay::kChipMax is NINE. The chip objects are shared:
    // setOpChips() uses six of them as a per-color checklist, while
    // setOpChipRow() and setOpGrid() use up to all nine and color each from
    // the caller's fill. So this array is NOT indexable by a chip index, and
    // every loop that walks chips must decide which of the two counts it
    // means. Getting that wrong is an out-of-bounds read of a const array,
    // which does not crash — it draws whatever follows in memory.
    const int kChipColorCount = 6;
    const uint32_t kChipColors[kChipColorCount] = {
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
    const uint32_t COL_MARK_GOOD = 0x4ADE70;   // the palette's green edge
    const uint32_t COL_MARK_BAD  = 0xF0603A;   // its red edge
    const uint32_t COL_CURSOR    = 0xFBFF47;   // the theme's cursor yellow
    const uint32_t COL_MARK_BUSY = COL_CURSOR;
    const uint32_t COL_MARK_TUNED = 0xF5A623;  // amber: not the compiled
                                               // default. Warm like Busy so it
                                               // reads as "attention", but far
                                               // enough from it and from Bad
                                               // to be told apart at 11 px.
    const uint32_t COL_OP_KEY   = 0xA8B0C4;   // the label half of a status row
    const uint32_t COL_OP_VALUE = 0xEFF3FF;   // the value half

    // A grid cell whose sensor answered nonsense. Dark enough that it cannot
    // be read as one of the six sticker colors, filled so it cannot be read as
    // a cell nobody has asked yet; the red edge says which of the two it is.
    const uint32_t COL_CELL_FAULT = 0x2B1A22;

    // The badge is reverse video — near-black on near-white — because that is
    // the highest contrast available that belongs to no theme, and the point
    // of the badge is to say something the frame color no longer can.
    const uint32_t COL_ARMED_BG  = 0xEEF0F8;
    const uint32_t COL_ARMED_INK = 0x0A0A12;

    inline void hide(lv_obj_t* o) { if (o) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); }
    inline void show(lv_obj_t* o) { if (o) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN); }

    // lv_obj_create() comes with the default theme's panel look — background,
    // border, padding, scrollbars. Everything here is either a bare positioning
    // container or a surface styled from scratch, so strip it first.
    inline void makeBare(lv_obj_t* o) {
        lv_obj_remove_style_all(o);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    }

    // Recolor a white mask, which is what makes one asset serve all six
    // themes. The masks are RGB565A8 rather than A8 deliberately: LVGL reads an
    // uncompressed RGB565A8 straight out of flash, but copies every alpha-only
    // image into a RAM buffer first, and a 49 KB band does not fit beside the
    // widget set in the 64 KB LV_MEM pool. It fails silently there — LV_USE_LOG is 0 — and the band
    // simply never appears. See Code/tools/bake_theme.py.
    inline void tint(lv_obj_t* img, uint32_t rgb, lv_opa_t opa = LV_OPA_COVER) {
        lv_obj_set_style_image_recolor(img, lv_color_hex(rgb), 0);
        lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
        lv_obj_set_style_image_opa(img, opa, 0);
    }
}

// Put a title in the notch, at the largest size that fits on ONE line.
//
// The notch is TITLE_W wide (see there) and one line tall, so the three ways a
// label normally copes with not fitting are all wrong here: an ellipsis turns a
// name into a worse name, wrapping has nowhere to go, and clipping loses the
// end of the word. Shrinking the type keeps the whole title, which is what a
// title is for.
//
// Both rungs are Anton — the same face the design specifies, only smaller — so
// nothing about the screen's character changes when it steps down. Measured
// against the baked fonts, only "Motor Calibration" (90.4 px) and "Color
// Calibration" (86.2 px) need the small rung today; everything else fits the
// large one, and the widest title at 11 px is 76.4, so there is real headroom.
//
// The y shift is not a nudge by eye. LVGL positions a label by its TOP, and
// these two fonts put the baseline at different depths — line_height minus
// base_line is 14 for the 13 px face and 11 for the 11 px one. Without the
// difference the smaller title floats 3 px high in the notch. Deriving it from
// the font structs rather than hardcoding 3 means a re-bake cannot silently
// break the alignment.
void CubeDisplay::setTitleText(const char* title) {
    if (!lbl_title) return;
    if (!title) title = "";

    lv_point_t sz;
    lv_text_get_size(&sz, title, &lv_font_title_13, 0, 0,
                     LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const lv_font_t* f = (sz.x <= TITLE_W) ? &lv_font_title_13 : &lv_font_desc_11;

    const int32_t baseBig = (int32_t)lv_font_title_13.line_height - lv_font_title_13.base_line;
    const int32_t baseNow = (int32_t)f->line_height - f->base_line;

    lv_obj_set_style_text_font(lbl_title, f, 0);
    lv_obj_set_pos(lbl_title, TITLE_X, TITLE_Y + (baseBig - baseNow));
    lv_label_set_text(lbl_title, title);
}

bool CubeDisplay::begin(uint32_t spiSpeed) {
    // The driver's mirror framebuffer and diff buffers exist only in the async
    // DMA mode (see CUBE_DISPLAY_ASYNC_DMA in the header). In synchronous mode
    // there is nothing to mirror — every flush goes straight to the glass — and
    // the 166 KB they would take in RAM2 stays free.
#if CUBE_DISPLAY_ASYNC_DMA
    diff1 = new ILI9341_T4::DiffBuffStatic<8000>();
    diff2 = new ILI9341_T4::DiffBuffStatic<8000>();
    internal_fb = new uint16_t[LX * LY];
#else
    diff1 = nullptr;
    diff2 = nullptr;
    internal_fb = nullptr;
#endif

    // Not a pixel count: sizeof(lv_color_t) is 3 (LVGL 9 stores it as three
    // uint8_t), while the display format at LV_COLOR_DEPTH 16 is 2 bytes per
    // pixel. So this is 38400 bytes, which LVGL will use as 60 lines, not the
    // 40 the name suggests. It is nonetheless correct, because the byte count
    // handed to lv_display_set_buffers() below is the same expression — the two
    // can only agree while they stay written the same way. Change one and you
    // must change the other, or LVGL renders past the end of the allocation.
    lv_buf = new lv_color_t[LX * BUF_LINES];

    // These null checks look like dead code and are not. Standard C++ says a
    // failing `new` throws rather than returning null, but Teensyduino defines
    // operator new/new[] as a bare malloc() (cores/teensy4/new.cpp), so on this
    // part they really do hand back null. Do not "correct" this to std::nothrow
    // and do not delete the branch.
#if CUBE_DISPLAY_ASYNC_DMA
    const bool driverBufsOk = (diff1 && diff2 && internal_fb);
#else
    const bool driverBufsOk = true;
#endif
    if (!driverBufsOk || !lv_buf) {
        Serial.println("ERROR: Failed to allocate display buffers!");
        return false;
    }

    // lv_buf is zeroed because its uninitialised contents can reach the glass.
    // LVGL renders one partial rectangle into it and flushDisplay() hands that
    // rectangle straight to the driver, so any pixel inside the flushed area
    // that the renderer did not write is transmitted verbatim — heap garbage,
    // as a stray block of noise, in a different place on every power-up.
    //
    // LVGL pre-clears the partial buffer itself only when the display's colour
    // format carries alpha (lv_refr.c). At LV_COLOR_DEPTH 16 it does not, and
    // instead relies on finding an object that opaquely covers the whole
    // refreshed area — today the screen's own black background, with the
    // full-frame theme_bg over it. That invariant holds, but it is a property
    // of how the screens happen to be styled rather than anything the driver
    // enforces, and the symptom when a future screen breaks it is uninitialised
    // RAM painted on the panel. One memset at boot is far cheaper than ever
    // having to recognise that again. (memset, not lv_memset: lv_init() has not
    // run yet.)
    memset(lv_buf, 0, (size_t)LX * BUF_LINES * sizeof(lv_color_t));

    // (Async DMA mode only.) internal_fb is deliberately NOT cleared here,
    // which looks wrong: it is not scratch space but the differential driver's
    // record of what is already on the glass, and ILI9341_T4 transmits only the
    // pixels that differ from it. Uninitialised, the driver would believe
    // random bytes were already displayed and never paint them. The reason no
    // memset belongs here is that the driver already does it twice, below:
    // setFramebuffer() memsets the whole buffer and drops _mirrorfb to force a
    // full redraw, and clear() then fills the glass and the buffer with the
    // same colour before declaring them in sync. Adding a third would be dead
    // code. If this library is ever upgraded, re-check those two functions
    // before trusting this paragraph.

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

#if CUBE_DISPLAY_ASYNC_DMA
    // Double-buffered differential mode: uploads are asynchronous and only the
    // pixels that differ from internal_fb are sent.
    tft->setFramebuffer(internal_fb);
    tft->setDiffBuffers(diff1, diff2);
#endif
    // With no internal framebuffer the driver is in its NO_BUFFERING mode:
    // updateRegion() writes the flushed rectangle to the panel immediately
    // (_updateRectNow — one CASET/PASET window, the pixels, a NOP) and
    // returns when it is on the wire. redrawNow is ignored in that mode, so
    // the "buffer the bands, draw on the last one" protocol in flushDisplay()
    // degrades to "draw every band now", which is exactly what we want.
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

    // Make sure the boot frame is actually ON THE GLASS before returning:
    // picture first, motors second.
    //
    // In the async DMA mode updateRegion(redrawNow=true) only STARTS the
    // upload (~125 ms for a full frame at 10 MHz). begin() used to return with
    // that transfer in flight and CubeSystem::begin() attached the servos,
    // enabled and homed the steppers and lit the sensor LEDs on top of it —
    // the window in which the panel came up part picture, part coloured snow,
    // and a differential driver never re-sends damaged pixels (see
    // repaintAll()). This is the same _waitUpdateAsyncComplete() every flush
    // performs when a transfer is still running, so it adds no new code path.
    // In synchronous mode every flush already returned with its pixels on the
    // wire, so the wait finds nothing in flight; it is kept unconditional so
    // the two modes print the same boot log.
    //
    // Both Serial lines are deliberate: the driver's own "Hanging in
    // _waitUpdateAsyncComplete()" message reaches Serial through
    // tft->output(&Serial) above, and these bracket it so a stall here is
    // attributable at a glance.
    //
    // DO NOT swap this for tft->update(internal_fb) as a "push everything"
    // insurance. That was tried: the panel went white and the firmware hung.
    // update() handed the driver's OWN internal buffer forces a full redraw by
    // copying fb onto _fb1 through the rotation (DiffBuffBase::copyfb →
    // _copy_rotate_90), and with fb == _fb1 that is an in-place transpose of
    // 150 KB. The "every pixel" primitive for this driver is setFramebuffer()
    // followed by a whole-screen invalidate, which is repaintAll().
    //
    // The simulator stubs update(), setFramebuffer() and
    // waitUpdateAsyncComplete() (Code/sim/shim/ILI9341_T4.h), so nothing the
    // shim fakes is tested until it has run on the bench; the pool line below
    // is what proves this function got past the wait, so keep it after it.
    Serial.println(F("Display: waiting for the boot frame to reach the glass"));
    tft->waitUpdateAsyncComplete();
    Serial.println(F("Display: boot frame on glass"));

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

    // Say which transport this build uses. The two modes print an otherwise
    // identical boot log, and "which firmware is on the bench" has already
    // cost a round of diagnosis.
#if CUBE_DISPLAY_ASYNC_DMA
    Serial.println(F("Display mode: async DMA, differential (CUBE_DISPLAY_ASYNC_DMA=1)"));
#else
    Serial.println(F("Display mode: synchronous writes (CUBE_DISPLAY_ASYNC_DMA=0)"));
#endif
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
    // Shared by both modes. Deliberately no opaque bar behind it: the themed
    // background already separates the title from the content, and a filled
    // bar across the top would cut through the frame band's notch, which is
    // the one piece of the design the title is supposed to sit inside.
    lbl_title = lv_label_create(scr);
    lv_obj_set_pos(lbl_title, TITLE_X, TITLE_Y);
    lv_obj_set_width(lbl_title, TITLE_W);   // backstop; setTitleText() does the work
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(COL_TITLE), 0);
    lv_obj_set_style_text_opa(lbl_title, OPA_TITLE, 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_title_13, 0);
    // CLIP, not DOT and not WRAP. A screen title is a name: an ellipsis makes
    // it a worse name and a second line does not exist here — the notch is one
    // line tall. setTitleText() sizes the font so this never has to fire.
    lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_CLIP);
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
    // One parent for all five and the cursor furniture, so a mode change can
    // show or hide them at once. Transitions move each bar on its own — see
    // placeBarsAt().
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
    // parented to the group so a mode change shows and hides it with them.
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
//  hint bar are lbl_msg, lbl_status and box_desc respectively — all idle
//  whenever an operation is on screen, so reusing them costs nothing from a
//  pool that is already two thirds gone.
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

        // The boolean block, in the value column. A bare object rather than a
        // glyph because the baked fonts carry 0x20-0x7F and nothing else — a
        // filled circle or a ballot box would draw as an empty rectangle, in
        // silence, which is trap 4.5. Seven of these cost about as much pool
        // as one bar box; they are the cheapest thing on the screen.
        row_dot[i] = lv_obj_create(scr);
        makeBare(row_dot[i]);
        lv_obj_set_size(row_dot[i], DOT_W, DOT_H);
        lv_obj_set_pos(row_dot[i], OP_LINE_X + OP_LINE_W - DOT_W,
                       OP_LINE_Y + i * OP_LINE_STEP + 2);
        lv_obj_set_style_radius(row_dot[i], 2, 0);
        lv_obj_set_style_border_width(row_dot[i], 1, 0);
        hide(row_dot[i]);
    }

    // Color chips. Two rows because the two sensor boards see DIFFERENT
    // colors at each rotation — they finish the set at different moments, and
    // a single shared row would have to lie about one of them.
    for (int b = 0; b < 2; ++b) {
        lbl_chipRow[b] = lv_label_create(scr);
        lv_obj_set_pos(lbl_chipRow[b], OP_LINE_X, CHIP_Y + b * CHIP_ROW_STEP + 3);
        lv_obj_set_style_text_font(lbl_chipRow[b], &lv_font_prev_9, 0);
        lv_obj_set_style_text_color(lbl_chipRow[b], lv_color_hex(COL_OP_KEY), 0);
        lv_label_set_text(lbl_chipRow[b], b == 0 ? "BOARD 1" : "BOARD 2");
        hide(lbl_chipRow[b]);

        for (int i = 0; i < kChipMax; ++i) {
            chip[b][i] = lv_obj_create(scr);
            makeBare(chip[b][i]);
            lv_obj_set_size(chip[b][i], CHIP_W, CHIP_H);
            lv_obj_set_pos(chip[b][i],
                           CHIP_X + i * (CHIP_W + CHIP_GAP),
                           CHIP_Y + b * CHIP_ROW_STEP);
            lv_obj_set_style_radius(chip[b][i], 2, 0);
            // Only the first six get a cube color here — see kChipColors. The
            // last three exist for setOpChipRow() and setOpGrid(), which color
            // every chip themselves from what they are showing, so there is
            // nothing to seed them with.
            if (i < kChipColorCount) {
                lv_obj_set_style_bg_color(chip[b][i], lv_color_hex(kChipColors[i]), 0);
                lv_obj_set_style_border_color(chip[b][i], lv_color_hex(kChipColors[i]), 0);
            }
            lv_obj_set_style_border_width(chip[b][i], 1, 0);
            hide(chip[b][i]);
        }
    }

    for (int i = 0; i < kChipMax; ++i) {
        lbl_faceCap[i] = lv_label_create(scr);
        lv_obj_set_size(lbl_faceCap[i], FACE_W, 12);
        lv_obj_set_pos(lbl_faceCap[i], FACE_X + i * (FACE_W + FACE_GAP), FACE_CAP_Y);
        lv_obj_set_style_text_font(lbl_faceCap[i], &lv_font_prev_9, 0);
        lv_obj_set_style_text_color(lbl_faceCap[i], lv_color_hex(COL_OP_KEY), 0);
        lv_obj_set_style_text_align(lbl_faceCap[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(lbl_faceCap[i], (i < 6) ? kFaceNames[i] : "");
        hide(lbl_faceCap[i]);
    }

    for (int i = 0; i < kRibbonSlots; ++i) {
        lbl_ribbon[i] = lv_label_create(scr);
        lv_obj_set_size(lbl_ribbon[i], RIB_SLOT, 20);
        lv_obj_set_pos(lbl_ribbon[i],
                       (320 - kRibbonSlots * RIB_SLOT) / 2 + i * RIB_SLOT, RIB_Y);
        lv_obj_set_style_text_font(lbl_ribbon[i], &lv_font_bar_12, 0);
        lv_obj_set_style_text_align(lbl_ribbon[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(lbl_ribbon[i], "");
        hide(lbl_ribbon[i]);
    }

    bar_track = lv_obj_create(scr);
    makeBare(bar_track);
    lv_obj_set_size(bar_track, PBAR_W, PBAR_H);
    lv_obj_set_pos(bar_track, PBAR_X, PBAR_Y);
    lv_obj_set_style_radius(bar_track, 2, 0);
    lv_obj_set_style_bg_color(bar_track, lv_color_hex(COL_DESC_BG), 0);
    lv_obj_set_style_bg_opa(bar_track, 190, 0);
    lv_obj_set_style_border_color(bar_track, lv_color_hex(COL_DESC_EDGE), 0);
    lv_obj_set_style_border_opa(bar_track, 150, 0);
    lv_obj_set_style_border_width(bar_track, 1, 0);
    hide(bar_track);

    // The dial. Built once like everything else here; a widget created and
    // destroyed per screen is how this pool gets fragmented.
    dial_arc = lv_arc_create(scr);
    lv_obj_remove_style(dial_arc, nullptr, LV_PART_KNOB);   // no drag handle:
    lv_obj_clear_flag(dial_arc, LV_OBJ_FLAG_CLICKABLE);     // the wheel drives it
    lv_obj_set_size(dial_arc, DIAL_D, DIAL_D);
    lv_obj_set_pos(dial_arc, DIAL_X, DIAL_Y);

    // Open at the bottom, like a rotary control rather than a pie chart. 135
    // to 45 through the top leaves the gap where the eye expects the pointer
    // to start from.
    lv_arc_set_bg_angles(dial_arc, 135, 45);
    lv_arc_set_rotation(dial_arc, 0);
    lv_obj_set_style_arc_width(dial_arc, DIAL_W, LV_PART_MAIN);
    lv_obj_set_style_arc_width(dial_arc, DIAL_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(dial_arc, lv_color_hex(0x1B1A3A), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(dial_arc, 200, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dial_arc, 0, LV_PART_KNOB);
    hide(dial_arc);

    lbl_dial = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_dial, &lv_font_head_16, 0);
    lv_obj_set_style_text_color(lbl_dial, lv_color_hex(COL_OP_HEAD), 0);
    lv_obj_set_style_text_align(lbl_dial, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_dial, DIAL_D);
    lv_obj_set_pos(lbl_dial, DIAL_X, DIAL_Y + DIAL_D / 2 - 9);
    lv_label_set_text(lbl_dial, "");
    hide(lbl_dial);

    lbl_dialCap = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_dialCap, &lv_font_bar_12, 0);
    lv_obj_set_style_text_color(lbl_dialCap, lv_color_hex(COL_OP_KEY), 0);
    lv_obj_set_style_text_align(lbl_dialCap, LV_TEXT_ALIGN_CENTER, 0);
    // BELOW the arc, not inside it. The ring's inner diameter is about 60 px
    // and any caption worth writing is wider than that, so a caption placed in
    // the middle draws straight across the stroke on both sides.
    lv_obj_set_width(lbl_dialCap, 200);
    lv_obj_set_pos(lbl_dialCap, (320 - 200) / 2, DIAL_Y + DIAL_D + 2);
    lv_label_set_text(lbl_dialCap, "");
    hide(lbl_dialCap);

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

    lv_memset(s_pnetBuf, 0, sizeof(s_pnetBuf));
    s_pnetDsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    s_pnetDsc.header.cf     = LV_COLOR_FORMAT_RGB565A8;
    s_pnetDsc.header.flags  = 0;
    s_pnetDsc.header.w      = PNET_W;
    s_pnetDsc.header.h      = PNET_H;
    s_pnetDsc.header.stride = PNET_W * 2;
    s_pnetDsc.data_size     = sizeof(s_pnetBuf);
    s_pnetDsc.data          = s_pnetBuf;

    // Created HERE, after its descriptor is filled in, not up with the menu
    // widgets. lv_image_set_src() reads the header immediately to size the
    // widget, so pointing it at a half-built descriptor gives a 0x0 image that
    // draws nothing — silently, like everything else in this stack.
    img_prevNet = lv_image_create(scr);
    lv_image_set_src(img_prevNet, &s_pnetDsc);
    lv_obj_set_pos(img_prevNet, PNET_X, PNET_Y);
    hide(img_prevNet);

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

    // The ARMED badge. Its label is its CHILD, so the breath can fade the
    // block and the word as one thing by touching two styles — and only two
    // styles. Fading the object's own `opa` instead would look identical and
    // would force LVGL to render it through a layer, which is the allocation
    // trap in 4.4 at small scale: cheap here, but a habit that is not.
    badge_armed = lv_obj_create(scr);
    makeBare(badge_armed);
    lv_obj_set_size(badge_armed, ARMED_W, ARMED_H);
    lv_obj_set_pos(badge_armed, ARMED_X, ARMED_Y);
    lv_obj_set_style_radius(badge_armed, 3, 0);
    lv_obj_set_style_bg_color(badge_armed, lv_color_hex(COL_ARMED_BG), 0);
    lv_obj_set_style_bg_opa(badge_armed, LV_OPA_COVER, 0);
    hide(badge_armed);

    lv_obj_t* lbl_armed = lv_label_create(badge_armed);
    lv_obj_set_width(lbl_armed, ARMED_W);
    lv_obj_set_pos(lbl_armed, 0, 1);
    lv_obj_set_style_text_font(lbl_armed, &lv_font_bar_12, 0);
    lv_obj_set_style_text_color(lbl_armed, lv_color_hex(COL_ARMED_INK), 0);
    lv_obj_set_style_text_align(lbl_armed, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(lbl_armed, "");

    bar_fill = lv_obj_create(bar_track);
    makeBare(bar_fill);
    lv_obj_set_size(bar_fill, 0, PBAR_H - 4);
    lv_obj_set_pos(bar_fill, 1, 1);
    lv_obj_set_style_radius(bar_fill, 1, 0);
    lv_obj_set_style_bg_color(bar_fill, lv_color_hex(COL_CURSOR), 0);
    lv_obj_set_style_bg_opa(bar_fill, LV_OPA_COVER, 0);
    hide(bar_fill);
}

void CubeDisplay::setOpRibbon(const char* const* moves, int count, int current) {
    if (!lbl_ribbon[0]) return;
    if (!moves || count <= 0) {
        for (int i = 0; i < kRibbonSlots; ++i) hide(lbl_ribbon[i]);
        return;
    }

    // Centre the window on the current move, then clamp to the ends so the
    // ribbon stops scrolling once the sequence does — otherwise the last few
    // moves would drift off one side with blank slots behind them.
    int first = current - kRibbonSlots / 2;
    if (first > count - kRibbonSlots) first = count - kRibbonSlots;
    if (first < 0) first = 0;

    for (int i = 0; i < kRibbonSlots; ++i) {
        const int m = first + i;
        if (m < 0 || m >= count) {
            lv_label_set_text(lbl_ribbon[i], "");
            show(lbl_ribbon[i]);
            continue;
        }

        // Done, doing, still to do — three states, told by color alone.
        uint32_t ink = COL_OP_VALUE;
        lv_opa_t opa = LV_OPA_COVER;
        if (m == current)     { ink = COL_CURSOR; }
        else if (m < current) { ink = COL_OP_KEY; opa = 120; }

        lv_obj_set_style_text_color(lbl_ribbon[i], lv_color_hex(ink), 0);
        lv_obj_set_style_text_opa(lbl_ribbon[i], opa, 0);
        lv_label_set_text(lbl_ribbon[i], moves[m] ? moves[m] : "");
        show(lbl_ribbon[i]);
    }
}

void CubeDisplay::setOpDial(int value, int lo, int hi,
                            const char* centre, const char* caption) {
    if (!dial_arc) return;
    if (hi <= lo) {
        hide(dial_arc);
        hide(lbl_dial);
        hide(lbl_dialCap);
        return;
    }

    if (value < lo) value = lo;
    if (value > hi) value = hi;

    // lv_arc's own range, set every call rather than once: the same dial serves
    // whatever screen borrows it, and a stale range would draw a correct number
    // at the wrong angle - which is worse than no dial, because it looks right.
    lv_arc_set_range(dial_arc, (int16_t)lo, (int16_t)hi);
    lv_arc_set_value(dial_arc, (int16_t)value);
    show(dial_arc);

    lv_label_set_text(lbl_dial, centre ? centre : "");
    if (centre && *centre) show(lbl_dial); else hide(lbl_dial);

    lv_label_set_text(lbl_dialCap, caption ? caption : "");
    if (caption && *caption) show(lbl_dialCap); else hide(lbl_dialCap);
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

// The fallback color for a screen whose caller did not name a branch.
//
// Since the frame became wayfinding (see OpKind in the header) the sketch
// almost always follows showOperation() with setOpTheme(), so this mapping is
// what a bare showOperation() lands on rather than what the panel ends up
// showing. Error is the exception that still means what it says.
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
    for (int i = 0; i < kOpLines; ++i) {
        hide(lbl_line[i]); hide(lbl_lineVal[i]); hide(row_dot[i]);
    }
    // Through setOpArmed() rather than hide(), because the badge owns a
    // repeating animation and a hidden object with a live animation still
    // costs a callback every frame, forever.
    setOpArmed(nullptr);
    for (int b = 0; b < 2; ++b) {
        hide(lbl_chipRow[b]);
        for (int i = 0; i < kChipMax; ++i) hide(chip[b][i]);
    }
    for (int i = 0; i < kChipMax; ++i) hide(lbl_faceCap[i]);
    for (int i = 0; i < kRows; ++i) hide(barBox[i]);
    hide(img_orb);
    hide(img_comma);
    hide(img_sonar[0]);
    hide(img_sonar[1]);
    for (int i = 0; i < kRibbonSlots; ++i) hide(lbl_ribbon[i]);
    hide(bar_track);
    hide(bar_fill);
    hide(dial_arc);
    hide(lbl_dial);
    hide(lbl_dialCap);
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

// Draw an unfolded cube into an RGB565A8 buffer, at whatever cell size the
// caller wants. The panel net and the pane preview differ only in that number.
namespace {
void drawNet(uint8_t* buf, int w, int h, int cell, int sticker,
             const char* facelets) {
    uint8_t* color = buf;
    uint8_t* alpha  = buf + (w * h * 2);

    // Everything transparent first: the gaps between stickers are the panel's
    // own background showing through, not a drawn color, so the net sits on
    // the themed backdrop instead of on a grey slab.
    lv_memset(alpha, 0x00, (size_t)(w * h));

    for (int f = 0; f < 6; ++f) {
        for (int k = 0; k < 9; ++k) {
            const int8_t ci = CubeDisplay::chipIndexForColor(facelets[kNetFaces[f].base + k]);

            const int cx = (kNetFaces[f].col + (k % 3)) * cell;
            const int cy = (kNetFaces[f].row + (k / 3)) * cell;

            // A sticker whose color is not known is drawn as a hollow outline
            // rather than skipped, so a gap in the net reads as "this one is
            // wrong" instead of as an empty space.
            const uint16_t px = (ci >= 0) ? rgb565(kChipColors[ci]) : rgb565(0x30364A);

            for (int y = 0; y < sticker; ++y) {
                for (int x = 0; x < sticker; ++x) {
                    const bool edge = (ci < 0) &&
                                      (x != 0 && y != 0 &&
                                       x != sticker - 1 && y != sticker - 1);
                    if (edge) continue;               // hollow centre

                    const int i = (cy + y) * w + (cx + x);
                    color[i * 2 + 0] = (uint8_t)(px & 0xFF);
                    color[i * 2 + 1] = (uint8_t)(px >> 8);
                    alpha[i] = 0xFF;
                }
            }
        }
    }
}
}  // namespace

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

    drawNet(s_netBuf, NET_W, NET_H, NET_CELL, NET_STICKER, facelets);
    // Letters take their ink from the centre sticker they land on, so a face
    // whose color was not read (drawn hollow) gets the light one.
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
//  the bottom — only the color and the contents change. The alternative, which
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
    opKind   = kind;
    clearOpExtras();

    // The frame comes back, the navigation furniture does not. The preview
    // pane and the NEXT SCREEN outline are both statements about where
    // selecting the current item would take you, and during an operation there
    // is no current item — leaving them up put the headline and the status rows
    // straight across a pane advertising a menu that is not on screen.
    show(img_bandFill);
    show(img_bandEdge);
    hide(img_pane);
    hide(img_nextFrame);
    hide(img_nextLabel);
    for (int i = 0; i < kPreviewLines; ++i) hide(lbl_prev[i]);
    for (int i = 0; i < 2; ++i) hide(ghost[i]);
    hide(img_prevNet);

    applyTheme(themeForKind(kind));

    if (title) { setTitleText(title); show(lbl_title); }
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

void CubeDisplay::setOpTheme(MenuTheme theme) {
    if (!opActive || theme == MenuTheme::Inherit) return;
    // Red outranks the branch color. The rule lives here, not in the sketch,
    // so no future caller can paint over a fault by accident.
    if (opKind == OpKind::Error) return;
    applyTheme(theme);
}

void CubeDisplay::setOpLines(const char* const* lines, int count,
                             const RowMark* marks, const RowValue* values) {
    if (!lbl_line[0]) return;
    if (count > kOpLines) count = kOpLines;
    if (count < 0)        count = 0;

    // Rows follow whatever is above them: a screen that is only a status list
    // starts high, one with a headline clears it, one with a sub-line clears
    // that too. Otherwise a short screen floats and a full one collides.
    const bool haveHead = (lbl_msg    && lv_label_get_text(lbl_msg)[0]    != '\0');
    const bool haveSub  = (lbl_status && lv_label_get_text(lbl_status)[0] != '\0');
    int top = haveHead ? OP_LINE_Y : 60;

    // The sub-line has to move with them. Left where showOperation() puts it
    // (y=82, under a headline) it lands ON the first row of a headline-less
    // screen — six rows from 77 would have run straight through it.
    if (haveSub) {
        lv_obj_set_pos(lbl_status, 48, haveHead ? OP_SUB_Y : top);
        top += OP_LINE_STEP;
    }

    for (int i = 0; i < kOpLines; ++i) {
        if (i >= count || lines[i] == nullptr) {
            hide(lbl_line[i]);
            hide(lbl_lineVal[i]);
            hide(row_dot[i]);
            continue;
        }

        const int y = top + i * OP_LINE_STEP;
        lv_obj_set_pos(lbl_line[i], OP_LINE_X, y);
        lv_obj_set_pos(lbl_lineVal[i], OP_LINE_X, y);

        // Hoisted out of the tab branch below: a row whose value is a lit
        // block takes the block's color from the same mark, so the ink has to
        // be known before we decide whether there is a value label at all.
        uint32_t ink = 0;
        if (marks) {
            switch (marks[i]) {
            case RowMark::Good: ink = COL_MARK_GOOD; break;
            case RowMark::Bad:  ink = COL_MARK_BAD;  break;
            case RowMark::Busy: ink = COL_MARK_BUSY; break;
            case RowMark::Tuned: ink = COL_MARK_TUNED; break;
            case RowMark::Plain:
            default: break;
            }
        }

        const RowValue rv = values ? values[i] : RowValue::Text;

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

            // Mark the value if there IS one, otherwise the label. A row like
            // "Load cube" with nothing in its value column would otherwise show
            // no mark at all — which is exactly what happened when it was used
            // as a cursor and those rows stayed grey while everything else lit.
            // A row whose value is a block counts as having none: the block is
            // drawn over that column and the label is all the text there is.
            const bool haveVal = (tab[1] != '\0' && rv == RowValue::Text);
            lv_obj_set_style_text_color(lbl_lineVal[i],
                                        lv_color_hex(ink ? ink : COL_OP_VALUE), 0);
            lv_obj_set_style_text_color(lbl_line[i],
                                        lv_color_hex((ink && !haveVal) ? ink : COL_OP_KEY), 0);

            show(lbl_line[i]);
            if (rv == RowValue::Text) show(lbl_lineVal[i]);
            else                      hide(lbl_lineVal[i]);
        } else {
            lv_label_set_text(lbl_line[i], lines[i]);
            // A tabless row is a full-width line with no value column to tint,
            // so it keeps its plain ink — unless a block is standing in that
            // column, in which case the row does have two halves after all.
            lv_obj_set_style_text_color(lbl_line[i],
                lv_color_hex((rv != RowValue::Text && ink) ? ink : COL_OP_VALUE), 0);
            show(lbl_line[i]);
            hide(lbl_lineVal[i]);
        }

        if (rv == RowValue::Text) {
            hide(row_dot[i]);
        } else {
            // Lit is a filled block, unlit is its own outline — the same two
            // recipes setOpChipRow() paints a captured and an empty chip with,
            // so a block in a row and a chip in a strip say the same thing.
            // Unlit is still DRAWN: a value column that went empty reads as a
            // row that broke, not as a button that is up.
            lv_obj_set_pos(row_dot[i], OP_LINE_X + OP_LINE_W - DOT_W, y + 2);

            if (rv == RowValue::On) {
                // Lit takes the row's mark color when it has one, so a row can
                // say "on" and "wrong" at once — an enable that is live when it
                // should not be is a Bad row that is also lit. With no mark it
                // is the cursor yellow, which is this theme's "live".
                const uint32_t litInk = ink ? ink : COL_MARK_BUSY;
                lv_obj_set_style_bg_color(row_dot[i], lv_color_hex(litInk), 0);
                lv_obj_set_style_bg_opa(row_dot[i], LV_OPA_COVER, 0);
                lv_obj_set_style_border_color(row_dot[i], lv_color_hex(litInk), 0);
                lv_obj_set_style_border_opa(row_dot[i], LV_OPA_COVER, 0);
            } else {
                lv_obj_set_style_bg_opa(row_dot[i], LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_color(row_dot[i], lv_color_hex(COL_OP_KEY), 0);
                lv_obj_set_style_border_opa(row_dot[i], 110, 0);
            }
            show(row_dot[i]);
        }
    }
}

void CubeDisplay::setOpChipRow(int row, const int8_t* fill, const char* const* caps,
                               int count, int active, int y) {
    if (row < 0 || row > 1) return;
    if (!chip[row][0] || !fill) return;
    if (count > kChipMax) count = kChipMax;
    if (count < 0)        count = 0;

    // Only this row is touched. A screen wanting two calls twice, and anything
    // left over from the last screen was already cleared by showOperation().
    hide(lbl_chipRow[row]);

    // Size the chips to the count so the row always clears the frame band.
    // Eight at the six-chip width overran it by a chip on each side — the last
    // one sat underneath the band's right run. Six still comes out at exactly
    // the width the scan screen has always used.
    const int maxSpan = 232;                       // inside the band, both sides
    const int gap = (count > 6) ? 5 : FACE_GAP;
    int w = (count > 0) ? (maxSpan - (count - 1) * gap) / count : FACE_W;
    if (w > FACE_W) w = FACE_W;

    const int span = count * w + (count - 1) * gap;
    const int x0   = (320 - span) / 2;

    for (int i = 0; i < kChipMax; ++i) {
        if (i >= count) { hide(chip[row][i]); if (row == 0) hide(lbl_faceCap[i]); continue; }

        lv_obj_t* c = chip[row][i];
        const int cx = x0 + i * (w + gap);
        lv_obj_set_size(c, w, FACE_H);
        lv_obj_set_pos(c, cx, y);

        const int8_t col  = fill[i];
        const bool   got  = (col >= 0 && col < 6);
        const bool   busy = (i == active);

        // A chip with a color is a readout; a hollow one is a slot waiting to
        // be filled, or — on a jog page — simply a thing you can point at.
        if (got) {
            lv_obj_set_style_bg_color(c, lv_color_hex(kChipColors[col]), 0);
            lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(c, lv_color_hex(kChipColors[col]), 0);
            lv_obj_set_style_border_opa(c, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(c, 1, 0);
        } else {
            lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_color(c, lv_color_hex(COL_OP_KEY), 0);
            lv_obj_set_style_border_opa(c, 110, 0);
            lv_obj_set_style_border_width(c, 1, 0);
        }

        if (busy) {
            lv_obj_set_style_border_color(c, lv_color_hex(COL_CURSOR), 0);
            lv_obj_set_style_border_opa(c, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(c, 2, 0);
        }

        if (row == 0) {
            lv_obj_set_size(lbl_faceCap[i], w, 12);
            lv_obj_set_pos(lbl_faceCap[i], cx, y + FACE_H + 3);
            lv_obj_set_style_text_color(lbl_faceCap[i],
                                        lv_color_hex(busy ? COL_CURSOR : COL_OP_KEY), 0);
            lv_label_set_text(lbl_faceCap[i], caps && caps[i] ? caps[i] : "");
            show(lbl_faceCap[i]);
        }
        show(c);
    }
}

void CubeDisplay::setOpFaces(const int8_t* faces, int activeA, int activeB) {
    setOpChipRow(0, faces, kFaceNames, kFaceCount, activeA, FACE_Y);

    // The scan lights BOTH faces under the sensors; the general row lights one.
    if (activeB >= 0 && activeB < kFaceCount) {
        lv_obj_t* c = chip[0][activeB];
        lv_obj_set_style_border_color(c, lv_color_hex(COL_CURSOR), 0);
        lv_obj_set_style_border_opa(c, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(c, 2, 0);
        lv_obj_set_style_text_color(lbl_faceCap[activeB], lv_color_hex(COL_CURSOR), 0);
    }
}

void CubeDisplay::setOpChips(const uint8_t* bits, int boards) {
    if (!chip[0][0] || !bits) return;
    if (boards > 2) boards = 2;

    for (int b = 0; b < 2; ++b) {
        if (b >= boards) {
            hide(lbl_chipRow[b]);
            for (int i = 0; i < kChipMax; ++i) hide(chip[b][i]);
            continue;
        }
        // Restore the caption too, not only the chips. setOpGrid() borrows
        // these same labels for its group headings — centred, full width and
        // captioned by the caller — so leaving any of that set would put this
        // row's "BOARD 1" in the middle of the screen with somebody else's
        // text in it.
        lv_obj_set_pos(lbl_chipRow[b], OP_LINE_X, CHIP_Y + b * CHIP_ROW_STEP + 3);
        lv_obj_set_width(lbl_chipRow[b], LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(lbl_chipRow[b], LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_color(lbl_chipRow[b], lv_color_hex(COL_OP_KEY), 0);
        lv_label_set_text(lbl_chipRow[b], b == 0 ? "BOARD 1" : "BOARD 2");
        show(lbl_chipRow[b]);

        // The checklist is SIX chips, one per cube color — kChipColorCount, not
        // kChipMax. This loop used to run to nine and show() every one of
        // them, so the three the wider layouts (setOpChipRow, setOpGrid) own
        // were drawn here too: placed to the RIGHT of the six real chips, and
        // outlined in whatever kChipColors[6..8] read back as, which is memory
        // past the end of the array. That is the stray colored block beside
        // the chip row.
        for (int i = kChipColorCount; i < kChipMax; ++i) hide(chip[b][i]);

        for (int i = 0; i < kChipColorCount; ++i) {
            // Restore this row's own geometry: setOpFaces() borrows these same
            // objects at a different size and position.
            lv_obj_set_size(chip[b][i], CHIP_W, CHIP_H);
            lv_obj_set_pos(chip[b][i], CHIP_X + i * (CHIP_W + CHIP_GAP),
                           CHIP_Y + b * CHIP_ROW_STEP);
            lv_obj_set_style_bg_color(chip[b][i], lv_color_hex(kChipColors[i]), 0);
            lv_obj_set_style_border_color(chip[b][i], lv_color_hex(kChipColors[i]), 0);
            lv_obj_set_style_border_width(chip[b][i], 1, 0);

            const bool got = (bits[b] >> i) & 1u;
            // Captured colors are solid; the rest are just their own outline,
            // so the row reads as a checklist rather than as decoration.
            lv_obj_set_style_bg_opa(chip[b][i], got ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_opa(chip[b][i], got ? LV_OPA_COVER : 110, 0);
            show(chip[b][i]);
        }
    }
}

// The same eighteen chips again, as two 3x3s. Third and last layout they are
// asked for; every one of the three sets size, position and caption on entry
// precisely because they share the objects.
void CubeDisplay::setOpGrid(const char* capA, const int8_t* cellsA,
                            const char* capB, const int8_t* cellsB,
                            int cursor) {
    if (!chip[0][0]) return;

    // The caption strip belongs to the chip ROW layout — there is one of it
    // and it sits under nine chips in a line, which is not this shape. A cell
    // here is identified by where it is, and the hint bar names the one under
    // the cursor, so nothing in the grid needs a label of its own.
    for (int i = 0; i < kChipMax; ++i) hide(lbl_faceCap[i]);

    const char*   caps[2]  = { capA,   capB   };
    const int8_t* cells[2] = { cellsA, cellsB };

    for (int b = 0; b < 2; ++b) {
        if (!cells[b]) {
            hide(lbl_chipRow[b]);
            for (int i = 0; i < kChipMax; ++i) hide(chip[b][i]);
            continue;
        }

        // The cursor is 0..17 across both groups, which is the numbering a
        // caller with eighteen sensors already has — not a pair of 0..8s it
        // would have to take apart here and put back together there.
        const int  sel  = cursor - b * kGridCells;
        const bool here = (sel >= 0 && sel < kGridCells);

        // The caption brightens for the group holding the cursor, so which
        // board is being pointed at can be read without finding the cell.
        lv_obj_set_pos(lbl_chipRow[b], OP_LINE_X, GRID_TOP[b]);
        lv_obj_set_width(lbl_chipRow[b], OP_LINE_W);
        lv_obj_set_style_text_align(lbl_chipRow[b], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(lbl_chipRow[b],
                                    lv_color_hex(here ? COL_OP_VALUE : COL_OP_KEY), 0);
        lv_label_set_text(lbl_chipRow[b], caps[b] ? caps[b] : "");
        show(lbl_chipRow[b]);

        for (int i = 0; i < kGridCells; ++i) {
            lv_obj_t* c = chip[b][i];
            lv_obj_set_size(c, GRID_W, GRID_H);
            lv_obj_set_pos(c, GRID_X + (i % 3) * (GRID_W + GRID_GAP),
                           GRID_TOP[b] + GRID_CAP_H + (i / 3) * (GRID_H + GRID_GAP));

            const int8_t v = cells[b][i];
            if (v >= 0 && v < kChipColorCount) {
                lv_obj_set_style_bg_color(c, lv_color_hex(kChipColors[v]), 0);
                lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
                lv_obj_set_style_border_color(c, lv_color_hex(kChipColors[v]), 0);
                lv_obj_set_style_border_opa(c, LV_OPA_COVER, 0);
            } else if (v == kCellFault) {
                // Asked, and the answer was unusable. FILLED, so it cannot be
                // read as a cell nobody has got to yet, and dark, so it cannot
                // be read as one of the six sticker colors either — the two
                // mistakes it would be worst to invite on a diagnostic.
                lv_obj_set_style_bg_color(c, lv_color_hex(COL_CELL_FAULT), 0);
                lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
                lv_obj_set_style_border_color(c, lv_color_hex(COL_MARK_BAD), 0);
                lv_obj_set_style_border_opa(c, LV_OPA_COVER, 0);
            } else {
                // Not read yet: its own outline and nothing inside, the same
                // hollow chip setOpChipRow() draws for a slot still waiting.
                lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_color(c, lv_color_hex(COL_OP_KEY), 0);
                lv_obj_set_style_border_opa(c, 110, 0);
            }
            lv_obj_set_style_border_width(c, 1, 0);

            if (here && i == sel) {
                // The cursor overrides the edge, a fault's red one included.
                // The fill underneath still says faulty, which is the reason
                // fault is a fill and not only an outline.
                lv_obj_set_style_border_color(c, lv_color_hex(COL_MARK_BUSY), 0);
                lv_obj_set_style_border_opa(c, LV_OPA_COVER, 0);
                lv_obj_set_style_border_width(c, 2, 0);
            }
            show(c);
        }
    }
}

// The badge's breath. Two style opacities rather than the object's own `opa`:
// `opa` on an object with a child makes LVGL render it through a layer, which
// it allocates whole (trap 4.4). This one is small enough that it would fit,
// but a per-frame allocation out of a pool whose exhaustion is a silent hang
// is not a habit worth starting for an effect two lines get for free.
void CubeDisplay::armedExec(void* var, int32_t v) {
    lv_obj_t* badge = (lv_obj_t*)var;
    lv_obj_set_style_bg_opa(badge, (lv_opa_t)v, 0);
    // The word fades WITH the block. Holding the ink solid while the block
    // faded would leave dark text over a dark backdrop at the bottom of the
    // breath — the badge reads as one object, so it has to fade as one.
    lv_obj_set_style_text_opa(lv_obj_get_child(badge, 0), (lv_opa_t)v, 0);
}

void CubeDisplay::setOpArmed(const char* label) {
    if (!badge_armed) return;

    if (!label || !*label) {
        // Delete by (var, exec) rather than by var alone: setMode() clears
        // every animation whose var is `this`, and the badge deliberately
        // animates on its own object so a mode change cannot leave it frozen
        // half-faded with no animation left to finish it.
        lv_anim_delete(badge_armed, armedExec);
        lv_obj_set_style_bg_opa(badge_armed, LV_OPA_COVER, 0);
        lv_obj_set_style_text_opa(lv_obj_get_child(badge_armed, 0), LV_OPA_COVER, 0);
        hide(badge_armed);
        return;
    }

    lv_label_set_text(lv_obj_get_child(badge_armed, 0), label);

    // Only a badge that was NOT already up starts a breath. The pages that arm
    // things repaint on a tick, and restarting the animation every time would
    // pin it at full brightness and look like nothing was moving at all.
    if (!lv_obj_has_flag(badge_armed, LV_OBJ_FLAG_HIDDEN)) return;
    show(badge_armed);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, badge_armed);
    lv_anim_set_exec_cb(&a, armedExec);
    lv_anim_set_values(&a, LV_OPA_COVER, ARMED_DIM);
    lv_anim_set_duration(&a, ARMED_MS);
    lv_anim_set_reverse_duration(&a, ARMED_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
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

    // The dial's indicator too, so it reads as part of the frame rather than
    // something sitting on top of it. Safe before the dial exists: applyTheme()
    // runs during buildUi().
    if (dial_arc) {
        lv_obj_set_style_arc_color(dial_arc, lv_color_hex(kPalette[idx].edge),
                                   LV_PART_INDICATOR);
    }
}

void CubeDisplay::setPreview(const MenuItem* item) {
    // A cube state wins over a list: an item that can show what it produces
    // should, and nothing yet wants both in the same pane.
    if (item && item->previewNet) {
        drawNet(s_pnetBuf, PNET_W, PNET_H, PNET_CELL, PNET_STICKER, item->previewNet);
        lv_obj_invalidate(img_prevNet);
        show(img_prevNet);
        for (int i = 0; i < kPreviewLines; ++i) hide(lbl_prev[i]);
        for (int i = 0; i < 2; ++i) hide(ghost[i]);
        return;
    }
    hide(img_prevNet);

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
        hide(img_prevNet);

        // Cancel anything mid-flight and forget the frame. Without this, the
        // next return to the menu would either resume a transition against
        // content that has been replaced, or wheel in from wherever the bars
        // were abandoned.
        lv_anim_delete(this, nullptr);
        transitioning = false;
        curRows = 0;
    }
}

// Serial output happens BEFORE the widget guard in each of these. A machine
// whose panel failed to initialise is exactly the machine whose operator needs
// the text most, and printing after an early return would make the console go
// quiet precisely when the screen did.
//
// Echo only when the text actually changes. A live screen repaints at ~20 Hz,
// and these used to print every call — a running Hardware Test emitted the
// same line twenty times a second, burying everything else in the console and
// spending real time on it. The panel is idempotent; the log should be too.
static bool changed(char* last, size_t cap, const char* msg) {
    const char* s = msg ? msg : "";
    if (strncmp(last, s, cap - 1) == 0) return false;
    strncpy(last, s, cap - 1);
    last[cap - 1] = '\0';
    return true;
}

// Progress updates from inside a running operation. Called from deep in
// CubeSystem, which knows what the machine is doing but nothing about how the
// panel is dressed: with an operation screen already up they only replace its
// headline or sub-line, leaving the frame, title, hint bar and rows exactly as
// the caller that opened the screen arranged them. Rebuilding the screen here
// would make every progress tick flash the whole panel.
void CubeDisplay::setMessage(const char* msg) {
    static char lastMsg[96] = { 1, 0 };
    if (changed(lastMsg, sizeof(lastMsg), msg)) Serial.println(msg ? msg : "");
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
    static char lastStatus[96] = { 1, 0 };
    if (changed(lastStatus, sizeof(lastStatus), msg)) Serial.println(msg ? msg : "");
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

        // Title, frame color, caption and preview all change at the START of
        // the transition, not the end — the design is explicit about this, and
        // it is what makes the new screen feel like it is already arriving
        // while the old items are still clearing.
        setTitleText(screen->title);
        applyTheme(CubeMenu::themeOf(screen, items[selectedRow]));
        swapDetail(items[selectedRow], false);

        startWheel();
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
    setTitleText(screen->title);
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

    // The frame takes the SELECTED item's color, not the screen's — that is
    // what makes moving the cursor recolor the whole frame.
    applyTheme(CubeMenu::themeOf(screen, items[selectedRow]));
    placeCursor(selectedRow, rows);

    curRows = (int8_t)rows;
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
//  64 KB pool, and even a single 167x56 bar box is ~19 KB on top of a widget
//  set already at ~35 KB. Such allocations fail, and with LV_USE_LOG at 0 they
//  fail by drawing nothing.
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

void CubeDisplay::startWheel() {
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

// Forget what is on the glass and re-send every pixel on the next update().
//
// ILI9341_T4 is a differential driver: it keeps internal_fb as its record of
// what the panel shows and transmits only the pixels that differ from it. That
// is what makes the UI fast, and it is also a trap — anything that corrupts
// the glass behind the driver's back (supply sag or SPI noise while a servo or
// stepper starts during a transfer) is never repaired, because as far as the
// driver knows those pixels are already right. The symptom is a screen that is
// part picture, part coloured snow, and heals only where something happens to
// be redrawn. The fix is not to draw more; it is to make the driver forget.
//
// setFramebuffer() is the driver's own "I know nothing about the glass": it
// waits for any transfer in flight, zeroes internal_fb and drops its mirror
// flag, after which the first flush-complete goes out as a dummy diff, i.e.
// EVERY pixel. The whole-screen invalidate has to follow IMMEDIATELY — with
// the mirror dropped, a partial flush in between would push a mostly-zero
// buffer and black out the panel — and the two together make the next
// lv_task_handler() render all bands and then upload the full frame. The
// render and the upload are the ordinary refresh path every screen switch
// already takes; the only thing added is the flag flip. Cost: one full frame,
// ~125 ms at 10 MHz.
//
// Not tft->update(internal_fb) — see begin(). Not tft->clear() — it paints the
// glass black synchronously, which flashes, and what follows is a diff against
// black rather than an unconditional upload. Not lv_refr_now() — there is no
// need, the next pump renders, and staying on the normal path keeps this on
// the code that is proven every frame.
//
// In synchronous mode (the default) there is no mirror to forget: a whole-
// screen invalidate makes the next update() render every band and write each
// one straight to the panel, so that alone is the full repaint.
void CubeDisplay::repaintAll() {
    if (!tft || !disp) return;
    Serial.println(F("Display: full repaint requested"));
#if CUBE_DISPLAY_ASYNC_DMA
    tft->setFramebuffer(internal_fb);
#endif
    lv_obj_invalidate(lv_screen_active());
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
