void setupFun() {
  // Serial Communication
  Serial.begin(115200);
  Serial.println("Starting Program");

  
  // Stepper Setup Function
  stepperSetup();
  
  // Servo Setup Function
  servoSetup();
  

}
