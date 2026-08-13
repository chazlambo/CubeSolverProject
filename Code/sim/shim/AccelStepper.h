// AccelStepper.h — desktop shim. Steppers report as instantly arrived; the
// simulator fakes motion timing at the CubeSystem level instead.
#ifndef ACCELSTEPPER_H_SIM
#define ACCELSTEPPER_H_SIM
#include "Arduino.h"
class AccelStepper {
public:
    enum MotorInterfaceType { FUNCTION = 0, DRIVER = 1, FULL2WIRE = 2, FULL4WIRE = 4 };
    AccelStepper(uint8_t = 1, uint8_t = 2, uint8_t = 3, uint8_t = 4, uint8_t = 5, bool = true) {}
    void setMaxSpeed(float) {}
    void setAcceleration(float) {}
    void setSpeed(float) {}
    void setPinsInverted(bool = false, bool = false, bool = false) {}
    void setPinsInverted(bool, bool, bool, bool, bool) {}
    void setEnablePin(uint8_t) {}
    void moveTo(long p) { pos_ = p; }
    void move(long d)   { pos_ += d; }
    void runToNewPosition(long p) { pos_ = p; }
    void runToPosition() {}
    bool run() { return false; }
    bool runSpeed() { return false; }
    bool runSpeedToPosition() { return false; }
    long currentPosition() { return pos_; }
    void setCurrentPosition(long p) { pos_ = p; }
    long targetPosition() { return pos_; }
    long distanceToGo() { return 0; }
    void disableOutputs() {}
    void enableOutputs() {}
    void stop() {}
private:
    long pos_ = 0;
};
#endif
