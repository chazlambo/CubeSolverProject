void setupFun() {
  // Serial Communication
  Serial.begin(115200);
  Serial.println("Starting Program");

  
  // Stepper Setup Function
  Serial.println("Initializing stepper motors...");
  stepperSetup();
  
  // Servo Setup Function
  Serial.println("Initializing servos...");
  servoSetup();
  
  Serial.println("Setup Complete\n");
}
