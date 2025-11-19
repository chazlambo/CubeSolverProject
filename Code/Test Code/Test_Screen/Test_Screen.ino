/********************************************************************
 * ILI9341_T4 + LVGL update-test demo
 * Counter with large, centered text
 ********************************************************************/

#include <ILI9341_T4.h>
#include <lvgl.h>

// ------------------- PIN DEFINITIONS -------------------
#define PIN_SCK      13
#define PIN_MISO     12
#define PIN_MOSI     11
#define PIN_DC       10
#define PIN_CS       14
#define PIN_RESET    15
#define PIN_TOUCH_CS 255
#define PIN_TOUCH_IRQ 255

// ------------------- DISPLAY CONFIG -------------------
#define SPI_SPEED 40000000
#define LX 320
#define LY 240

// ------------------- DIFF + FRAMEBUFFER -------------------
ILI9341_T4::DiffBuffStatic<8000> diff1;
ILI9341_T4::DiffBuffStatic<8000> diff2;

DMAMEM uint16_t internal_fb[LX * LY];

ILI9341_T4::ILI9341Driver tft(
  PIN_CS, PIN_DC,
  PIN_SCK, PIN_MOSI, PIN_MISO,
  PIN_RESET,
  PIN_TOUCH_CS, PIN_TOUCH_IRQ
);

// LVGL draw buffer
#define BUF_LINES 40
static lv_color_t lv_buf[LX * BUF_LINES];
lv_display_t* disp;

// ------------------- LVGL FLUSH CALLBACK -------------------
void my_disp_flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map)
{
    bool redraw_now = lv_disp_flush_is_last(disp);

    tft.updateRegion(
        redraw_now,
        (uint16_t*)px_map,
        area->x1,
        area->x2,
        area->y1,
        area->y2
    );

    lv_disp_flush_ready(disp);
}

// ------------------- LVGL TICK -------------------
static uint32_t my_tick(void)
{
    return millis();
}

// ------------------- COUNTER LABEL -------------------
lv_obj_t* counter_label;
uint32_t counter_value = 0;


// -----------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    delay(300);
    Serial.println("=== LVGL + ILI9341_T4 Counter Test ===");

    // ----------------------------- TFT INIT -------------------------
    tft.output(&Serial);
    while (!tft.begin(SPI_SPEED));

    tft.setFramebuffer(internal_fb);
    tft.setDiffBuffers(&diff1, &diff2);
    tft.setRotation(1);
    tft.setDiffGap(4);
    tft.setVSyncSpacing(1);
    tft.setRefreshRate(100);
    tft.clear(0);

    // ----------------------------- LVGL INIT ------------------------
    lv_init();
    lv_tick_set_cb(my_tick);

    disp = lv_display_create(LX, LY);
    lv_display_set_flush_cb(disp, my_disp_flush);

    lv_display_set_buffers(
        disp,
        lv_buf,
        NULL,
        LX * BUF_LINES * sizeof(lv_color_t),
        LV_DISPLAY_RENDER_MODE_PARTIAL
    );

    // ----------------------------- UI SETUP ------------------------
    counter_label = lv_label_create(lv_screen_active());

    // ---- Make text larger ----
    static lv_style_t style_big;
    lv_style_init(&style_big);
    lv_style_set_text_font(&style_big, &lv_font_montserrat_24);
    lv_obj_add_style(counter_label, &style_big, LV_STATE_DEFAULT);

    // ---- Center label on screen ----
    lv_obj_align(counter_label, LV_ALIGN_CENTER, 0, 0);

    lv_label_set_text(counter_label, "Starting...");

    Serial.println("Setup complete.");
}

// -----------------------------------------------------------
void loop()
{
    lv_task_handler();

    static uint32_t last = 0;

    if (millis() - last > 50) {
        last = millis();

        unsigned long ms = millis();
        unsigned long seconds = ms / 1000;
        unsigned long tenths = (ms / 100) % 10;
        unsigned long hundredths = (ms / 10) %10;

        lv_label_set_text_fmt(counter_label,
                              "Time: %lu.%lu%lu sec",
                              seconds,
                              tenths,
                              hundredths);
    }
}
