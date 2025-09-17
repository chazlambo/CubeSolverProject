#include <CubeSystem.h>

CubeSystem Cube;

void setup() {
    Cube.begin();

  // Initialize the encoder mux
  encoderMux.begin();

  // Initialize the front motor encoder
  int rc = motF.begin();
  if (rc != 0) {
    Serial.print("Front Motor Encoder begin() failed, rc = ");
    Serial.println(rc);
  } else {
    Serial.println("Front Motor Encoder initialized.");
  }
}

void loop() {
  // Read raw encoder value (0–4095)
  int val = motF.scan();

  if (val >= 0) {
    Serial.print("Front Motor Encoder Value: ");
    Serial.println(val);
  } else {
    Serial.print("Error reading front motor encoder, code: ");
    Serial.println(val);
  }

  delay(200); // adjust as needed
}
