// CubeDisplay.cpp
#include "CubeDisplay.h"
#include "CubeHardwareConfig.h"  // For menuEncoder

// Static instance pointer for callbacks
CubeDisplay* CubeDisplay::instance = nullptr;

CubeDisplay::CubeDisplay(int sck, int miso, int mosi, int dc, int cs, int reset,
                         int touchCs, int touchIrq)
    : PIN_SCK(sck), PIN_MISO(miso), PIN_MOSI(mosi), PIN_DC(dc), PIN_CS(cs),
      PIN_RESET(reset), PIN_TOUCH_CS(touchCs), PIN_TOUCH_IRQ(touchIrq),
      tft(nullptr), diff1(nullptr), diff2(nullptr), internal_fb(nullptr),
      lv_buf(nullptr), disp(nullptr), lbl_msg(nullptr), lbl_status(nullptr),
      lbl_title(nullptr), lbl_footer(nullptr),
      lbl_scrollUp(nullptr), lbl_scrollDn(nullptr), mode(Mode::None)
{
    for (int i = 0; i < kRows; ++i) { rowLbl[i] = nullptr; rowMark[i] = nullptr; }
    instance = this;  // Set static instance for callbacks
}

// ---------------------------------------------------------------------------
//  Layout, in pixels on the 320x240 panel (rotation 1, landscape).
//  Five rows is the design limit for this machine's menus; the geometry below
//  is what makes them fit above the footer.
// ---------------------------------------------------------------------------
namespace {
    const int TITLE_H   = 30;
    const int ROW_TOP   = 34;
    const int ROW_H     = 33;
    const int ROW_STEP  = 35;   // 5 rows -> last row ends at 207
    const int ROW_X     = 6;
    const int ROW_W     = 308;
    const int FOOTER_Y  = 214;  // 7 px clear of the last row

    const uint32_t COL_TITLE_BG = 0x14344F;   // dark slate
    const uint32_t COL_SEL_BG   = 0xE8E8E8;   // selection bar
    const uint32_t COL_FOOTER   = 0x9A9A9A;

    inline void hide(lv_obj_t* o) { if (o) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); }
    inline void show(lv_obj_t* o) { if (o) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN); }
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

    // ---- title bar ----
    lbl_title = lv_label_create(scr);
    lv_obj_set_size(lbl_title, LX, TITLE_H);
    lv_obj_set_pos(lbl_title, 0, 0);
    lv_obj_set_style_bg_color(lbl_title, lv_color_hex(COL_TITLE_BG), 0);
    lv_obj_set_style_bg_opa(lbl_title, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_left(lbl_title, 10, 0);
    lv_obj_set_style_pad_top(lbl_title, 6, 0);
    lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_DOT);
    lv_label_set_text(lbl_title, "");
    hide(lbl_title);

    // ---- list rows ----
    for (int i = 0; i < kRows; ++i) {
        rowLbl[i] = lv_label_create(scr);
        lv_obj_set_size(rowLbl[i], ROW_W, ROW_H);
        lv_obj_set_pos(rowLbl[i], ROW_X, ROW_TOP + i * ROW_STEP);
        lv_obj_set_style_radius(rowLbl[i], 4, 0);
        lv_obj_set_style_bg_color(rowLbl[i], lv_color_hex(COL_SEL_BG), 0);
        lv_obj_set_style_bg_opa(rowLbl[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(rowLbl[i], lv_color_white(), 0);
        lv_obj_set_style_text_font(rowLbl[i], &lv_font_montserrat_16, 0);
        lv_obj_set_style_pad_left(rowLbl[i], 12, 0);
        lv_obj_set_style_pad_top(rowLbl[i], 8, 0);
        // DOT rather than SCROLL: a marquee on the selected row draws every
        // frame forever, which on this panel means a continuous SPI diff-flush
        // while the machine sits idle in a menu.
        lv_label_set_long_mode(rowLbl[i], LV_LABEL_LONG_DOT);
        lv_label_set_text(rowLbl[i], "");
        hide(rowLbl[i]);

        rowMark[i] = lv_label_create(scr);
        lv_obj_set_pos(rowMark[i], ROW_X + ROW_W - 22, ROW_TOP + i * ROW_STEP + 8);
        lv_obj_set_style_text_color(rowMark[i], lv_color_white(), 0);
        lv_obj_set_style_text_font(rowMark[i], &lv_font_montserrat_16, 0);
        lv_label_set_text(rowMark[i], ">");
        hide(rowMark[i]);
    }

    // ---- scroll hints ----
    // Plain ASCII, not LV_SYMBOL_*: the symbol glyphs depend on how the
    // Montserrat fonts were built, and a missing glyph renders as a box with no
    // warning (LV_USE_LOG is 0). These are never visible on a menu that obeys
    // the five-item limit anyway.
    lbl_scrollUp = lv_label_create(scr);
    lv_obj_set_pos(lbl_scrollUp, LX - 22, 7);
    lv_obj_set_style_text_color(lbl_scrollUp, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_scrollUp, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl_scrollUp, "^");
    hide(lbl_scrollUp);

    lbl_scrollDn = lv_label_create(scr);
    lv_obj_set_pos(lbl_scrollDn, LX - 22, FOOTER_Y);
    lv_obj_set_style_text_color(lbl_scrollDn, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_scrollDn, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl_scrollDn, "v");
    hide(lbl_scrollDn);

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
    lbl_footer = lv_label_create(scr);
    lv_obj_set_width(lbl_footer, LX - 40);
    lv_obj_set_pos(lbl_footer, ROW_X + 6, FOOTER_Y);
    lv_obj_set_style_text_color(lbl_footer, lv_color_hex(COL_FOOTER), 0);
    lv_obj_set_style_text_font(lbl_footer, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(lbl_footer, LV_LABEL_LONG_DOT);
    lv_label_set_text(lbl_footer, "");
    hide(lbl_footer);

    mode = Mode::Message;
}

void CubeDisplay::setMode(Mode m) {
    if (mode == m || lbl_msg == nullptr) return;
    mode = m;

    if (m == Mode::List) {
        hide(lbl_msg);
        hide(lbl_status);
        // Rows are shown selectively by showList(); leave them hidden here so a
        // shorter list does not inherit stale rows from a longer one.
    } else {
        show(lbl_msg);
        show(lbl_status);
        for (int i = 0; i < kRows; ++i) { hide(rowLbl[i]); hide(rowMark[i]); }
        hide(lbl_scrollUp);
        hide(lbl_scrollDn);
    }
}

// Serial output happens BEFORE the widget guard in each of these.
//
// A machine whose panel failed to initialise is exactly the machine whose
// operator needs the text most, and it is the only remaining way to see what it
// is doing. Printing after an early return would make the console go quiet
// precisely when the screen did.
void CubeDisplay::setMessage(const char* msg) {
    Serial.println(msg ? msg : "");
    if (!lbl_msg) return;
    setMode(Mode::Message);
    // A bare setMessage() is an operation talking, not a screen: drop the menu
    // chrome so scan/solve/error text looks the way it always has.
    hide(lbl_title);
    hide(lbl_footer);
    lv_obj_set_style_text_align(lbl_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_msg, 12, 60);
    lv_label_set_text(lbl_msg, msg ? msg : "");
}

void CubeDisplay::setStatus(const char* msg) {
    Serial.println(msg ? msg : "");
    if (!lbl_status) return;
    setMode(Mode::Message);
    lv_label_set_text(lbl_status, msg ? msg : "");
}

void CubeDisplay::clearStatus() {
    if (lbl_status) {
        lv_label_set_text(lbl_status, "");
    }
}

void CubeDisplay::showMessage(const char* title, const char* body, const char* footer) {
    if (title) Serial.println(title);
    if (body)  Serial.println(body);
    if (!lbl_msg) return;

    setMode(Mode::Message);

    if (title) { lv_label_set_text(lbl_title, title); show(lbl_title); }
    else       { hide(lbl_title); }

    // Left-aligned and higher up the panel than setMessage()'s single centred
    // line. These bodies are multi-line status text — About, Calibration
    // Status, the startup fault list — and centring ragged lines of differing
    // length makes them noticeably harder to read.
    lv_obj_set_style_text_align(lbl_msg, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_pos(lbl_msg, 12, title ? 44 : 56);
    lv_label_set_text(lbl_msg, body ? body : "");
    lv_label_set_text(lbl_status, "");

    if (footer) { lv_label_set_text(lbl_footer, footer); show(lbl_footer); }
    else        { hide(lbl_footer); }
}

void CubeDisplay::showList(const char*        title,
                           const char* const* labels,
                           const bool*        chevron,
                           int                rows,
                           int                selectedRow,
                           bool               moreAbove,
                           bool               moreBelow) {
    if (!lbl_title || !labels) return;
    setMode(Mode::List);

    if (rows > kRows) rows = kRows;
    if (rows < 0)     rows = 0;

    lv_label_set_text(lbl_title, title ? title : "");
    show(lbl_title);

    for (int i = 0; i < kRows; ++i) {
        if (i >= rows) {
            hide(rowLbl[i]);
            hide(rowMark[i]);
            continue;
        }

        const bool sel = (i == selectedRow);

        lv_label_set_text(rowLbl[i], labels[i] ? labels[i] : "");
        lv_obj_set_style_bg_opa(rowLbl[i], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(rowLbl[i],
                                    sel ? lv_color_black() : lv_color_white(), 0);
        show(rowLbl[i]);

        if (chevron && chevron[i]) {
            // Match the row's text colour, or the chevron vanishes into the
            // selection bar on the one row the user is looking at.
            lv_obj_set_style_text_color(rowMark[i],
                                        sel ? lv_color_black() : lv_color_white(), 0);
            show(rowMark[i]);
        } else {
            hide(rowMark[i]);
        }
    }

    lv_label_set_text(lbl_footer, "wheel: move   SEL: enter   LEFT: back");
    show(lbl_footer);

    if (moreAbove) show(lbl_scrollUp); else hide(lbl_scrollUp);
    if (moreBelow) show(lbl_scrollDn); else hide(lbl_scrollDn);
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