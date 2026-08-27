// CubeDisplay.h
#ifndef CubeDisplay_h
#define CubeDisplay_h

#include <Arduino.h>
#include <ILI9341_T4.h>
#include <lvgl.h>

#include "CubeMenu.h"                  // MenuScreen / MenuItem / MenuTheme / MenuNav
#include "utility/CubeThemeAssets.h"   // baked images and fonts

// How pixels get from LVGL to the glass.
//
// 0 (default): SYNCHRONOUS. Every LVGL flush is written straight to the panel
//   as one exact CASET/PASET window, blocking until it is on the wire. No
//   internal framebuffer, no diff buffers, no DMA, no interrupt chain. LVGL's
//   own invalidation already limits each flush to what changed, so this is the
//   differential update the other mode promises, done by the part that knows
//   what changed. A whole frame costs ~125 ms at 10 MHz; a menu step a fraction.
//
// 1: ASYNC DMA. The driver's double-buffered differential mode: a 150 KB
//   mirror of the glass, two diff buffers, uploads streamed by DMA from an
//   interrupt chain while the CPU carries on. Faster, and the mode this
//   firmware ran in until the bench showed, boot after boot, the glass holding
//   the PREVIOUS frame in large contiguous chunks — or coloured snow — while
//   the driver believed the new one had been sent. Three rounds of fixes inside
//   that mode (a boot-frame wait, a forced full re-send, ordering changes) did
//   not cure it and the driver's code gives no reason it should fail, which
//   points at the wire: this board runs the driver at 10 MHz with a custom
//   motherboard between Teensy and panel, and the DMA path toggles DC through
//   the SPI chip-select logic between every chunk. Mode 0 removes the DMA and
//   the interrupt chain — but NOT the DC-via-chip-select mechanism. DC is on
//   pin 10, a hardware CS, so the driver rides every command<->data transition
//   through the LPSPI TCR logic in this mode too; the wire that garbled mode 1
//   is still the wire mode 0 writes over. What a dropped bit costs here, and
//   what repairs it, is panelHealthTick() in the .cpp. Flip this to 1 only to
//   compare the two on the bench.
#ifndef CUBE_DISPLAY_ASYNC_DMA
#define CUBE_DISPLAY_ASYNC_DMA 0
#endif

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
    // The frame color is WAYFINDING, not machine state.
    //
    // It used to be the latter — blue meant scanning, violet meant calibrating
    // — and that fought the menu, which recolors the frame to the selected
    // item's theme. Two schemes, one band. The menu won: a branch of the tree
    // keeps its color all the way through the operation screens it leads to,
    // so a yellow frame means "you are somewhere under Settings" whether you
    // are reading About or watching a motor calibration sweep. The sketch says
    // which branch it is in with setOpTheme(); OpKind is the fallback for a
    // caller that has not, and it still names what KIND of screen this is for
    // anything that wants to decorate by kind.
    //
    // Error is the one exception, and it is enforced here rather than trusted
    // to callers: an Error screen is red whatever branch it happened in. Red
    // means "stopped" everywhere in this machine, and a blue solve that fails
    // into a blue screen would say nothing went wrong.
    enum class OpKind : uint8_t { Info, Scan, Solve, Calibrate, Done, Error };

    // The base screen. headline is the large line; hint fills the description
    // box. Either may be null.
    void showOperation(OpKind kind, const char* title,
                       const char* headline, const char* hint = nullptr);

    // Recolor the frame directly, bypassing the OpKind mapping.
    //
    // This is the normal way an operation screen gets its color: the sketch
    // knows which branch of the menu the operator came down, and that branch's
    // theme is what the frame should carry for as long as they are in it. It
    // also reaches Purple and Green-as-a-branch, which no OpKind maps to.
    //
    // Ignored on an Error screen, deliberately — see OpKind above. A caller
    // that wants a red screen recolored has to draw a different screen.
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
    //
    // Tuned is the tuning editor's: "this value is not the compiled default".
    // It is amber rather than the yellow that was first asked for because Busy
    // — the cursor — is already a yellow, and a page where the cursor row and
    // the changed rows were the same color would say nothing about either.
    // Busy still wins on the row under the cursor, as it does for every mark.
    enum class RowMark : uint8_t { Plain, Good, Bad, Busy, Tuned };

    // What a status row's value IS, when it is a state rather than a number.
    // Off and On replace the value column with an unlit or lit block; Text,
    // the default, leaves it as whatever followed the tab.
    //
    // Deliberately PARALLEL to RowMark rather than folded into it. RowMark
    // answers "what color is this row", this answers "what is drawn in its
    // value column", and the two compose — the jog page's cursor row is Busy
    // and may also be lit, and a motor-enable row can be Bad and lit at once.
    // Two more RowMark values would have made those mutually exclusive, so a
    // row that lit up would silently lose the cursor, and would have quietly
    // changed what every existing marks[] array means.
    //
    // The block is the chip vocabulary this theme already uses for status, at
    // row size, so it cannot be mistaken for bar art. Off still DRAWS its
    // outline: an empty cell reads as a broken row, not as "not pressed".
    enum class RowValue : uint8_t { Text, Off, On };

    // marks[] and values[] are both optional and both indexed by row. A row
    // whose value is Off or On does not show its text value at all — two
    // things in one column is how a row stops being scannable — so such a row
    // may be written with an empty value ("SELECT\t") or with no tab at all.
    void setOpLines(const char* const* lines, int count,
                    const RowMark* marks = nullptr,
                    const RowValue* values = nullptr);

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
    // chips. bits[b] holds one bit per color in kChipColors' order (W Y R O
    // G B), low bit first.
    void setOpChips(const uint8_t* bits, int boards);

    // Two captioned groups of nine cells, each drawn as a 3x3 — the shape of
    // a color board itself. Eighteen sensors in one strip has to be counted
    // along; two 3x3s are pointed at, which is the whole difference between
    // "sensor 14" and "that one, bottom right of board 2".
    //
    // A THIRD chip system is exactly what this is not: it reuses the same
    // chip[][] objects setOpChips() and setOpFaces() borrow — chip[0][0..8]
    // is group A and chip[1][0..8] is group B — and lbl_chipRow[b] as the
    // group caption. Nothing is created for it. Every one of the three
    // layouts sets size, position and caption on each call, because they
    // share the objects and none may inherit another's geometry.
    //
    // cells[] holds a chip color index (0..5) or one of the sentinels below;
    // pass nullptr for a group to hide it. `cursor` is 0..17 across BOTH
    // groups — the numbering a caller with eighteen sensors already has — or
    // -1 for none. The cursor's group brightens its caption, so which board
    // is being pointed at is readable without finding the cell first.
    //
    // It owns the WHOLE body — y 54 to 192, which is everything between the
    // title and the hint bar. Open the screen with a null headline and put
    // what the status rows would have said in the group captions; a headline,
    // a sub-line or setOpLines() rows on the same screen draw straight
    // through it.
    static const int kGridCells = 9;

    // Not read yet, and read but unusable. Both have to be distinguishable
    // from all six sticker colors AND from each other, because "we have not
    // asked this sensor yet" and "this sensor answered nonsense" are opposite
    // conclusions: unread is a hollow outline, faulty is a filled dark cell
    // with a red edge. Anything else negative is treated as unread.
    static const int8_t kCellUnread = -1;
    static const int8_t kCellFault  = -2;

    void setOpGrid(const char* capA, const int8_t* cellsA,
                   const char* capB, const int8_t* cellsB,
                   int cursor = -1);

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

    // "This row is armed" — you have taken control of it and the next move
    // goes to the machine. A small reverse-video badge at the top right,
    // breathing slowly so it reads as a live state rather than as furniture.
    //
    // Non-color ON PURPOSE. Arming used to turn the frame yellow; the frame
    // is wayfinding now and the whole Settings subtree — where every screen
    // that can arm anything lives — is already yellow, so that signal moves
    // nothing. A shape that appears and a motion that continues survive any
    // frame color, including the one they sit on. It is not bar art: it is a
    // chip-shaped badge, which in this theme means status.
    //
    // `label` is the word, short — about 8 characters at this face. nullptr
    // or "" clears it, as does any showOperation(), so a screen that has not
    // asked to be armed cannot inherit somebody else's badge. Calling it
    // repeatedly with the badge already up only swaps the text; the breath is
    // not restarted, so a page that repaints on a tick does not stutter.
    //
    // It sits on the TITLE's line, right-aligned to the same x=268 the status
    // rows end at, which is the one strip of an operation screen that is
    // always clear. A title long enough to reach x=212 would run under it.
    void setOpArmed(const char* label);

    // Drop any of the decorations above.
    void clearOpExtras();


    // ---- Message screen -------------------------------------------------
    //
    // The two-label API CubeSystem's progress text, calibration prompts and
    // error screens use. Either call switches the panel back to message mode,
    // so a call from deep inside an operation takes the panel away from the
    // menu; with an operation screen already up it only replaces that
    // screen's headline or sub-line.
    void setMessage(const char* msg);
    void setStatus(const char* msg);
    void clearStatus();

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

    // Re-send every pixel on the next update(). ~125 ms at 10 MHz. Call it once
    // the boot actuators have stopped and on each return to the menu; in the
    // async DMA mode it also makes the driver forget its mirror of the glass.
    void repaintAll();

    // How many times the panel watchdog has had to bring the controller back
    // from a lost init (see panelHealthTick() in the .cpp). Zero on a healthy
    // wire; a count that climbs while scrolling convicts the SPI link.
    uint16_t panelRecoveryCount() const { return panelRecoveries; }

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

    // Panel watchdog state — see panelHealthTick() in the .cpp.
    uint32_t spi_speed;
    uint32_t nextHealthMs;
    uint16_t panelRecoveries;
    void panelHealthTick();
    
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

    // All five bars and the cursor furniture share one parent so a mode change
    // can show or hide them at once. The wheel does NOT move the group — a
    // transformed container is a layer the pool cannot hold (see startWheel()),
    // so placeBarsAt() moves each bar on its own.
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
    // Only what the menu has no equivalent for: the headline and sub-line
    // reuse lbl_msg / lbl_status and the hint bar reuses box_desc.
    lv_obj_t* lbl_line[kOpLines];
    lv_obj_t* lbl_lineVal[kOpLines];
    lv_obj_t* row_dot[kOpLines];       // the boolean block, in place of a value
    lv_obj_t* chip[2][kChipMax];
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
    lv_obj_t* badge_armed;      // the ARMED badge; its only child is its label

    lv_style_t white_style;

    // Last theme pushed to the band, so a cursor move that does not change
    // color does not invalidate two full-screen images for nothing.
    MenuTheme  curTheme;

    // Which family of widgets is currently on screen.
    //
    // Widgets are created ONCE in begin() and shown/hidden, never created and
    // deleted per screen. Churning LVGL objects at menu speed fragments the
    // 64 KB LV_MEM pool configured in lv_conf.h, and the failure mode of that
    // pool filling up is silent (LV_USE_LOG is 0).
    enum class Mode : uint8_t { None, Message, List };
    Mode mode;

    // True while showOperation() owns the panel, as opposed to the plain
    // setMessage() path. Kept separate from Mode because both are Message mode
    // as far as the menu widgets are concerned.
    bool opActive;

    // What the live operation screen was drawn as. Kept only so setOpTheme()
    // can refuse to recolor an Error screen; nothing else reads it.
    OpKind opKind;

    void setMode(Mode m);
    void buildUi();

    // ---- themed menu internals -------------------------------------------
    void buildTheme(lv_obj_t* scr);
    void buildOpUi(lv_obj_t* scr);

    // The ONLY way the screen title is set. Picks the largest Anton that fits
    // the frame's notch on one line and keeps the baseline steady across the
    // two sizes — see the comment on the definition. Every path that shows a
    // title goes through here, so no caller has to know the notch exists.
    void setTitleText(const char* title);
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
    // (167x56 at 16bpp, ~19 KB) does not fit what is left of the 64 KB pool.
    // Transformed *images* are different — they stream through a small bounded
    // buffer — so the wheel moves each bar along its arc instead of rotating a
    // group. See the note above startWheel().
    void startCursorAnims();
    void placeBarsAt(float deg, int rows, lv_opa_t opa);
    void startWheel();

    static void armedExec(void* var, int32_t v);
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
    int8_t            curRows;
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