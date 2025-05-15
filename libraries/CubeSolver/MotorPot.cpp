    #include "MotorPot.h"

    MotorPot::MotorPot(int board, int pin, int eepromFlagAddr, int eepromAddr, Adafruit_PCF8591* adc)
        : board(board), pin(pin), eepromFlagAddr(eepromFlagAddr), eepromAddr(eepromAddr), adc(adc), value(0), calibration(defaultVal) {}

    void MotorPot::begin() {
        pinMode(pin, INPUT);
        
        if (isCalibrated()){
            EEPROM.get(eepromAddr, calibration);
        }
        else {
            calibration = defaultVal;
        }
        
        if (calibration < minValid || calibration > maxValid) {
            calibration = defaultVal;
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
        EEPROM.get(eepromAddr, calibration);
        
        // Ensure calibration is valid
        if (calibration < minValid || calibration > maxValid) {
            calibration = defaultVal;
            EEPROM.update(eepromFlagAddr, 0);   // Flag for recalibration
            return 2;             
        }

        return 0;
    }

    bool MotorPot::saveCalibration() {
        value = scan();
        if (value < minValid || value > maxValid) return false;

        EEPROM.update(eepromFlagAddr, flagValue);
        EEPROM.put(eepromAddr, value);
        calibration = value;
        return true;
    }

    int MotorPot::getCalibration() {
        if (isCalibrated()) {
            EEPROM.get(eepromAddr, calibration);
        }

        return calibration; }
