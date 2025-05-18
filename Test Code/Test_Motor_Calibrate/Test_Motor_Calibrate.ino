#include <CubeSystem.h>

CubeSystem Cube;  // Global CubeSystem instance

unsigned int t = 0;
unsigned int t_buffer = 100;
unsigned int t_old = 0;

int state = 0;
bool startPrintBool = false;
bool endPrintBool = false;

int extendMotors = 0;

void setup() {
  // Begin Cube Solver System
  Cube.begin();

  // Move all motors into the center
  if (extendMotors) {
    Cube.topServoExtend();
    Cube.botServoExtend();
    Cube.ringExtend();
  }

  // Print if motors are calibrated
  if (Cube.getMotorCalibration()) {
    Serial.println("Motors are calibrated to values:");
    for (int i = 0; i < Cube.numMotors; i++) {
      Serial.print(MotorPots[i]->getCalibration());
      Serial.print("\t");
    }
    Serial.println();
  } else {
    Serial.println("Motors are not calibrated");
  }

  state = 1;
}

void loop() {
  t = millis();

  switch (state) {
    case 1: // Waiting to begin
      // Only print once
      if (!startPrintBool) {
        Serial.println("\nPlease enter anything to begin calibration:");
        startPrintBool = true;
      }

      // Wait for Serial to to start calibration
      if (Serial.available() > 0) {
        while (Serial.available()) Serial.read(); // Clear buffer
        startPrintBool = false;
        state = 2;
        Serial.println("\nBeginning calibration shortly. Press any key to save current values...");
      }
      break;

    case 2: // Printing calibration values
      // Print current motor values 
      if (t - t_old > t_buffer) {
        for (int i = 0; i < Cube.numMotors; i++) {
          Serial.print(MotorPots[i]->scan());
          Serial.print("\t");
        }
        Serial.println();
        t_old = t;
      }

      // Once message gets sent, save current position values
      if (Serial.available() > 0) {
        while (Serial.available()) Serial.read(); // Clear buffer
        Serial.println("Saving current motor positions as calibration values...");
        Cube.calibrateMotorRotations();
        Serial.println("Calibration complete and saved.");
        state = 3;
      }
      break;

    case 3: // Calibration complete
      // Only print once
      if (!endPrintBool) {
        Serial.println("\nCalibration Complete.");

        // Retract motors back to starting state
        if (extendMotors) {
          Cube.topServoRetract();
          Cube.botServoRetract();
          Cube.ringRetract();
        }

        // Print the calibrated values for each motors
        Serial.println("Calibrated values for each motor:");
        for (int i = 0; i < Cube.numMotors; i++) {
          Serial.print("Motor ");
          Serial.print(i);
          Serial.print(": ");
          for (int j = 0; j < 4; j++) {
            Serial.print(MotorPots[i]->getCalibration(j));
            Serial.print("\t");
          }
          Serial.println();
        }

        // Print out the calibrated values from EEPROM to check if correct
        Serial.println("\nEEPROM Check: Calibrated Values and Flag");
        Serial.print("Calibration Flag: ");
        Serial.println(EEPROM.read(motorCalFlagAddress));

        for (int i = 0; i < Cube.numMotors; i++) {
          Serial.print("Motor ");
          Serial.print(i);
          Serial.print(": ");

          for (int j = 0; j < 4; j++) {
            int val;
            EEPROM.get(motorCalAddresses[i][j], val);
            Serial.print(val);
            Serial.print("\t");
          }
          Serial.println();
        }

        endPrintBool = true;
      }
      break;

    default:
      Serial.println("Invalid state. Resetting...");
      delay(1000);
      state = 1;
      break;
  }
}
