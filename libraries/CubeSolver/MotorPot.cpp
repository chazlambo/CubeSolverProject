#include "MotorPot.h"

MotorPot::MotorPot(int board, int pin, int eepromAddr, Adafruit_PCF8591* adc)
    : board(board), pin(pin), eepromAddr(eepromAddr), adc(adc), value(0), calibration(defaultVal) {}

void MotorPot::begin() {
    pinMode(pin, INPUT);
    
    EEPROM.get(eepromAddr, calibration);
    if (calibration < minValid || calibration > maxValid) {
        calibration = defaultVal;
    }
}

int MotorPot::scan() {
    value = adc->analogRead(pin);
    return value;
}

bool MotorPot::isCalibrated() {
    return EEPROM.read(0) == flagValue;
}

bool MotorPot::loadCalibration() {
    if (!isCalibrated()) return false;
    EEPROM.get(eepromAddr, calibration);
    return calibration >= minValid && calibration <= maxValid;
}

bool MotorPot::saveCalibration() {
    value = scan();
    if (value < minValid || value > maxValid) return false;

    EEPROM.update(0, flagValue);
    EEPROM.put(eepromAddr, value);
    calibration = value;
    return true;
}

int MotorPot::getValue() const { return value; }
int MotorPot::getCalibration() const { return calibration; }
