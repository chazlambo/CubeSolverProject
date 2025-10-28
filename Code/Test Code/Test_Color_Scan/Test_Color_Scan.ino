#include <CubeSystem.h>

CubeSystem Cube;

int selectedSensor = 0;  // 0 = none, 1 = sensor1, 2 = sensor2
bool waitingForInput = true;

void setup() {
  // Begin Cube System
  Cube.begin();
  
  Serial.println("========================================");
  Serial.println("    COLOR SENSOR DIAGNOSTIC TEST");
  Serial.println("========================================\n");

  // Check if color sensors are calibrated
  if (Cube.getColorCalibration()) {
    Serial.println("✓ Color sensors are calibrated.\n");
  } else {
    Serial.println("✗ WARNING: Color sensors are NOT calibrated!");
    Serial.println("  Please run calibration first.\n");
  }
}

void loop() {
  if (waitingForInput) {
    printMenu();
    waitingForInput = false;
  }

  if (Serial.available() > 0) {
    char input = Serial.read();
    while (Serial.available()) Serial.read();  // Clear buffer

    if (input == '1' || input == '2') {
      selectedSensor = input - '0';
      scanAndDisplayResults();
      waitingForInput = true;
    } else {
      Serial.println("\nInvalid input. Please enter 1 or 2.\n");
      waitingForInput = true;
    }
  }
}

void printMenu() {
  Serial.println("========================================");
  Serial.println("Select which sensor board to test:");
  Serial.println("  [1] Color Sensor 1");
  Serial.println("  [2] Color Sensor 2");
  Serial.println("========================================");
  Serial.print("Enter choice: ");
}

void scanAndDisplayResults() {
  ColorSensor* sensor;
  
  if (selectedSensor == 1) {
    sensor = &colorSensor1;
    Serial.println("\n\n*** SCANNING COLOR SENSOR 1 ***\n");
  } else {
    sensor = &colorSensor2;
    Serial.println("\n\n*** SCANNING COLOR SENSOR 2 ***\n");
  }

  // Scan the face
  Serial.println("Scanning face...");
  sensor->scanFace();
  Serial.println("Scan complete!\n");

  // Display results for each of the 9 sensors
  Serial.println("========================================");
  Serial.println("         SCAN RESULTS");
  Serial.println("========================================\n");

  for (int i = 0; i < 9; i++) {
    displaySensorResults(sensor, i);
  }

  Serial.println("\n========================================");
  Serial.println("Scan complete. Ready for next test.\n");
}

void displaySensorResults(ColorSensor* sensor, int sensorIdx) {
  // Get the scan values for this sensor
  const int* scanVals = sensor->getScanValRow(sensorIdx);
  
  // Get the predicted color
  char predictedColor = sensor->getColor(sensorIdx, scanVals);
  
  // Calculate minimum distance to calibrated colors
  float minDist = 99999.0;
  float secondMinDist = 99999.0;
  char closestColor = 'U';
  char secondClosestColor = 'U';
  int closestCalVals[4] = {0, 0, 0, 0};
  int secondClosestCalVals[4] = {0, 0, 0, 0};  // Added line
  const char colorChars[7] = {'R', 'G', 'B', 'Y', 'O', 'W', 'E'};
  
  for (int c = 0; c < 7; c++) {
    int calVals[4];
    for (int k = 0; k < 4; k++) {
      calVals[k] = sensor->getColorCal(sensorIdx, colorChars[c], k);
    }
    
    float dist = sensor->colorDistance(scanVals, calVals);
    
    if (dist < minDist) {
      // New closest - push old closest to second place
      secondMinDist = minDist;
      secondClosestColor = closestColor;
      for (int k = 0; k < 4; k++) {
        secondClosestCalVals[k] = closestCalVals[k];
      }
      minDist = dist;
      closestColor = colorChars[c];
      // Save the calibration values for closest color
      for (int k = 0; k < 4; k++) {
        closestCalVals[k] = calVals[k];
      }
    } else if (dist < secondMinDist) {
      // New second closest
      secondMinDist = dist;
      secondClosestColor = colorChars[c];
      for (int k = 0; k < 4; k++) {
        secondClosestCalVals[k] = calVals[k];
      }
    }
  }

  // Print sensor position label
  const char* positions[9] = {"UL", "UM", "UR", "ML", "MM", "MR", "DL", "DM", "DR"};
  
  Serial.print("Sensor ");
  Serial.print(sensorIdx);
  Serial.print(" (");
  Serial.print(positions[sensorIdx]);
  Serial.println("):");
  
  // Print predicted color
  Serial.print("  Predicted Color: ");
  Serial.print(predictedColor);
  if (predictedColor == 'U') {
    Serial.println(" (UNKNOWN - Distance exceeded tolerance)");
  } else {
    Serial.println();
  }
  
  // Print minimum distance
  Serial.print("  Min Distance:    ");
  Serial.print(minDist, 4);  // Print 4 decimal places
  Serial.print(" (to ");
  Serial.print(closestColor);
  Serial.print(", tolerance = ");
  Serial.print(sensor->colorTol);
  Serial.println(")");
  
  // Print calibrated values for closest color
  Serial.print("  Calibrated RGBW: R=");
  Serial.print(closestCalVals[0]);
  Serial.print(", G=");
  Serial.print(closestCalVals[1]);
  Serial.print(", B=");
  Serial.print(closestCalVals[2]);
  Serial.print(", W=");
  Serial.println(closestCalVals[3]);
  
  // Print second closest distance
  Serial.print("  2nd Closest:     ");
  Serial.print(secondMinDist, 4);  // Print 4 decimal places
  Serial.print(" (to ");
  Serial.print(secondClosestColor);
  Serial.print(", margin = ");
  Serial.print(secondMinDist - minDist, 4);  // Print 4 decimal places
  Serial.println(")");
  
  // Print calibrated values for 2nd closest color
  Serial.print("  2nd Calibrated RGBW: R=");
  Serial.print(secondClosestCalVals[0]);
  Serial.print(", G=");
  Serial.print(secondClosestCalVals[1]);
  Serial.print(", B=");
  Serial.print(secondClosestCalVals[2]);
  Serial.print(", W=");
  Serial.println(secondClosestCalVals[3]);

  // Print RGBW values
  Serial.print("  RGBW Values:     R=");
  Serial.print(scanVals[0]);
  Serial.print(", G=");
  Serial.print(scanVals[1]);
  Serial.print(", B=");
  Serial.print(scanVals[2]);
  Serial.print(", W=");
  Serial.println(scanVals[3]);
  
  Serial.println();
}
