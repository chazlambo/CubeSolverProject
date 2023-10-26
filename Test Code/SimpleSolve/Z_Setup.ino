void setupFun() {
  // Steppers
  stepperSetup();
  
  // Servos
  servoSetup();

  // Serial
  Serial.begin(115200);
  while(!Serial);
  Serial.println("Enter anything to begin");
  while (!Serial.available());
  while (Serial.available()) {
    Serial.read();
  }
  Serial.println("Starting...");
}
