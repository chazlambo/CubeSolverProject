#include "cube_basic.h"

void basicSetup(){
    Serial.begin(baudRate);
    pinMode(POWPIN, INPUT);
}

bool powerCheck(){
    bool status = digitalRead(POWPIN);
    return status;
}
