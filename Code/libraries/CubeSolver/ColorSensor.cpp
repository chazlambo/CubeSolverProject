#include "ColorSensor.h"


ColorSensor::ColorSensor(TCA9548* multiplexers[2], const int LEDPIN, int muxOrder[9], int channelOrder[9], int eepromFlagAddress, int eepromAddresses[9][7][4])
: ledPin(LEDPIN), eepromFlagAddr(eepromFlagAddress) {

    // Assign muxes
    for (int i = 0; i < 2; i++) {
        this->multiplexers[i] = multiplexers[i];
    }

    for (int i = 0; i < 9; i++) {
        // Assign mux and channel order vectors
        this->muxOrder[i]     = muxOrder[i];
        this->channelOrder[i] = channelOrder[i];
        for (int j = 0; j < 7; j++) {
            for (int k = 0; k < 4; k++) {
                this->eepromAddr[i][j][k] = eepromAddresses[i][j][k];
            }
        }
    }
    
}

int ColorSensor::begin() {
    // Begins color sensor object
    // Returns: 
    //  0 - Success
    //  1 - Failed to being VEML color sensor
    //  1X - Multiplexer X not found on I2C wire
    //  2X - Multiplexer X failed to begin

    // Initialize LED pin
    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, LOW);

    // Check if calibration can be loaded, otherwise reset to zero
    if (!loadCalibration()) {
        resetCalibration();
    }

    for (int i = 0; i < 2; i++){
        // Check if MUX is found on I2C Wire
        if(!multiplexers[i]->isConnected()){
            return 10 + i;
        }

        // Begin Mux
        if(!multiplexers[i]->begin()){
            return 20 + i;
        } 
    }

    // Initialize VEML6040
    if (!veml.begin()) {
        return 1;
    }
    veml.setConfiguration(integrationTime);  // Set integration time

    return 0;
}

void ColorSensor::readSensor(int sensorIdx) {
    // Look up which multiplexer and channel this sensor is on
    int muxIdx = muxOrder[sensorIdx];
    int chan   = channelOrder[sensorIdx];

    // Disable all channels in both muxes (just in case)
    multiplexers[0]->setChannelMask(0x00);
    multiplexers[1]->setChannelMask(0x00);

    // Tell the multiplexer to enable only this channel
    multiplexers[muxIdx]->selectChannel(chan);

    // Give the mux a moment to settle before attempting to read
    delayMicroseconds(50);

    currentRGBW[0] = veml.getRed();
    currentRGBW[1] = veml.getGreen();
    currentRGBW[2] = veml.getBlue();
    currentRGBW[3] = veml.getWhite();

    // Disable all channels in both muxes (just in case)
    multiplexers[muxIdx]->setChannelMask(0x00);
}

void ColorSensor::setLED(bool ledState) {
    digitalWrite(ledPin, ledState);
}

void ColorSensor::scanFace() {
    RunningMedian filter[9][4] = {
        {RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans)},
        {RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans)},
        {RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans)},
        {RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans)},
        {RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans)},
        {RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans)},
        {RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans)},
        {RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans)},
        {RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans)}
      };
      
    // Turn on LED
    setLED(true);

    // Scan numScans times
    for (int i = 0; i < numScans; ++i) {
        for (int j = 0; j < 9; ++j) {
            readSensor(j);  // fills currentRGBW[0..3]

            // Push each channel into the corresponding filter
            for (int k = 0; k < 4; ++k) {
                filter[j][k].add(currentRGBW[k]);
            }
        }
    }

    // Turn off LED
    setLED(false);

    // Save median values into scanVals
    for (int j = 0; j < 9; ++j) {
        for (int k = 0; k < 4; ++k) {
            scanVals[j][k] = filter[j][k].getMedian();
        }
    }
}

void ColorSensor::getFaceColors(char output[9]){
    for(int i = 0; i < 9; i++) {
        output[i] = getColor(i, scanVals[i]);
    }
}

const int *ColorSensor::getScanValRow(int idx)
{
    return scanVals[idx]; 
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

char ColorSensor::getColor(int sensorIdx, const int rgbw[4]) {
    // Initialize search variables
    int minDist = 999;
    char closestColor = 'U';  // Default to unknown

    const char colorChars[7] = { 'R', 'G', 'B', 'Y', 'O', 'W', 'E'};
    
    // Check distance to each color
    for (int c = 0; c < 7; ++c) {
        int dist = colorDistance(rgbw, calVals[sensorIdx][c]);

        // If closest so far then update
        if (dist < minDist) {
            minDist = dist;
            closestColor = colorChars[c];
        }
    }

    // If not within tolerance to closest color, return unknown
    if (minDist > colorTol){
        // TODO: DEBUG REMOVE LATER
        Serial.print("Min Dist: ");
        Serial.println(minDist);
        Serial.print("Closest Color: ");
        Serial.println(closestColor);

        return 'U';
    } 
    
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

int ColorSensor::getColorCal(int sensorIdx, char color, int channel){
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

    if(colorIdx >= 0 && colorIdx < 7) {
        return calVals[sensorIdx][colorIdx][channel];
    }

    return -1;
}

bool ColorSensor::loadCalibration() {
    // Check if calibration flag exists
    int flag;
    EEPROM.get(eepromFlagAddr, flag);

    if (flag != flagValue) {
        return false;
    }
    
    // Load all calibration data from EEPROM
    for (int i = 0; i < 9; i++) {           // For each sensor
        for (int j = 0; j < 7; j++) {       // For each color
            for (int k = 0; k < 4; k++) {   // For each RGBW value
                EEPROM.get(eepromAddr[i][j][k], calVals[i][j][k]);
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
    
    EEPROM.put(eepromFlagAddr, flagValue);
    return true;
}

void ColorSensor::resetCalibration() {
    for (int i = 0; i < 9; i++) {
        for (int j = 0; j < 7; j++) {
            for (int k = 0; k < 4; k++) {
                calVals[i][j][k] = 0;
            }
        }
    }
}