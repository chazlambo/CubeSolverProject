// CubeDisplay.h
#ifndef CubeDisplay_h
#define CubeDisplay_h

#include <Arduino.h>
#include <ILI9341_T4.h>
#include <lvgl.h>

#include "CubeMenu.h"                  // MenuScreen / MenuItem / MenuTheme / MenuNav
#include "utility/CubeThemeAssets.h"   // baked images and fonts

class CubeDisplay {
public:
    CubeDisplay(int sck, int miso, int mosi, int dc, int cs, int reset, 
                int touchCs = 255, int touchIrq = 255);

    // Initialize display and LVGL
    bool begin(uint32_t spiSpeed = 10000000);

    // ---- Operation screens ----------------------------------------------
    //
    // What the panel shows while the machine is doing something, or when it is
    // reporting a result. These share the menu's furniture — the same frame
    // band, the same title position, the same description box as a hint bar —
    // so an operation does not look like a different program.
    //
    // The frame color says what kind of thing is happening at a glance, which
    // is the one piece of information an operator across the room can still
    // read.
    enum class OpKind : uint8_t { Info, Scan, Solve, Calibrate, Done, Error };

    // The base screen. headline is the large line; hint fills the description
    // box. Either may be null.
    void showOperation(OpKind kind, const char* title,
                       const char* headline, const char* hint = nullptr);

    // Recolor the frame without disturbing anything on the screen. For an
    // operation that changes character partway through — a scramble becoming a
    // solve — where calling showOperation() again would clear the progress bar
    // and flicker. The frame is the state, so the state has to be able to move.
    void setOpKind(OpKind kind);

    // Recolor the frame directly, bypassing the OpKind mapping.
    //
    // OpKind exists so a frame color MEANS something — blue is scanning, red is
    // stopped — and going through it is right for every screen that is doing a
    // job. Idle Mode is the one that is not: it cycles all six colors slowly to
    // look alive, and mapping that through OpKind would both misreport what the
    // machine is doing and be unable to reach Purple, which no kind maps to.
    void setOpTheme(MenuTheme theme);

    // Body rows under the headline, for the screens that are mostly text.
    // A row containing a '\t' is split: the part before it is left-aligned and
    // the part after is right-aligned, which is what makes a status list read
    // as a table instead of as a paragraph.
    // Seven, because the machine has seven motor encoders — six faces and the
    // ring — and a screen listing them wants them all at once.
    static const int kOpLines = 7;

    // What a status row's value is saying. Plain is the default; the rest tint
    // the value half so a checklist can be read by color before it is read by
    // word — which is the whole point of a checklist you watch running.
    enum class RowMark : uint8_t { Plain, Good, Bad, Busy };

    void setOpLines(const char* const* lines, int count,
                    const RowMark* marks = nullptr);

    // The six cube faces as a row of chips, each showing the color actually
    // read from that face's centre sticker, hollow until it has been. `active`
    // are the two faces being read right now, or -1.
    //
    // faces[] is indexed U R F D L B and holds a chip color index (0..5) or
    // -1 for "not yet". This deliberately does NOT use the menu's bar art: in
    // this theme a bar means "you can select this", and borrowing it for status
    // made a scan look like a screen full of buttons.
    static const int kFaceCount = 6;
    void setOpFaces(const int8_t* faces, int activeA = -1, int activeB = -1);

    // The general form: a row of captioned chips, one of which may be lit.
    // `fill` is a chip color index per chip, or -1 to draw it hollow; `caps`
    // are the letters beneath them. setOpFaces() is this with the cube's face
    // names at the scan screen's position.
    //
    // Used as a SELECTOR as well as a readout — a row of eight boxes holds what
    // eight menu bars could not.
    // Nine, because a color board has nine sensors — one per sticker — and a
    // live readout of them is the widest row anything asks for.
    static const int kChipMax = 9;
    void setOpChipRow(int row, const int8_t* fill, const char* const* caps,
                      int count, int active, int y);

    // Per-board color capture for the sensor calibration, as two rows of six
    // chips. bits[b] holds one bit per color in kChipOrder, low bit first.
    static const int kChipCount = kChipMax;
    void setOpChips(const uint8_t* bits, int boards);

    // A color letter as the sensors and the virtual cube use them
    // ('W','Y','R','O','G','B') mapped to an index into the chip palette, or
    // -1 for anything else. Lives here because this class defines that palette
    // and its order; CubeSystem::chipIndexForColor() forwards to it.
    static int8_t chipIndexForColor(char c);

    // The cube as an unfolded net: 54 facelets in the standard order
    // (U R F D L B, row-major within each face) as color letters, exactly what
    // VirtualCube::getColorArray() returns. nullptr hides it.
    //
    // A facelet that is not a known color is drawn hollow, so a partial or
    // impossible state shows WHICH stickers are the problem.
    static const int kNetFacelets = 54;
    void setOpCubeNet(const char* facelets, bool labelFaces = true);

    // A move sequence with a cursor on the one being run: a window of moves
    // centred on `current`, past dimmed, future plain, current in cursor
    // yellow. Deliberately PLAIN TEXT with no boxes — boxed tokens would drift
    // back toward looking like buttons, and color alone carries the cursor.
    static const int kRibbonSlots = 7;
    void setOpRibbon(const char* const* moves, int count, int current);

    // A filled bar, for operations with a countable end — the solve knows how
    // many moves it has to run, and a number alone does not show how far along
    // that is at a glance.
    void setOpProgress(int done, int total);

    // A round dial for a value you are steering, with the value in the middle
    // and a caption under it.
    //
    // NOT for progress — that is the bar, and a screen showing both would be
    // claiming two different kinds of "how far along". This is for a setting
    // whose whole point is that it has a range and you are somewhere in it:
    // the arc says at a glance whether you are near the fast end or the slow
    // one, which a line of text cannot do from across the room.
    //
    // Follows the frame color, so it stays part of the screen rather than
    // sitting on it. Pass total <= 0 (lo >= hi) to hide it.
    void setOpDial(int value, int lo, int hi,
                   const char* centre, const char* caption);

    // Drop any of the decorations above.
    void clearOpExtras();

    // ---- Message screen -------------------------------------------------
    //
    // The original two-label API. Calling either of these switches the panel
    // back to message mode, so every existing caller in CubeSystem (the scan
    // progress text, the calibration prompts, the error screens) keeps working
    // unchanged and correctly takes the panel away from the menu.
    void setMessage(const char* msg);
    void setStatus(const char* msg);
    void clearStatus();

    // Message screen with an explicit title bar and footer hint. Passing
    // nullptr for title or footer hides that element.
    void showMessage(const char* title, const char* body, const char* footer = nullptr);

    // ---- Menu screen ----------------------------------------------------
    //
    // Draws the themed menu: baked background, a frame band recolored to the
    // SELECTED item's theme, zigzag bars, description line and preview pane.
    //
    // Rows must be <= kRows; anything beyond is dropped. The caller (CubeMenu)
    // owns scrolling and passes only the visible slice, so this function has no
    // notion of a cursor beyond which of the drawn rows is highlighted.
    static const int kRows = 5;

    // Longest preview list any screen offers. Five, because Modes and
    // Diagnostics both have five items and the pane shows them all.
    static const int kPreviewLines = 5;

    void showList(const MenuScreen*      screen,
                  const MenuItem* const* items,
                  int                    rows,
                  int                    selectedRow,
                  bool                   moreAbove = false,
                  bool                   moreBelow = false,
                  MenuNav                nav = MenuNav::None);

    void update();  // Call lv_task_handler()

    // Utility methods
    void waitForSelect(const char* msg);

    // Display dimensions
    int getWidth() { return LX; }
    int getHeight() { return LY; }

private:
    // Pin definitions
    int PIN_SCK, PIN_MISO, PIN_MOSI, PIN_DC, PIN_CS;
    int PIN_RESET, PIN_TOUCH_CS, PIN_TOUCH_IRQ;

    // Display dimensions
    static const int LX = 320;
    static const int LY = 240;
    static const int BUF_LINES = 40;

    // TFT driver and buffers
    ILI9341_T4::ILI9341Driver* tft;
    ILI9341_T4::DiffBuffStatic<8000>* diff1;
    ILI9341_T4::DiffBuffStatic<8000>* diff2;
    uint16_t* internal_fb;
    lv_color_t* lv_buf;
    
    // LVGL objects
    lv_display_t* disp;

    // Message-mode widgets
    lv_obj_t* lbl_msg;
    lv_obj_t* lbl_status;

    // Chrome, shared by both modes
    lv_obj_t* lbl_title;
    lv_obj_t* lbl_footer;

    // ---- Themed menu widgets --------------------------------------------
    //
    // Every one of these is a blit of a baked asset or a line of text over it.
    // Nothing here is drawn with LVGL primitives, because nothing in the design
    // is expressible as one: the glows are Gaussian, the band's stroke is
    // sub-pixel, and the preview pane is a perspective trapezoid with two
    // rounded corners out of four.
    lv_obj_t* img_bg;
    lv_obj_t* img_pane;
    lv_obj_t* lbl_prev[kPreviewLines];
    lv_obj_t* ghost[2];            // placeholder graphic when an item has no list
    lv_obj_t* img_nextFrame;
    lv_obj_t* img_nextLabel;

    // The band is two masks, not one image: the design's fill and edge are
    // different hues of the theme, and a single recolored mask can only be one
    // color. Both are white RGB565A8 that LVGL recolors at draw time.
    lv_obj_t* img_bandFill;
    lv_obj_t* img_bandEdge;

    lv_obj_t* box_desc;
    lv_obj_t* lbl_desc;

    // All five bars share one parent so a screen transition can move them as a
    // group without touching each in turn.
    lv_obj_t* menuGroup;
    lv_obj_t* barBox[kRows];
    lv_obj_t* img_bar[kRows];
    lv_obj_t* lbl_bar[kRows];

    // One cursor, moved to whichever bar is selected, rather than one per bar:
    // only ever one is visible, and five idle copies would cost LVGL memory and
    // five more objects to keep in step.
    lv_obj_t* img_orb;
    lv_obj_t* img_comma;
    lv_obj_t* img_sonar[2];

    // ---- Operation-screen widgets ---------------------------------------
    // Deliberately few: the headline and sub-line reuse lbl_msg / lbl_status,
    // the hint bar reuses box_desc, and the step rows reuse the menu's bars.
    // Only the body rows and the calibration chips are new.
    lv_obj_t* lbl_line[kOpLines];
    lv_obj_t* lbl_lineVal[kOpLines];
    lv_obj_t* chip[2][kChipCount];
    lv_obj_t* lbl_chipRow[2];
    lv_obj_t* lbl_faceCap[kChipMax];
    lv_obj_t* lbl_ribbon[kRibbonSlots];
    lv_obj_t* bar_track;
    lv_obj_t* bar_fill;
    lv_obj_t* dial_arc;
    lv_obj_t* lbl_dial;
    lv_obj_t* lbl_dialCap;
    lv_obj_t* img_net;
    lv_obj_t* lbl_netFace[6];
    lv_obj_t* img_prevNet;      // the same net, pane-sized, for menu previews

    lv_style_t white_style;

    // Last theme pushed to the band, so a cursor move that does not change
    // color does not invalidate two full-screen images for nothing.
    MenuTheme  curTheme;

    // Which family of widgets is currently on screen.
    //
    // Widgets are created ONCE in begin() and shown/hidden, never created and
    // deleted per screen. Churning LVGL objects at menu speed fragments the
    // 32 KB LV_MEM pool configured in lv_conf.h, and the failure mode of that
    // pool filling up is silent (LV_USE_LOG is 0).
    enum class Mode : uint8_t { None, Message, List };
    Mode mode;

    // True while showOperation() owns the panel, as opposed to the plain
    // setMessage() path. Kept separate from Mode because both are Message mode
    // as far as the menu widgets are concerned.
    bool opActive;

    void setMode(Mode m);
    void buildUi();

    // ---- themed menu internals -------------------------------------------
    void buildTheme(lv_obj_t* scr);
    void buildOpUi(lv_obj_t* scr);
    void applyTheme(MenuTheme t);
    static MenuTheme themeForKind(OpKind kind);
    void setPreview(const MenuItem* item);
    void placeCursor(int row, int rows);

    // Everything about a frame except the description and preview, which swap
    // on their own schedule (instantly on a screen change, faded on a move).
    void applyScreen(const MenuScreen* screen, const MenuItem* const* items,
                     int rows, int selectedRow);
    void applyDetail(const MenuItem* item);
    void swapDetail(const MenuItem* item, bool instant);

    // ---- animation --------------------------------------------------------
    //
    // All of these are LVGL animations driving image transforms and opacities.
    // None of them transform a CONTAINER, and that is a hard constraint rather
    // than a style: a transformed object is rendered through a layer, LVGL
    // allocates the whole layer up front for a transform, and even one bar box
    // (167x56 at 16bpp, ~19 KB) does not fit what is left of the 32 KB pool.
    // Transformed *images* are different — they stream through a small bounded
    // buffer — so the wheel moves each bar along its arc instead of rotating a
    // group. See the note above startWheel().
    void startCursorAnims();
    void placeBarsAt(float deg, int rows, lv_opa_t opa);
    void startWheel(int8_t dir);

    static void sonarExec(void* var, int32_t v);
    static void commaExec(void* var, int32_t v);
    static void detailExec(void* var, int32_t v);
    static void detailFadedOut(lv_anim_t* a);
    static void wheelOutExec(void* var, int32_t v);
    static void wheelOutDone(lv_anim_t* a);
    static void wheelInExec(void* var, int32_t v);
    static void wheelInDone(lv_anim_t* a);

    // The screen waiting for the outgoing bars to clear. These point into the
    // application's const tables, so holding them across an animation is safe —
    // nothing the menu can do while the wheel turns will free them.
    const MenuScreen* pendScreen;
    const MenuItem*   pendItems[kRows];
    const MenuItem*   pendDetail;
    int8_t            pendRows, pendSel, pendDir;

    // What is on the panel right now, so the outgoing half of a transition
    // knows how many bars to sweep. Zeroed whenever the menu loses the panel,
    // which is what stops an operation screen handing back into an animation.
    int8_t            curRows, curSel;
    bool              transitioning;

    // Screen position of a bar's box, for `rows` items on screen. The row
    // geometry is the design's zigzag, not a constant pitch, and it changes
    // with the item count.
    static void barBoxPos(int i, int rows, int& x, int& y);

    // Static callback wrapper for LVGL flush
    static void flush_cb_wrapper(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
    static CubeDisplay* instance;  // For static callback access

    // Actual flush implementation
    void flushDisplay(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);

    // Static callback for LVGL tick
    static uint32_t tick_cb() { return millis(); }
};

#endif // CubeDisplay_h