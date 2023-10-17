// Definitions and Inclusions
#include <AccelStepper.h>
#include <Servo.h>


void setup() {
  // Call Setup Function
  setupFun();

}

void loop() {
  // Serial Setup
  Serial.println("Working :)");
  delay(5000);
  ringExtend();
  delay(5000);
  ringRetract();
  
}
