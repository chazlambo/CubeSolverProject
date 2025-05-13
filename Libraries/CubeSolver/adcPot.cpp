#include "adcPot.h"

adcPot::adcPot(int board, int pin) {
    this->Board = board;
    this->Pin = pin;
}

void adcPot::setColorCal(char color, int rgbw[4]) {
    switch(color) {
        case 'R':
            for(int i = 0; i<4; i++) {
                this->cal_R[i] = rgbw[i];
            }
            break;
        case 'G':
            for(int i = 0; i<4; i++) {
                this->cal_G[i] = rgbw[i];
            }
            break;
        case 'B':
            for(int i = 0; i<4; i++) {
                this->cal_B[i] = rgbw[i];
            }
            break;
        case 'Y':
            for(int i = 0; i<4; i++) {
                this->cal_Y[i] = rgbw[i];
            }
            break;
        case 'O':
            for(int i = 0; i<4; i++) {
                this->cal_O[i] = rgbw[i];
            }
            break;
        case 'W':
            for(int i = 0; i<4; i++) {
                this->cal_W[i] = rgbw[i];
            }
            break;
        case 'E':
            for(int i = 0; i<4; i++) {
                this->cal_E[i] = rgbw[i];
            }
            break;
        default:
            return;
    }
}

void adcPot::setMotorCal(int cal) {
    if(cal > 255 || cal < 0) {
        return;
    }
    this->motorCal = cal;
}

char adcPot::colorParse(int rgbwScan[4], int tolerance) {
    // Initialize counters for each color
    int colorCounts[7] = {0}; // Indexes: 0-red, 1-green, 2-blue, 3-yellow, 4-orange, 5-white, 6-empty
    char colorLabels[7] = {'r', 'g', 'b', 'y', 'o', 'w', 'e'};
    
    for (int i = 0; i < 4; i++) {
        // Check each color calibration
        if (this->cal_R[i] - tolerance <= rgbwScan[i] && rgbwScan[i] <= this->cal_R[i] + tolerance) {
            colorCounts[0]++;
        }
        if (this->cal_G[i] - tolerance <= rgbwScan[i] && rgbwScan[i] <= this->cal_G[i] + tolerance) {
            colorCounts[1]++;
        }
        if (this->cal_B[i] - tolerance <= rgbwScan[i] && rgbwScan[i] <= this->cal_B[i] + tolerance) {
            colorCounts[2]++;
        }
        if (this->cal_Y[i] - tolerance <= rgbwScan[i] && rgbwScan[i] <= this->cal_Y[i] + tolerance) {
            colorCounts[3]++;
        }
        if (this->cal_O[i] - tolerance <= rgbwScan[i] && rgbwScan[i] <= this->cal_O[i] + tolerance) {
            colorCounts[4]++;
        }
        if (this->cal_W[i] - tolerance <= rgbwScan[i] && rgbwScan[i] <= this->cal_W[i] + tolerance) {
            colorCounts[5]++;
        }
        if (this->cal_E[i] - (tolerance + 40) <= rgbwScan[i] && rgbwScan[i] <= this->cal_E[i] + (tolerance + 40)) {
            colorCounts[6]++;
        }
    }
    
    // Determine the dominant color
    for (int i = 0; i < 7; i++) {
        if (colorCounts[i] >= 4) {
            return colorLabels[i];
        }
    }
    
    // Return unknown if no dominant color found
    return 'u';
}
