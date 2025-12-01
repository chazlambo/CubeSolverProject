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
      lv_buf(nullptr), disp(nullptr), lbl_msg(nullptr), lbl_status(nullptr)
{
    instance = this;  // Set static instance for callbacks
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

    // Setup screen styling (black background)
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);

    // Setup white text style
    lv_style_init(&white_style);
    lv_style_set_text_color(&white_style, lv_color_white());

    // Create message label
    lbl_msg = lv_label_create(lv_screen_active());
    lv_obj_align(lbl_msg, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_add_style(lbl_msg, &white_style, LV_STATE_DEFAULT);
    lv_label_set_text(lbl_msg, "Display Ready");

    // Create status label
    lbl_status = lv_label_create(lv_screen_active());
    lv_obj_align(lbl_status, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_add_style(lbl_status, &white_style, LV_STATE_DEFAULT);
    lv_label_set_text(lbl_status, "");

    // Initial update
    lv_task_handler();

    Serial.println("Display initialized successfully!");
    return true;
}

void CubeDisplay::setMessage(const char* msg) {
    if (lbl_msg) {
        lv_label_set_text(lbl_msg, msg);
        Serial.println(msg);
    }
}

void CubeDisplay::setStatus(const char* msg) {
    if (lbl_status) {
        lv_label_set_text(lbl_status, msg);
        Serial.println(msg);
    }
}

void CubeDisplay::clearStatus() {
    if (lbl_status) {
        lv_label_set_text(lbl_status, "");
    }
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