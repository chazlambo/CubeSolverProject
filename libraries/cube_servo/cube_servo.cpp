#include "cube_servo.h"

void servoSetup() {
  // Servo Attachments
  topServo.attach(TOPSERVO);
  topServo.write(topRetPos);
  topServoPos = topRetPos;
  
  botServo.attach(BOTSERVO);
  botServo.write(botRetPos);
  botServoPos = botRetPos;
}

void servoSweep(PWMServo &servo, unsigned int &currentPos, unsigned int newPos) {
  unsigned int currentAngle = map(currentPos, 0, 270, 0, 180);  // Map 270 degree range to function that takes 180 degree input
  unsigned int newAngle = map(newPos, 0, 270, 0, 180);          // Map 270 degree range to function that takes 180 degree input

  if(currentAngle < newAngle) {                             // If servo needs to go forwards
    for(unsigned int i=currentAngle; i < newAngle; i++) {   // Sweep through all angles in between current and desired position
    servo.write(i);                                         // Write servo to new angle
    delay(sweepDelay);                                      // Delay set amount
    }
  }
  else if(currentAngle > newAngle) {                        // If servo needs to go backwards
    for(unsigned int i=currentAngle; i > newAngle; i--) {   // Sweep through all angles in between current and desired position
    servo.write(i);                                         // Write servo to new angle
    delay(sweepDelay);                                      // Delay set amount
    }
  }              
  return;
}

void topServoExtend(){
  servoSweep(topServo, topServoPos, topExtPos);
  topServoPos = topExtPos;
  topExtBool = 1;
}

void topServoRetract(){
  servoSweep(topServo, topServoPos, topRetPos);
  topServoPos = topRetPos;
  topExtBool = 0;
}

void botServoExtend(){
  servoSweep(botServo, botServoPos, botExtPos);
  botServoPos = botExtPos;
  botExtBool = 1;
}

void botServoRetract() {
  servoSweep(botServo, botServoPos, botRetPos);
  botServoPos = botRetPos;
  botExtBool = 0;
}

void topServoToggle() { // Toggles the top servo between position states
  if(topExtBool) {      // If Extended
    topServoRetract();  // Retract
  }
  else {                // If not extended
    topServoExtend();   // Extend
  }
}

void botServoToggle() { // Toggles the bottom servo between position states
  if(botExtBool) {      // If extended
    botServoRetract();  // Retract
  }
  else {                // If not extended
    botServoExtend();   // Extend
  }
}
