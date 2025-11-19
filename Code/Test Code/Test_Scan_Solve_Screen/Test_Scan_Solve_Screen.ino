#include <Wire.h>
#include <ILI9341_T4.h>
#include <lvgl.h>

#include "CubeSystem.h"
#include "CubeHardwareConfig.h"    // extern menuEncoder

// ======================================================
//                    TFT / LVGL SETUP
// ======================================================
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

ILI9341_T4::DiffBuffStatic<8000> diff1;
ILI9341_T4::DiffBuffStatic<8000> diff2;

DMAMEM uint16_t internal_fb[LX * LY];

ILI9341_T4::ILI9341Driver tft(
    PIN_CS, PIN_DC,
    PIN_SCK, PIN_MOSI, PIN_MISO,
    PIN_RESET, PIN_TOUCH_CS, PIN_TOUCH_IRQ
);

#define BUF_LINES 40
static lv_color_t lv_buf[LX * BUF_LINES];
lv_display_t* disp;

// UI labels
lv_obj_t* lbl_msg;
lv_obj_t* lbl_status;

// ======================================================
//                    CUBE SYSTEM
// ======================================================
CubeSystem Cube;
int scanResult = -1;
int solveResult = -1;
int execResult  = -1;


// ======================================================
//               Utility: display text
// ======================================================
void ui_set_message(const char* msg) {
    lv_label_set_text(lbl_msg, msg);
    Serial.println(msg);
}

void ui_set_status(const char* msg) {
    lv_label_set_text(lbl_status, msg);
    Serial.println(msg);
}

void ui_clear_status() {
    lv_label_set_text(lbl_status, "");
}


// ======================================================
//      Wait For SELECT Button (with full debouncing)
// ======================================================
void waitForSelect(const char* msg)
{
    ui_set_message(msg);
    ui_set_status("Press SELECT");

    lv_task_handler();
    delay(5);

    // Require RELEASE first
    while (menuEncoder.selectPressed()) {
        lv_task_handler();
        delay(10);
    }

    // Wait for PRESS
    while (!menuEncoder.selectPressed()) {
        lv_task_handler();
        delay(10);
    }

    ui_clear_status();

    // Wait for RELEASE again
    while (menuEncoder.selectPressed()) {
        lv_task_handler();
        delay(10);
    }
}


// ======================================================
//                    LVGL flush
// ======================================================
void my_disp_flush(lv_display_t* disp,
                   const lv_area_t* area,
                   uint8_t* px_map)
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

uint32_t my_tick() { return millis(); }


// ======================================================
//                        SETUP
// ======================================================
void setup()
{
    Serial.begin(115200);
    delay(200);

    Serial.println("==== Cube Solver UI Startup ====");

    // -------- TFT ----------
    tft.output(&Serial);
    while (!tft.begin(SPI_SPEED));
    tft.setFramebuffer(internal_fb);
    tft.setDiffBuffers(&diff1, &diff2);
    tft.setRotation(1);
    tft.clear(0x0000);  // BLACK

    // -------- LVGL ----------
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

    // Black background
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);

    // White text style
    static lv_style_t white;
    lv_style_init(&white);
    lv_style_set_text_color(&white, lv_color_white());

    // Create labels
    lbl_msg = lv_label_create(lv_screen_active());
    lv_obj_align(lbl_msg, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_add_style(lbl_msg, &white, LV_STATE_DEFAULT);

    lbl_status = lv_label_create(lv_screen_active());
    lv_obj_align(lbl_status, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_add_style(lbl_status, &white, LV_STATE_DEFAULT);

    // Startup message
    ui_set_message("Initializing Cube...");
    lv_task_handler();

    // -------- Cube ----------
    Cube.begin();

    ui_set_message("Ready.");
    ui_clear_status();
}


// ======================================================
//                        LOOP
// ======================================================
void loop()
{
    lv_task_handler();

    // -------------------- STEP 1: SCAN --------------------
    waitForSelect("Insert scrambled cube, then:");

    ui_set_message("Scanning cube...");
    lv_task_handler();
    delay(5);

    Serial.println("Starting Scan...");
    scanResult = Cube.scanCube();

    if (scanResult != 0) {
        ui_set_message("SCAN ERROR!");
        char buf[40];
        sprintf(buf, "Err %d", scanResult);
        ui_set_status(buf);
        Serial.print("Scan failed - Error: ");
        Serial.println(scanResult);
        while (true) { lv_task_handler(); delay(20); }
    }

    ui_set_message("Scan complete.");
    Serial.println("Scan successful.");
    delay(250);


    // -------------------- STEP 2: SOLVE VIRTUALLY --------------------
    ui_set_message("Solving cube...");
    lv_task_handler();
    delay(5);

    Serial.println("Computing virtual solution...");
    solveResult = Cube.solveVirtual();

    if (solveResult != 0) {
        ui_set_message("SOLVE ERROR!");
        char buf[40];
        sprintf(buf, "Err %d", solveResult);
        ui_set_status(buf);
        Serial.print("Virtual solve failed: ");
        Serial.println(solveResult);
        while (1) { lv_task_handler(); delay(20); }
    }

    ui_set_message("Solution found!");
    ui_set_status("Press SELECT to load cube");
    Serial.println("Virtual solution ready.");


    // -------------------- STEP 3: LOAD CUBE --------------------
    waitForSelect("Solution found! To load cube:");

    ui_set_message("Loading cube...");
    ui_clear_status();
    Serial.println("Loading cube...");

    Cube.botServoExtend();
    delay(120);
    Cube.ringExtend();
    Cube.topServoExtend();
    delay(160);

    ui_set_message("Cube loaded.");
    ui_set_status("Cube ready to solve.\nPress SELECT to begin.");
    Serial.println("Cube loaded and ready.");


    // -------------------- STEP 4: EXECUTE SOLVE --------------------
    waitForSelect("Cube ready to solve.\n");

    ui_set_message("Solving...");
    ui_clear_status();
    lv_task_handler();
    delay(5);

    Serial.println("Executing solve...");

    // Time the physical solve
    unsigned long t0 = millis();
    execResult = Cube.executeSolve();
    unsigned long t1 = millis();

    if (execResult != 0) {
        ui_set_message("EXEC ERROR!");
        char buf[40];
        sprintf(buf, "Err %d", execResult);
        ui_set_status(buf);
        Serial.print("Execute solve failed - Error: ");
        Serial.println(execResult);
        while (true) { lv_task_handler(); delay(20); }
    }

    float elapsedSec = (t1 - t0) / 1000.0f;
    int moves = Cube.solutionLength;

    char resultBuf[80];
    sprintf(resultBuf, "Solve complete!\n%d moves in %.2f sec", moves, elapsedSec);
    ui_set_message(resultBuf);

    Serial.println("Solve complete!");
    Serial.print("Moves: ");
    Serial.println(moves);
    Serial.print("Time: ");
    Serial.print(elapsedSec, 2);
    Serial.println(" sec");

    Cube.ringRetract();
    Cube.topServoRetract();
    delay(130);
    Cube.botServoRetract();

    // Done – stay on result screen
    while (1) { lv_task_handler(); delay(30); }
}
