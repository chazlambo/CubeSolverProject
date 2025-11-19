#include <Wire.h>
#include "Adafruit_seesaw.h"

#define SEESAW_ADDR 0x49

// Button pin definitions for ANO wheel
#define SS_SWITCH_SELECT 1
#define SS_SWITCH_UP     2
#define SS_SWITCH_LEFT   3
#define SS_SWITCH_DOWN   4
#define SS_SWITCH_RIGHT  5

Adafruit_seesaw ss(&Wire1);
int32_t last_enc = 0;
String lastButton = "";

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println("=== Seesaw Test ===");
  Serial.println("Using I2C1 on Teensy 4.1 pins 17=SDA1, 16=SCL1");

  // ---- Initialize Wire1 ----
  Wire1.begin();
  Wire1.setClock(100000); 
  delay(10);

  // ---- Initialize Seesaw on Wire1 ----
  if (!ss.begin(SEESAW_ADDR)) {
    Serial.println("ERR: Seesaw NOT FOUND on Wire1!");
    while (1);
  }

  Serial.println("Seesaw detected on Wire1!");

  // Configure buttons
  ss.pinMode(SS_SWITCH_UP,     INPUT_PULLUP);
  ss.pinMode(SS_SWITCH_DOWN,   INPUT_PULLUP);
  ss.pinMode(SS_SWITCH_LEFT,   INPUT_PULLUP);
  ss.pinMode(SS_SWITCH_RIGHT,  INPUT_PULLUP);
  ss.pinMode(SS_SWITCH_SELECT, INPUT_PULLUP);

  // Initial encoder read
  last_enc = ss.getEncoderPosition();
  Serial.print("Initial encoder pos: ");
  Serial.println(last_enc);
}

void loop() {

  // ----------- ENCODER -------------
  int32_t enc = ss.getEncoderPosition();

  if (enc != last_enc) {
    Serial.print("Encoder: ");
    Serial.println(enc);
    last_enc = enc;
  }

  // ----------- BUTTONS -------------
  String b = "";

  if (!ss.digitalRead(SS_SWITCH_UP))     b = "UP";
  if (!ss.digitalRead(SS_SWITCH_DOWN))   b = "DOWN";
  if (!ss.digitalRead(SS_SWITCH_LEFT))   b = "LEFT";
  if (!ss.digitalRead(SS_SWITCH_RIGHT))  b = "RIGHT";
  if (!ss.digitalRead(SS_SWITCH_SELECT)) b = "SELECT";

  if (b != lastButton) {
    lastButton = b;
    if (b.length() > 0)
      Serial.println("Button: " + b);
  }

  delay(5);  // keep I2C stable
}
