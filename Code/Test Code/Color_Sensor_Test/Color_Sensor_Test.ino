#include <CubeSystem.h>

// Global color sensors declared in CubeHardwareConfig.h
// extern ColorSensor colorSensor1, colorSensor2;

CubeSystem Cube;

bool readingActive = false;
int activeBoard = 0;
int activeSensor = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("\n=== Color Sensor Debug Tool ==="));
  Serial.println(F("Initializing CubeSystem..."));

  Cube.begin();
  Serial.println(F("Initialization complete.\n"));

  // Print basic mux info
  Serial.println(F("Detected Multiplexers:"));
  Serial.print(F("  Board 1: "));
  Serial.print(F("Mux1=0x"));
  Serial.print(C1_MUX1_ADDR, HEX);
  Serial.print(F(", Mux2=0x"));
  Serial.println(C1_MUX2_ADDR, HEX);

  Serial.print(F("  Board 2: "));
  Serial.print(F("Mux1=0x"));
  Serial.print(C2_MUX1_ADDR, HEX);
  Serial.print(F(", Mux2=0x"));
  Serial.println(C2_MUX2_ADDR, HEX);

  Serial.println(F("\nEnter command like '1-3' to read sensor #3 on board #1."));
  Serial.println(F("Type anything else to stop continuous reading.\n"));
}

void loop() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.length() == 0) return;

    // Stop reading if already active
    if (readingActive) {
      readingActive = false;
      Serial.println(F("\nStopped continuous reading.\n"));
      delay(200);
      return;
    }

    // Parse format "1-3"
    int dashIndex = input.indexOf('-');
    if (dashIndex == -1) {
      Serial.println(F("Invalid format. Use e.g. '1-3'."));
      return;
    }

    int board = input.substring(0, dashIndex).toInt();
    int sensor = input.substring(dashIndex + 1).toInt();

    if (board < 1 || board > 2 || sensor < 1 || sensor > 9) {
      Serial.println(F("Invalid board (1-2) or sensor (1-9)."));
      return;
    }

    sensor -= 1;

    activeBoard = board;
    activeSensor = sensor;
    readingActive = true;

    Serial.println(F("\nStarting continuous read..."));
    Serial.println(F("Board  Sensor  MuxAddr  Chan  R    G    B    W"));
  }

  if (readingActive) {
    ColorSensor* cs = (activeBoard == 1) ? &colorSensor1 : &colorSensor2;

    // Determine which mux and channel are in use for this sensor
    int muxIdx = cs->muxOrder[activeSensor] - 1;
    int chan   = cs->channelOrder[activeSensor];
    int muxAddr = (activeBoard == 1)
                    ? ((muxIdx == 0) ? C1_MUX1_ADDR : C1_MUX2_ADDR)
                    : ((muxIdx == 0) ? C2_MUX1_ADDR : C2_MUX2_ADDR);

    cs->readSensor(activeSensor);
    int* rgbw = cs->currentRGBW;

    Serial.print("  ");
    Serial.print(activeBoard);
    Serial.print("       ");
    Serial.print(activeSensor+1);
    Serial.print("       0x");
    Serial.print(muxAddr, HEX);
    Serial.print("    ");
    Serial.print(chan);
    Serial.print("     ");
    Serial.print(rgbw[0]);
    Serial.print("   ");
    Serial.print(rgbw[1]);
    Serial.print("   ");
    Serial.print(rgbw[2]);
    Serial.print("   ");
    Serial.println(rgbw[3]);

    delay(50);
  }
}
