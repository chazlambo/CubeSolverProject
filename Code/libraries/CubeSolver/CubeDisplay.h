// CubeDisplay.h
#ifndef CubeDisplay_h
#define CubeDisplay_h

#include <Arduino.h>
#include <ILI9341_T4.h>
#include <lvgl.h>

class CubeDisplay {
public:
    CubeDisplay(int sck, int miso, int mosi, int dc, int cs, int reset, 
                int touchCs = 255, int touchIrq = 255);

    // Initialize display and LVGL
    bool begin(uint32_t spiSpeed = 10000000);

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

    // ---- List screen ----------------------------------------------------
    //
    // Rows must be <= kRows; anything beyond is dropped. The caller (CubeMenu)
    // owns scrolling and passes only the visible slice, so this function has no
    // notion of a cursor beyond which of the drawn rows is highlighted.
    static const int kRows = 5;
    void showList(const char*        title,
                  const char* const* labels,
                  const bool*        chevron,      // nullptr = no chevrons
                  int                rows,
                  int                selectedRow,
                  bool               moreAbove = false,
                  bool               moreBelow = false);

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

    // List-mode widgets. rowLbl carries the text and its own background (the
    // selection bar IS the label's background, so there is no separate
    // highlight object to keep in sync); rowMark is the right-aligned submenu
    // chevron.
    lv_obj_t* rowLbl[kRows];
    lv_obj_t* rowMark[kRows];
    lv_obj_t* lbl_scrollUp;
    lv_obj_t* lbl_scrollDn;

    lv_style_t white_style;

    // Which family of widgets is currently on screen.
    //
    // Widgets are created ONCE in begin() and shown/hidden, never created and
    // deleted per screen. Churning LVGL objects at menu speed fragments the
    // 32 KB LV_MEM pool configured in lv_conf.h, and the failure mode of that
    // pool filling up is silent (LV_USE_LOG is 0).
    enum class Mode : uint8_t { None, Message, List };
    Mode mode;

    void setMode(Mode m);
    void buildUi();

    // Static callback wrapper for LVGL flush
    static void flush_cb_wrapper(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
    static CubeDisplay* instance;  // For static callback access

    // Actual flush implementation
    void flushDisplay(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);

    // Static callback for LVGL tick
    static uint32_t tick_cb() { return millis(); }
};

#endif // CubeDisplay_h