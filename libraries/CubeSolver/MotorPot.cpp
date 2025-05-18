    #include "MotorPot.h"

    MotorPot::MotorPot(int board, int pin, int eepromFlagAddr, int eepromAddr[4], Adafruit_PCF8591* adc)
        : board(board), pin(pin), eepromFlagAddr(eepromFlagAddr), adc(adc), value(0){
            for (int i = 0; i < 4; i++) {
                this->eepromAddr[i] = eepromAddr[i];
                calibration[i] = defaultVal[i];
            }
            
            // Initialize calibration value bounds
            for (int i = 0; i < 4; i++) {
                minVal[i] = defaultVal[i] - tolerance;
                maxVal[i] = defaultVal[i] + tolerance;
            
                wrapped[i] = false;
            
                // Wrap below 0
                if (minVal[i] < 0) {
                    minVal[i] += 256;
                    wrapped[i] = true;
                }
            
                // Wrap above 255
                if (maxVal[i] > 255) {
                    maxVal[i] -= 256;
                    wrapped[i] = true;
                }
            }
        }

    void MotorPot::begin() {
        
        if (isCalibrated()) {
            // Load from EEPROM
            for (int i = 0; i < 4; i++) {
                calibration[i] = EEPROM.read(eepromAddr[i]);
            }
        } else {
            // Use default values
            for (int i = 0; i < 4; i++) {
                calibration[i] = defaultVal[i];
            }
        }
    
        // Clamp any bad values
        for (int i = 0; i < 4; i++) {
            if (calibration[i] < minVal[i] || calibration[i] > maxVal[i]) {
                calibration[i] = defaultVal[i];
            }
        }
    }

    int MotorPot::scan() {
        value = adc->analogRead(pin);
        return value;
    }

    bool MotorPot::isCalibrated() {
        return EEPROM.read(eepromFlagAddr) == flagValue;
    }

    int MotorPot::loadCalibration() {
        // Returns:
        // 0 - Calibrated Correctly
        // 1 - Not Calibrated (Flag not correct) 
        // 2 - Invalid calibration value - Using default

        if (!isCalibrated()) return 1;

        for (int i = 0; i < 4; i++) {
            calibration[i] = EEPROM.read(eepromAddr[i]);
        
            bool valid;
            if (!wrapped[i]) {
                // Normal case: range does not wrap around 0
                valid = (calibration[i] >= minVal[i] && calibration[i] <= maxVal[i]);
            } else {
                // Wrapped case: valid if value is >= min OR <= max (crosses 255 → 0)
                valid = (calibration[i] >= minVal[i] || calibration[i] <= maxVal[i]);
            }

            if (!valid) {
                // If any value is invalid, clear flag and return error
                calibration[i] = defaultVal[i];
                EEPROM.put(eepromFlagAddr, 0);  // Clear flag so system knows it's invalid
                return 2;  // Calibration data out of bounds
            }
        }
        
        return 0;
    }

    bool MotorPot::saveCalibration() {
        for (int i = 0; i < 4; i++) {
            int val = calibration[i];
            
            // Check if value is within range
            if (!wrapped[i]) {
                // Normal non-wrapping case: value must lie between min and max
                if (val < minVal[i] || val > maxVal[i]) {
                    return false; // Out of range
                }
            } else {                
                // Wrapped case: valid if val >= min OR val <= max (range wraps around 0–255)
                if (!(val >= minVal[i] || val <= maxVal[i])) {
                    return false; // Out of range
                }
            }
    
            // Store the calibration value to its EEPROM address
            EEPROM.put(eepromAddr[i], calibration[i]);
        }
        EEPROM.put(eepromFlagAddr, flagValue);
        return true;
    }

    int MotorPot::getCalibration(int index) {
        return calibration[index]; 
    }

    int MotorPot::setCalibration(int index){
        // Outputs:
        //  0 - Successfully set
        //  1 - Invalid index
        //  2 - Invalid value

        if (index >= 0 && index < 4) {
            int value = scan();

            if (value < 0 || value > 255) {
                return 2;
            }

            calibration[index] = value;
            return 0;
        }
        
        return 1;
    }

    int MotorPot::setCalibration(int index, int value){
        // Outputs:
        //  0 - Successfully set
        //  1 - Invalid index
        //  2 - Invalid value

        if (index >= 0 && index < 4) {
            if (value < 0 || value > 255) {
                return 2;
            }
            
            calibration[index] = value;
            return 0;
        }

        return 1;
    }
