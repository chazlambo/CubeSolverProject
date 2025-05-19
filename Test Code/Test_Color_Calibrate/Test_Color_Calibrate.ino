#include <CubeSystem.h>

CubeSystem Cube;
unsigned int t = 0;
unsigned int t_buffer = 100;
unsigned int t_old = 0;

int state = 0;
bool startPrintBool = false;
bool endPrintBool = false;

void setup() {
  Serial.begin(115200);
  delay(200);  // Wait for serial monitor to connect

  // Begin Cube System
  Cube.begin();

  // Print if color sensors are already calibrated
  if (Cube.getColorCalibration()) {
    Serial.println("Color sensors are already calibrated.");
  } else {
    Serial.println("Color sensors are NOT calibrated.");
  }

  state = 1;
}

void loop() {
  t = millis();

  switch (state) {
    case 1:  // Prompt user to begin calibration
      if (!startPrintBool) {
        Serial.println("\nPlace a solved cube into the device and press any key to begin color calibration.");
        startPrintBool = true;
      }

      if (Serial.available() > 0) {
        while (Serial.available()) Serial.read();  // Clear buffer
        startPrintBool = false;
        state = 2;
      }
      break;

    case 2:  // Perform calibration
      Serial.println("\nBeginning color sensor calibration...");
      Cube.calibrateColorSensors();
      Serial.println("Calibration complete and saved.");
      state = 3;
      break;

    case 3:  // Print calibrated values and EEPROM check
      if (!endPrintBool) {
        Serial.println("\nEEPROM Check: Calibration Flag and Stored Values");

        Serial.print("Sensor 1 Flag: ");
        Serial.println(EEPROM.read(colorSensor1EEPROMFlag));

        Serial.print("Sensor 2 Flag: ");
        Serial.println(EEPROM.read(colorSensor2EEPROMFlag));

        for (int i = 0; i < 9; i++) {
          Serial.print("Sensor ");
          Serial.print(i);
          Serial.println(":");

          for (int c = 0; c < 7; c++) {
            char color;
            switch (c) {
              case 0: color = 'R'; break;
              case 1: color = 'G'; break;
              case 2: color = 'B'; break;
              case 3: color = 'Y'; break;
              case 4: color = 'O'; break;
              case 5: color = 'W'; break;
              case 6: color = 'E'; break;
            }

            Serial.print("  ");
            Serial.print(color);
            Serial.print(": ");
            for (int k = 0; k < 4; k++) {
              Serial.print(colorSensor1.getColorCal(i, color, k));
              Serial.print("\t");
            }
            Serial.println();
          }
          Serial.println();
        }

        endPrintBool = true;

        Serial.println("Color calibration complete.");
      }
      break;

    default:
      Serial.println("Invalid state. Resetting...");
      delay(1000);
      state = 1;
      break;
  }
}
