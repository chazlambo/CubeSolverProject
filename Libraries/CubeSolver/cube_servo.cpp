#include "cube_servo.h"

void servoSetup() {
  // Servo Attachments
  topServo.attach(TOPSERVO);
  botServo.attach(BOTSERVO);

  // Read saved state from EEPROM
  topExtState = EEPROM.read(topServoEEPROMAddress);
  botExtState = EEPROM.read(botServoEEPROMAddress);

  // Pull out servo location only if needed
  switch(topExtState){  // Determine which state we are moving to
    case 0:     // Retracted
      topServoPos = topRetPos;
      break;    // Do nothing

    case 1:     // Extended - Retract to start
      topServoPos = topExtPos;
      topServoRetract();
      break;

    default:    // Unknown position
        topExtState = -1;
        topServoPos = (topExtPos - topRetPos)/2;  // Assume it's somewhere in the middle
        topServoRetract();
        topExtState = 0;
        return;
  }

  // Pull out servo location only if needed
  switch(botExtState){  // Determine which state we are moving to
    case 0:     // Retracted
      botServoPos = botRetPos;
      break;    // Do nothing

    case 1:     // Extended - Retract to start
      botServoPos = botExtPos;
      botServoRetract();
      break;

    default:    // Unknown position
        botExtState = -1;
        botServoPos = (botExtPos - botRetPos)/2; // Assume it's somewhere in the middle
        botServoRetract();
        botExtState = 0;
        return;
  }
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
  // Set to unknown state temporarily
  topExtState = -1;
  EEPROM.update(topServoEEPROMAddress, topExtState); // Update state in EEPROM

  // Sweep servo to desired position
  servoSweep(topServo, topServoPos, topExtPos);
  topServoPos = topExtPos;

  // Update state
  topExtState = 1;
  EEPROM.update(topServoEEPROMAddress, topExtState); // Update state in EEPROM
}

void topServoRetract(){
  // Set to unknown state temporarily
  topExtState = -1;
  EEPROM.update(topServoEEPROMAddress, topExtState);

  // Sweep servo to desired position
  servoSweep(topServo, topServoPos, topRetPos);
  topServoPos = topRetPos;
  topExtState = 0;

  // Update state
  topExtState = 0;
  EEPROM.update(topServoEEPROMAddress, topExtState);
}

void botServoExtend(){
  // Set to unknown state temporarily
  botExtState = -1;
  EEPROM.update(botServoEEPROMAddress, botExtState);

  // Sweep servo to desired position
  servoSweep(botServo, botServoPos, botExtPos);
  botServoPos = botExtPos;

  // Update state
  botExtState = 1;
  EEPROM.update(botServoEEPROMAddress, botExtState);
}

void botServoRetract() {
  // Set to unknown state temporarily
  botExtState = -1;
  EEPROM.update(botServoEEPROMAddress, botExtState);

  // Sweep servo to desired position
  servoSweep(botServo, botServoPos, botRetPos);
  botServoPos = botRetPos;

  // Update state
  botExtState = 0;
  EEPROM.update(botServoEEPROMAddress, botExtState);
}

void topServoToggle() { // Toggles the top servo between position states
  if(topExtState==1) {  // If Extended
    topServoRetract();  // Retract
  }
  else {                // If not extended
    topServoExtend();   // Extend
  }
}

void botServoToggle() { // Toggles the bottom servo between position states
  if(botExtState==1) {      // If extended
    botServoRetract();  // Retract
  }
  else {                // If not extended
    botServoExtend();   // Extend
  }
}
