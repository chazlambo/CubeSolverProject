#include <Arduino.h>
#include "CubeSystem.h"
#include "CubeHardwareConfig.h"   // contains extern menuEncoder

// Named Cube, capital C: the library's CubeTuneTable.cpp declares
// `extern CubeSystem Cube;`, and the Arduino 1.0 library format links every
// library .cpp into every sketch — so any sketch using CubeSystem must define
// the global under exactly this name.
CubeSystem Cube;

int32_t lastPos = 0;
String lastButton = "";

void setup() {
    Serial.begin(115200);
    delay(300);

    Serial.println("=== CubeSystem + Rotary Encoder Test ===");
    Serial.println("Initializing CubeSystem...");

    Cube.begin();   // This calls menuEncoder.begin() internally

    lastPos = menuEncoder.getPosition();
    Serial.print("Initial encoder pos: ");
    Serial.println(lastPos);
}

String readButton() {
    if (menuEncoder.upPressed())     return "UP";
    if (menuEncoder.downPressed())   return "DOWN";
    if (menuEncoder.leftPressed())   return "LEFT";
    if (menuEncoder.rightPressed())  return "RIGHT";
    if (menuEncoder.selectPressed()) return "SELECT";
    return "";
}

void loop() {
    // ----------- ENCODER -------------
    int32_t enc = menuEncoder.getPosition();
    if (enc != lastPos) {
        Serial.print("Encoder: ");
        Serial.println(enc);
        lastPos = enc;
    }

    // ----------- BUTTONS -------------
    String b = readButton();
    if (b != lastButton) {
        lastButton = b;
        if (b.length() > 0)
            Serial.println("Button: " + b);
    }

    delay(5);
}
