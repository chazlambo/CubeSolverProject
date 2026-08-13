#ifndef MULTISTEPPER_H_SIM
#define MULTISTEPPER_H_SIM
#include "AccelStepper.h"
class MultiStepper {
public:
    bool addStepper(AccelStepper&) { return true; }
    void moveTo(long[]) {}
    bool run() { return false; }
    void runSpeedToPosition() {}
};
#endif
