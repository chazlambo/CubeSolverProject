#include "C:\Users\charl\OneDrive\Documents\Projects\Cube Solver Project\Arduino Code\CubeSolverProject\libraries\CubeSolver\CubeSolver.cpp"

unsigned int t = 0;
unsigned int t_buffer = 50;
unsigned int t_old = 0;

int state = 0;
bool startPrintBool = false;
bool endPrintBool = false;

int extendMotors = 0;

void setup() {
  mainSetup();

  if (extendMotors) {
    topServo.extend();
    botServo.extend();
    ringMove(2);  // Extend ring motors
  }

  Serial.println("Starting Calibration...");
  delay(100);

  Serial.println("Checking if calibrated...");
  delay(100);

  if (getMotorCalibration()) {
    Serial.println("Motors are calibrated to values:");
    for (int i = 0; i < numMotors; i++) {
      Serial.print(MotorPots[i]->getCalibration());
      Serial.print("\t");
    }
    Serial.println();
  } else {
    Serial.println("Motors are not calibrated");
  }

  delay(100);
  state = 1;
}

void loop() {
  t = millis();

  switch (state) {
    case 1: // Waiting to begin
      if (!startPrintBool) {
        Serial.println("\nPlease enter anything to begin calibration:");
        startPrintBool = true;
      }

      if (Serial.available() > 0) {
        while (Serial.available()) Serial.read(); // Clear buffer
        startPrintBool = false;
        state = 2;
        Serial.println("\nBeginning calibration shortly. Press any key to save current values...");
      }
      break;

    case 2: // Printing calibration values
      if (t - t_old > t_buffer) {
        for (int i = 0; i < numMotors; i++) {
          Serial.print(MotorPots[i]->scan());
          Serial.print("\t");
        }
        Serial.println();
        t_old = t;
      }

      if (Serial.available() > 0) {
        while (Serial.available()) Serial.read(); // Clear buffer
        Serial.println("Saving current motor positions as calibration values...");

        bool allValid = true;
        for (int i = 0; i < numMotors; i++) {
          if (!MotorPots[i]->saveCalibration()) {
            allValid = false;
            Serial.print("Motor ");
            Serial.print(i);
            Serial.println(" calibration out of valid range.");
          }
        }

        if (allValid) {
          Serial.println("Calibration successful. Saved values:");
          for (int i = 0; i < numMotors; i++) {
            Serial.print(MotorPots[i]->getCalibration());
            Serial.print("\t");
          }
          Serial.println();
          state = 3;
        } else {
          Serial.println("Calibration failed. Try again.");
        }
      }
      break;

    case 3: // Calibration complete
      if (!endPrintBool) {
        Serial.println("\nCalibration Complete.");

        if (extendMotors) {
          topServo.retract();
          botServo.retract();
          ringMove(0);  // Retract ring motors
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
