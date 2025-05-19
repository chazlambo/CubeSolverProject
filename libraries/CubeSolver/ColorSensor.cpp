#include "ColorSensor.h"

ColorSensor::ColorSensor(Adafruit_PCF8591* adcArray[9], const int pinArray[9], const int ledPinsIn[3], int eepromFlagAddress, int eepromAddresses[9][7][4]) {
    // Set pin values and EEPROM addresses
    for (int i = 0; i < 9; ++i) {
        adcs[i] = adcArray[i];
        pins[i] = pinArray[i];
        eepromAddr[i] = eepromAddresses[i];
    }

    // Set LED pin values
    for (int i = 0; i < 3; ++i) {
        ledPins[i] = ledPinsIn[i];
    }

    // Set EEPROM Flag Address
    eepromFlagAddr = eepromFlagAddress;
    
}

void ColorSensor::begin() {
    // Initialize LED pins
    for (int i = 0; i < 3; ++i) {
        pinMode(ledPins[i], OUTPUT);
        digitalWrite(ledPins[i], LOW); // Turn off by default
    }

    // Check if calibration can be loaded, otherwise reset to zero
    if (!loadCalibration()) {
        resetCalibration();
    }
}

int ColorSensor::readADC(int index) {
    return adcs[index]->analogRead(pins[index]);
}

void ColorSensor::setLED(char color) {
    // Determine state to set LEDs
    int state[3] = {LOW, LOW, LOW};

    switch(color) {
        case 'R':
            state[0] = HIGH;
            break;

        case 'G':
            state[1] = HIGH;
            break;

        case 'B':
            state[2] = HIGH;
            break;

        case 'W':
            state[0] = HIGH;
            state[1] = HIGH;
            state[2] = HIGH;
            break;
    
        case '0':   // Handle off input by doing nothing
            break;
    }

    // Set the state for LEDs
    for (int i = 0; i < 3; i++) {
        digitalWrite(ledPins[i], state[i]);
    }
}

void ColorSensor::scanFace() {
    int channel = 0;

    // Scan each channel numScans times
    for (int i = 0; i < numScans; ++i) {
        // Reset channel variable
        channel = 0;
        for (char ch : {'R', 'G', 'B', 'W'}) {
            // Set LEDs to specified color
            setLED(ch);
            delay(scanDelay);

            // Add value to channel (sum will be averaged later)
            for (int j = 0; j < 9; ++j) {
                rgbw[channel][j] += readADC(j);
            }

            // Update channel variable
            channel++;
        }
    }

    setLED(0);  // Turn off LEDs

    // Average scan values and save
    for (int j = 0; j < 9; ++j) {
        int averaged[4];
        for (int k = 0; k < 4; ++k) {
            averaged[k] = rgbw[k][j] / numScans;
            scanVals[j][k] = averaged[k];
        }
    }
}

void ColorSensor::getFaceColors(char *output[9]){
    for(int i = 0; i < 9; i++) {
        output[i] = getColor(i, scanVals[i])
    }
}

int ColorSensor::colorDistance(const int rgbw1[4], const int rgbw2[4]) {
    // Simple Euclidean distance in 4D space (R,G,B,W)
    int sumSq = 0;
    for (int i = 0; i < 4; i++) {
        int diff = rgbw1[i] - rgbw2[i];
        sumSq += diff * diff;
    }
    return sqrt(sumSq);
}

void ColorSensor::getColor(int sensorIdx, const int rgbw[4]) {
    // Initialize search variables
    int minDist = 999;
    char closestColor = 'U';  // Default to unknown

    const char colorChars[7] = { 'R', 'G', 'B', 'Y', 'O', 'W', 'E'};
    
    // Check distance to each color
    for (int c = 0; c < 7; ++c) {
        int dist = colorDistance(rgbw, cal[c][sensorIdx]);

        // If closest so far then update
        if (dist < minDist) {
            minDist = dist;
            closestColor = colorChars[c];
        }
    }

    // If not within tolerance to closest color, return unknown
    if (minDist > colorTol) return 'U';
    
    return closestColor;
}

int ColorSensor::colorIndex(char color) {
    switch (color) {
        case 'R': return 0;
        case 'G': return 1;
        case 'B': return 2;
        case 'Y': return 3;
        case 'O': return 4;
        case 'W': return 5;
        case 'E': return 6;
        default:  return -1;
    }
}

int ColorSensor::setColorCal(int sensorIdx, char color, const int rgbw[4]) {
    // Output:
    //  0 - Success
    //  1 - Invalid sensor index
    //  2 - Invalid color
    //  3 - Invalid RGBW value
    

    // Check valid sensor index
    if (sensorIdx < 0 || sensorIdx >= 9){
        return 1;
    } 

    // Get color index
    int colorIdx = colorIndex(color);

    // Validate color index
    if(colorIdx < 0 || colorIdx > 6) {
        return 2;
    }

    // Check valid RGBW values
    for (int i = 0; i < 4; i++) {
        if(rgbw[i] < 0 || rgbw[i] > 255) {
            return 3;
        }
    }
    
    // Get calibration array for desired sensor and color    
    int* target = calVals[sensorIdx][colorIdx];

    // Set calibration array to desired RGBW
    for (int i = 0; i < 4; i++) {
        target[i] = rgbw[i];
    }

    return 0;
}

int ColorSensor::getColorCal(int sensorIdx, char color, int channel)
{
    // Check valid sensor index
    if (sensorIdx < 0 || sensorIdx >= 9){
        return -1;
    } 

    // Check valid channel index
    if (channel < 0 || channel >= 4){
        return -1;
    } 

    // Get color index
    int colorIdx = colorIndex(color);

    int* target;
    if(colorIdx > 0 && colorIdx < 7) {
        return calVals[sensorIdx][colorIdx][channel];
    }

    return -1;
}

bool ColorSensor::loadCalibration() {
    // Check if calibration flag exists
    if (EEPROM.get(eepromFlagAddr) != flagValue) {
        return false;
    }
    
    // Load all calibration data from EEPROM
    for (int i = 0; i < 9; i++) {           // For each sensor
        for (int j = 0; j < 7; j++) {       // For each color
            for (int k = 0; k < 4; k++) {   // For each RGBW value
                calVals[i][j][k] = EEPROM.get(eepromAddr[i][j][k]);
            }
        }
    }
    
    return true;
}

bool ColorSensor::saveCalibration() {
    // First write the flag value
    EEPROM.put(eepromFlagAddr, flagValue);
    
    // Save all calibration data to EEPROM
    for (int i = 0; i < 9; i++) {           // For each sensor
        for (int j = 0; j < 7; j++) {       // For each color
            for (int k = 0; k < 4; k++) {   // For each RGBW value
                EEPROM.put(eepromAddr[i][j][k], calVals[i][j][k]);
            }
        }
    }
    
    return true;
}

void ColorSensor::resetCalibration(int value = 0) {
    for (int i = 0; i < 9; i++) {
        for (int j = 0; j < 7; j++) {
            for (int k = 0; k < 4; k++) {
                calVals[i][j][k] = value;
            }
        }
    }
}