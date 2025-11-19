#include <CubeSystem.h>

CubeSystem Cube;
unsigned int t = 0;
unsigned int t_buffer = 100;
unsigned int t_old = 0;

int state = 0;
bool startPrintBool = false;
bool endPrintBool = false;

bool printMargin = true;


void setup() {
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
        Serial.println("\nPlace a solved cube into the device and press any key to begin color calibration. (Red back, green right)");
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

        Serial.println("Color Sensor 1");
        for (int i = 0; i < 9; i++) {
          Serial.print("1 - Sensor ");
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

        // Add color separation analysis for sensor 1
        if(printMargin){
          printColorSeparationAnalysis(&colorSensor1, 1);
        }

        Serial.println("Color Sensor 2");
        for (int i = 0; i < 9; i++) {
          Serial.print("2 - Sensor ");
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
              Serial.print(colorSensor2.getColorCal(i, color, k));
              Serial.print("\t");
            }
            Serial.println();
          }
          Serial.println();
        }

        // Add color separation analysis for sensor 2
        if(printMargin){
          printColorSeparationAnalysis(&colorSensor2, 2);
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

void printColorSeparationAnalysis(ColorSensor* sensor, int sensorNum) {
  const char colorChars[7] = {'R', 'G', 'B', 'Y', 'O', 'W', 'E'};
  
  Serial.print("\n=== Color Separation Analysis: Sensor ");
  Serial.print(sensorNum);
  Serial.println(" ===\n");
  
  // Print distance matrix between all color pairs
  Serial.println("Distance Matrix (lower = more similar):");
  Serial.print("     ");
  for (int j = 0; j < 7; j++) {
    Serial.print("   ");
    Serial.print(colorChars[j]);
    Serial.print("  ");
  }
  Serial.println();
  
  float minMargin = 999.0;
  char minColor1 = 'X';
  char minColor2 = 'X';
  
  for (int i = 0; i < 9; i++) {
    for (int c1 = 0; c1 < 7; c1++) {
      Serial.print(colorChars[c1]);
      Serial.print("  ");
      
      for (int c2 = 0; c2 < 7; c2++) {
        if (c1 == c2) {
          Serial.print("  -   ");
        } else {
          int calVals1[4], calVals2[4];
          for (int k = 0; k < 4; k++) {
            calVals1[k] = sensor->getColorCal(i, colorChars[c1], k);
            calVals2[k] = sensor->getColorCal(i, colorChars[c2], k);
          }
          
          float dist = sensor->colorDistance(calVals1, calVals2);
          
          // Track minimum margin (but only for upper triangle to avoid duplicates)
          if (c2 > c1 && dist < minMargin) {
            minMargin = dist;
            minColor1 = colorChars[c1];
            minColor2 = colorChars[c2];
          }
          
          Serial.print(dist, 2);
          Serial.print(" ");
        }
      }
      Serial.println();
    }
    
    Serial.print("\nMinimum margin for sensor ");
    Serial.print(i);
    Serial.print(": ");
    Serial.print(minMargin, 4);
    Serial.print(" (between ");
    Serial.print(minColor1);
    Serial.print(" and ");
    Serial.print(minColor2);
    Serial.println(")");
    
    if (minMargin < 0.05) {
      Serial.println("*** WARNING: Very poor color separation! Recalibration recommended. ***");
    } else if (minMargin < 0.10) {
      Serial.println("** CAUTION: Marginal color separation. May cause errors. **");
    } else if (minMargin < 0.15) {
      Serial.println("* NOTE: Adequate separation but could be improved. *");
    } else {
      Serial.println("Good color separation.");
    }
    Serial.println();
    
    // Reset for next sensor
    minMargin = 999.0;
  }
}