#include <Wire.h>
#include <ILI9341_T4.h>
#include <lvgl.h>

#include "CubeSystem.h"
#include "CubeHardwareConfig.h"   // gives global: extern RotaryEncoder menuEncoder

// ------------------- DISPLAY PINS -------------------
#define PIN_SCK      13
#define PIN_MISO     12
#define PIN_MOSI     11
#define PIN_DC       10
#define PIN_CS       14
#define PIN_RESET    15
#define PIN_TOUCH_CS 255
#define PIN_TOUCH_IRQ 255

#define SPI_SPEED 10000000
#define LX 320
#define LY 240

// ------------------- DIFF + FRAMEBUFFER -------------------
ILI9341_T4::DiffBuffStatic<8000> diff1;
ILI9341_T4::DiffBuffStatic<8000> diff2;
DMAMEM uint16_t internal_fb[LX * LY];

ILI9341_T4::ILI9341Driver tft(
  PIN_CS, PIN_DC,
  PIN_SCK, PIN_MOSI, PIN_MISO,
  PIN_RESET, PIN_TOUCH_CS, PIN_TOUCH_IRQ
);

// ------------------- LVGL DRAW BUFFER -------------------
#define BUF_LINES 40
static lv_color_t lv_buf[LX * BUF_LINES];
lv_display_t* disp;

void my_disp_flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map)
{
    bool redraw_now = lv_disp_flush_is_last(disp);

    tft.updateRegion(
        redraw_now,
        (uint16_t*)px_map,
        area->x1, area->x2,
        area->y1, area->y2
    );

    lv_disp_flush_ready(disp);
}

static uint32_t my_tick() { return millis(); }

lv_obj_t* label_button;
lv_obj_t* label_counter;

// ----------------- Cube System -----------------
CubeSystem cube;

int32_t last_raw_pos = 0;
long encoder_count   = 0;
String lastButton    = "";

// -----------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    delay(300);

    Serial.println("=== LVGL + ILI9341_T4 + CubeSystem Encoder Test ===");

    // --------------------------- TFT INIT -----------------------------
    tft.output(&Serial);
    while (!tft.begin(SPI_SPEED));

    tft.setFramebuffer(internal_fb);
    tft.setDiffBuffers(&diff1, &diff2);
    tft.setRotation(1);
    tft.setDiffGap(4);
    tft.setVSyncSpacing(1);
    tft.setRefreshRate(100);
    tft.clear(0);

    // --------------------------- LVGL INIT ----------------------------
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

    // --------------------------- DARK MODE ----------------------------
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);

    static lv_style_t style_text_white;
    lv_style_init(&style_text_white);
    lv_style_set_text_color(&style_text_white, lv_color_white());

    // ------------------------ UI ELEMENTS ------------------------
    label_button = lv_label_create(lv_screen_active());
    lv_obj_align(label_button, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(label_button, "Button: NONE");
    lv_obj_add_style(label_button, &style_text_white, LV_STATE_DEFAULT);

    label_counter = lv_label_create(lv_screen_active());
    static lv_style_t style_big;
    lv_style_init(&style_big);
    lv_style_set_text_font(&style_big, &lv_font_montserrat_24);
    lv_obj_add_style(label_counter, &style_big, LV_STATE_DEFAULT);
    lv_obj_align(label_counter, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(label_counter, "0");
    lv_obj_add_style(label_counter, &style_text_white, LV_STATE_DEFAULT);

    // ------------------------ CUBE SYSTEM INIT ------------------------
    Serial.println("Initializing CubeSystem (includes menuEncoder.begin())...");
    cube.begin();

    last_raw_pos = menuEncoder.getPosition();

    Serial.println("Setup complete.");
}

// -----------------------------------------------------------
void loop()
{
    lv_task_handler();

    // ----------- BUTTONS via CubeSystem encoder -------------
    String b = "NONE";
    if (menuEncoder.upPressed())     b = "UP";
    if (menuEncoder.downPressed())   b = "DOWN";
    if (menuEncoder.leftPressed())   b = "LEFT";
    if (menuEncoder.rightPressed())  b = "RIGHT";
    if (menuEncoder.selectPressed()) b = "SELECT";

    if (b != lastButton) {
        lastButton = b;
        lv_label_set_text_fmt(label_button, "Button: %s", b.c_str());
        Serial.println("Button: " + b);
    }

    // ----------- ENCODER via CubeSystem encoder -------------
    int32_t raw = menuEncoder.getPosition();

    if (raw != last_raw_pos) {
        encoder_count += (raw > last_raw_pos ? 1 : -1);
        last_raw_pos = raw;

        lv_label_set_text_fmt(label_counter, "%ld", encoder_count);
        Serial.printf("Encoder Count: %ld\n", encoder_count);
    }

    delay(5);
}
