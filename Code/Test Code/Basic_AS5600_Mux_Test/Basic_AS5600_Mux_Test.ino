#include <Wire.h>
#include "TCA9548.h"

#define AS5600_ADDR 0x36
#define TCA_ADDR    0x70
#define TCA_CHANNEL 1 // U, R, F, D, L, B, RING (0-6)

TCA9548 mux(TCA_ADDR);

void setup() {
  Serial.begin(115200);
  Wire.begin();

  if (!mux.begin()) {
    Serial.println("Could not connect to TCA9548 multiplexer.");
    while (1);
  }

  // Open the channel for the AS5600
  mux.selectChannel(TCA_CHANNEL);
  Serial.print("Opened multiplexer channel ");
  Serial.println(TCA_CHANNEL);

  // Quick test: check if AS5600 responds
  Wire.beginTransmission(AS5600_ADDR);
  if (Wire.endTransmission() == 0) {
    Serial.println("AS5600 detected!");
  } else {
    Serial.println("AS5600 not found!");
  }
}

void loop() {
  // Select the correct channel before every read
  mux.selectChannel(TCA_CHANNEL);

  // Read raw angle from AS5600 (12-bit, registers 0x0E and 0x0F)
  Wire.beginTransmission(AS5600_ADDR);
  Wire.write(0x0E);  // Start register: RAW_ANGLE (high byte)
  Wire.endTransmission();

  Wire.requestFrom(AS5600_ADDR, 2);
  if (Wire.available() == 2) {
    uint16_t rawAngle = (Wire.read() << 8) | Wire.read();
    rawAngle = rawAngle & 0x0FFF;  // AS5600 angle is 12-bit

    // Convert to degrees
    float angleDeg = (rawAngle * 360.0) / 4096.0;

    Serial.print("Raw: ");
    Serial.print(rawAngle);
    Serial.print("\tAngle: ");
    Serial.print(angleDeg, 2);
    Serial.println(" deg");
  }

  delay(200);  // Update ~5x per second
}
