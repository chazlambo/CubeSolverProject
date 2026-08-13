#ifndef PWMSERVO_H_SIM
#define PWMSERVO_H_SIM
#include "Arduino.h"
class PWMServo {
public:
    uint8_t attach(int) { return 1; }
    uint8_t attach(int, int, int) { return 1; }
    void    write(int a) { angle_ = a; }
    int     read() { return angle_; }
    bool    attached() { return true; }
private:
    int angle_ = 0;
};
#endif
