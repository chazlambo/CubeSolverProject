#ifndef RUNNINGMEDIAN_H_SIM
#define RUNNINGMEDIAN_H_SIM
#include "Arduino.h"
class RunningMedian {
public:
    RunningMedian(uint8_t = 19) {}
    void  clear() {}
    void  add(float v) { last_ = v; }
    float getMedian()  { return last_; }
    float getAverage() { return last_; }
    float getLowest()  { return last_; }
    float getHighest() { return last_; }
private:
    float last_ = 0.0f;
};
#endif
