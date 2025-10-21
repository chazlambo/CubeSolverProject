#include <CubeSystem.h>

CubeSystem Cube;  // Global CubeSystem instance

unsigned int t = 0;
unsigned int t_buffer = 100;
unsigned int t_old = 0;

int state = 0;
bool startPrintBool = false;
bool endPrintBool = false;

int extendMotors = 1;

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }  // Wait for Serial Monitor to open
  Serial.println("\n=== Cube Calibration Utility ===");

  // Begin Cube Solver System
  Cube.begin();

  // Move all motors into the center
  if (extendMotors) {
    Cube.topServoExtend();
    Cube.botServoExtend();
    Cube.ringExtend();
  }

  // ----- CHECK EXISTING CALIBRATION -----
  if (Cube.getMotorCalibration()) {
    Serial.println("\nMotor encoders are already calibrated.\n");
    Serial.println("Calibration Table (Raw Encoder Values)");
    Serial.println("=================================================");
    Serial.println("Motor |   Cal 1   |   Cal 2   |   Cal 3   |   Cal 4");
    Serial.println("-------------------------------------------------");

    for (int i = 0; i < Cube.numMotors; i++) {
      Serial.print("  ");
      Serial.print(i);
      Serial.print("   |");

      for (int j = 0; j < 4; j++) {
        int val = MotorEncoders[i]->getCalibration(j);
        Serial.print("  ");
        Serial.print(val);
        if (val < 10)      Serial.print("      |");
        else if (val < 100) Serial.print("     |");
        else if (val < 1000) Serial.print("    |");
        else if (val < 10000) Serial.print("   |");
        else Serial.print("  |");
      }
      Serial.println();
    }
    Serial.println("=================================================\n");
  } else {
    Serial.println("\nMotor encoders are not calibrated.\n");
  }

  // Continue into main calibration loop
  state = 1;
}

void loop() {
  t = millis();

  switch (state) {
    case 1: // Waiting to begin
      if (!startPrintBool) {
        Serial.println("\nPress any key to begin motor encoder calibration...");
        startPrintBool = true;
      }

      if (Serial.available() > 0) {
        while (Serial.available()) Serial.read(); // Clear input buffer
        startPrintBool = false;
        state = 2;
        Serial.println("\nBeginning calibration. Press any key to save current values...");
      }
      break;

    case 2: // Printing live encoder values
      if (t - t_old > t_buffer) {
        for (int i = 0; i < Cube.numMotors; i++) {
          int val = MotorEncoders[i]->scan();
          Serial.print(val);
          Serial.print("\t");
        }
        Serial.println();
        t_old = t;
      }

      // Save calibration when user presses a key
      if (Serial.available() > 0) {
        while (Serial.available()) Serial.read(); // Clear buffer
        Serial.println("Saving current encoder positions as calibration values...");
        Cube.calibrateMotorRotations();
        Serial.println("Calibration complete and saved to EEPROM.");
        state = 3;
      }
      break;

    case 3: // Print final calibrated values
      if (!endPrintBool) {
        Serial.println("\nCalibration Complete.");

        if (extendMotors) {
          Cube.topServoRetract();
          Cube.botServoRetract();
          Cube.ringRetract();
        }

        Serial.println("\nFinal Calibrated Values:");
        Serial.println("Motor |   Cal 1   |   Cal 2   |   Cal 3   |   Cal 4");
        Serial.println("-------------------------------------------------");

        for (int i = 0; i < Cube.numMotors; i++) {
          Serial.print("  ");
          Serial.print(i);
          Serial.print("   |");
          for (int j = 0; j < 4; j++) {
            Serial.print("  ");
            Serial.print(MotorEncoders[i]->getCalibration(j));
            Serial.print("   |");
          }
          Serial.println();
        }

        // EEPROM Check
        Serial.println("\nEEPROM Check: Flag and Stored Values");
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
