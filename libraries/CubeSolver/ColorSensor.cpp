#include "ColorSensor.h"

ColorSensor::ColorSensor(Adafruit_PCF8591* adcArray[9], const int pinArray[9], const int ledPinsIn[3], int eepromStartAddress) {
    for (int i = 0; i < 9; ++i) {
        adcs[i] = adcArray[i];
        pins[i] = pinArray[i];
    }
    for (int i = 0; i < 3; ++i) {
        ledPins[i] = ledPinsIn[i];
    }
    eepromBase = eepromStartAddress;
}

void ColorSensor::begin() {
    // Initialize LED pins
    for (int i = 0; i < 3; ++i) {
        pinMode(ledPins[i], OUTPUT);
        digitalWrite(ledPins[i], LOW); // Turn off by default
    }

    
    loadCalibration();
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

void ColorSensor::scanFace(char* output, int numScans, int delayMs, int tolerance) {
    int rgbw[4][9] = {};  // red, green, blue, white
    int channel = 0;

    for (int i = 0; i < numScans; ++i) {
        // Reset channel variable
        channel = 0;
        for (char ch : {'R', 'G', 'B', 'W'}) {
            // Set LEDs to specified color
            setLED(ch);
            delay(delayMs);

            // Add value to channel (sum will be averaged later)
            for (int j = 0; j < 9; ++j) {
                rgbw[channel][j] += readADC(j);
            }

            // Update channel variable
            channel++;
        }
    }

    setLED(0);  // Turn off LEDs

    for (int j = 0; j < 9; ++j) {
        int averaged[4];
        for (int k = 0; k < 4; ++k) {
            averaged[k] = rgbw[k][j] / numScans;
        }
        output[j] = getColor(j, averaged, tolerance);
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

char ColorSensor::getColor(int sensorIdx, const int rgbw[4], int tolerance) {
    // Calculate color distances to calibration values
    int dist_R = colorDistance(rgbw, cal_R[sensorIdx]);
    int dist_G = colorDistance(rgbw, cal_G[sensorIdx]);
    int dist_B = colorDistance(rgbw, cal_B[sensorIdx]);
    int dist_Y = colorDistance(rgbw, cal_Y[sensorIdx]);
    int dist_O = colorDistance(rgbw, cal_O[sensorIdx]);
    int dist_W = colorDistance(rgbw, cal_W[sensorIdx]);
    
    // Find closest match
    int minDist = dist_R;
    char closestColor = 'R';
    
    if (dist_G < minDist) {
        minDist = dist_G;
        closestColor = 'G';
    }
    if (dist_B < minDist) {
        minDist = dist_B;
        closestColor = 'B';
    }
    if (dist_Y < minDist) {
        minDist = dist_Y;
        closestColor = 'Y';
    }
    if (dist_O < minDist) {
        minDist = dist_O;
        closestColor = 'O';
    }
    if (dist_W < minDist) {
        minDist = dist_W;
        closestColor = 'W';
    }
    
    // Check if within tolerance
    if (minDist > tolerance) {
        return 'E'; // Error - no close match
    }
    
    return closestColor;
}

void ColorSensor::setColorCal(int sensorIdx, char color, const int rgbw[4]) {
    if (sensorIdx < 0 || sensorIdx >= 9) return;
    
    int* target;
    switch (color) {
        case 'R': target = cal_R[sensorIdx]; break;
        case 'G': target = cal_G[sensorIdx]; break;
        case 'B': target = cal_B[sensorIdx]; break;
        case 'Y': target = cal_Y[sensorIdx]; break;
        case 'O': target = cal_O[sensorIdx]; break;
        case 'W': target = cal_W[sensorIdx]; break;
        default: return;
    }
    
    for (int i = 0; i < 4; i++) {
        target[i] = rgbw[i];
    }
}

bool ColorSensor::loadCalibration() {
    // Check if calibration flag exists
    if (EEPROM.read(eepromBase) != flagValue) {
        return false;
    }
    
    int addr = eepromBase + 1;
    
    // Load all calibration data from EEPROM
    for (int sensor = 0; sensor < 9; sensor++) {
        for (int i = 0; i < 4; i++) {
            EEPROM.get(addr, cal_R[sensor][i]);
            addr += sizeof(int);
        }
        for (int i = 0; i < 4; i++) {
            EEPROM.get(addr, cal_G[sensor][i]);
            addr += sizeof(int);
        }
        for (int i = 0; i < 4; i++) {
            EEPROM.get(addr, cal_B[sensor][i]);
            addr += sizeof(int);
        }
        for (int i = 0; i < 4; i++) {
            EEPROM.get(addr, cal_Y[sensor][i]);
            addr += sizeof(int);
        }
        for (int i = 0; i < 4; i++) {
            EEPROM.get(addr, cal_O[sensor][i]);
            addr += sizeof(int);
        }
        for (int i = 0; i < 4; i++) {
            EEPROM.get(addr, cal_W[sensor][i]);
            addr += sizeof(int);
        }
    }
    
    return true;
}

bool ColorSensor::saveCalibration() {
    // First write the flag value
    EEPROM.update(eepromBase, flagValue);
    
    int addr = eepromBase + 1;
    
    // Save all calibration data to EEPROM
    for (int sensor = 0; sensor < 9; sensor++) {
        for (int i = 0; i < 4; i++) {
            EEPROM.put(addr, cal_R[sensor][i]);
            addr += sizeof(int);
        }
        for (int i = 0; i < 4; i++) {
            EEPROM.put(addr, cal_G[sensor][i]);
            addr += sizeof(int);
        }
        for (int i = 0; i < 4; i++) {
            EEPROM.put(addr, cal_B[sensor][i]);
            addr += sizeof(int);
        }
        for (int i = 0; i < 4; i++) {
            EEPROM.put(addr, cal_Y[sensor][i]);
            addr += sizeof(int);
        }
        for (int i = 0; i < 4; i++) {
            EEPROM.put(addr, cal_O[sensor][i]);
            addr += sizeof(int);
        }
        for (int i = 0; i < 4; i++) {
            EEPROM.put(addr, cal_W[sensor][i]);
            addr += sizeof(int);
        }
    }
    
    return true;
}

