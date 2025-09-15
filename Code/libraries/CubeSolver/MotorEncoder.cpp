#include "MotorEncoder.h"

MotorEncoder::MotorEncoder(int channel, int eepromFlagAddr, int eepromAddr[4], Adafruit_TCA9548A* tca, int as5600Addr)
    : channel(channel), as5600Addr(as5600Addr), tca(tca), eepromFlagAddr(eepromFlagAddr) {
    
    for (int i = 0; i < 4; i++){
        this->eepromAddr[i] = eepromAddr[i];
    } 
}

int MotorEncoder::begin() {
    // Begins motor encoder object
    // Returns: 
    //  0 - Success
    //  1 - Invalid EEPROM Address
    //  1X - Load calibration failed
    
    // Validate EEPROM
    for (int i = 0; i < 4; i++) {
        if (eepromAddr[i] < 0 || eepromAddr[i] > EEPROM.length() - 2) {
            Serial.print("EEPROM address invalid: "); Serial.println(eepromAddr[i]);
            return 1;
        }
    }

    // Load calibration
    int loadError = loadCalibration();
    if(loadError) { // If load calibration failed
        return loadError + 10;
    }

    // Success
    return 0;
}

bool MotorEncoder::selectMux() {
    if (!tca) {
        return false;
    }

    bool result = tca->enableChannel(channel);
    return result;
}

bool MotorEncoder::deselectMux() {
    if (!tca) {
        return false;
    }

    bool result = tca->disableChannel(channel);
    return result;
}

int MotorEncoder::scan() {
    // RETURNS: 
    //  >= 0 : Raw 12-bit angle value from AS5600 [0–4095]
    //   -1 : Failed to select TCA9548A mux channel
    //   -2 : I2C write to AS5600 failed (e.g. disconnected device)
    //   -3 : I2C read from AS5600 failed (wrong address, bus issue)

    // Attempt to select the correct channel on the TCA9548A mux
    if (!selectMux()) {
        return -1;
    }

    // Request angle register from AS5600 (starts at 0x0E)
    Wire.beginTransmission(as5600Addr);
    Wire.write(0x0E);  // ANGLE MSB register
    if (Wire.endTransmission(false) != 0) {  // Repeated start, no stop
        deselectMux();
        return -2;
    }

    // Request 2 bytes: MSB (0x0E) and LSB (0x0F)
    int n = Wire.requestFrom((int)as5600Addr, 2);
    if (n != 2) {
        deselectMux();
        return -3;
    }

    int msb = Wire.read();
    int lsb = Wire.read();
    deselectMux();

    // Combine MSB and LSB into full 12-bit angle (upper 4 MSB bits ignored)
    value = (msb << 8) | lsb;
    value &= 0x0FFF;  // mask to 12 bits
    return value;
}


bool MotorEncoder::isCalibrated() {
    return EEPROM.read(eepromFlagAddr) == kFlagValue;
}

int MotorEncoder::loadCalibration() {
    // Load calibrated data from EEPROM
    // Returns:
    //  0 - Success
    //  1 - Not calibrated (Calibration flag not set)
    //  2 - Invalid calibration value (outside range)
    
    // Check if calibrated
    if (!isCalibrated()) return 1;

    // Check calibrated values to determine if in range
    bool ok = true;
    for (int i = 0; i < 4; i++) {
        int v = 0;
        EEPROM.get(eepromAddr[i], v);
        if (v > 4095) ok = false;     // outside AS5600 range
        calibration[i] = v;
    }

    // If any calibration value not in range, throw error
    if (!ok) {
        // Clear flag to force recalibration next boot
        EEPROM.update(eepromFlagAddr, 0);
        return 2;
    }
    return 0;
}

bool MotorEncoder::saveCalibration() {
    // Validate range
    for (int i = 0; i < 4; i++) {
        if (calibration[i] > 4095){
            EEPROM.update(eepromFlagAddr, 0);
            return false;
        }
    }

    // Write values
    for (int i = 0; i < 4; i++) {
        EEPROM.put(eepromAddr[i], calibration[i]);
    }

    // Set flag
    EEPROM.update(eepromFlagAddr, kFlagValue);
    return true;
}

int MotorEncoder::getCalibration(int index) {
    if (index < 0 || index > 3) return -1;
    return calibration[index];
}

int MotorEncoder::setCalibration(int index) {
    // Sets calibration values in class
    // Returns:
    //    0 - Success
    //   -1 - Invalid index
    //  -1X - Scan failed
    //   -2  - Invalid calibration value

    // Check for valid index
    if (index < 0 || index > 3){
        return -1;
    }

    // Scan current value
    int v = scan();

    // Check for succssful valid scan
    if (v < 0) {
        return v - 10;
    }

    // Check for valid calibration value
    if (v > 4095){
        return -2;
    }
    
    // Update calibration value at desired index
    calibration[index] = v;
    
    return 0;   // Success
}

int MotorEncoder::setCalibration(int index, int calValue) {
    // Sets calibration values in class
    // Returns:
    //   0 - Success
    //  -1 - Invalid index
    //  -2  - Invalid calibration value
    
    // Check for valid index
    if (index < 0 || index > 3){
        return -1;
    }

    // Check for valid calibration value
    if (calValue < 0 || calValue > 4095){
        return -2;
    }

    // Update calibration value at desired index
    calibration[index] = calValue;
    return 0;   // Success
}
