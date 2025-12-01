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

    // UI update methods
    void setMessage(const char* msg);
    void setStatus(const char* msg);
    void clearStatus();
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
    lv_obj_t* lbl_msg;
    lv_obj_t* lbl_status;
    lv_style_t white_style;

    // Static callback wrapper for LVGL flush
    static void flush_cb_wrapper(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
    static CubeDisplay* instance;  // For static callback access

    // Actual flush implementation
    void flushDisplay(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);

    // Static callback for LVGL tick
    static uint32_t tick_cb() { return millis(); }
};

#endif // CubeDisplay_h