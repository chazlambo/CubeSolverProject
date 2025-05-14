#include "CubeSolver.h"

void mainSetup()
{
    Serial.begin(baudRate);
    pinMode(POWPIN, INPUT);

    if(!skipMotorInt){
        servoSetup();
        stepperSetup();
    }

    setupI2C();    // Sets up I2C devices
    //setupANO();    // Sets up ANO rotary encoder
    setupPots();   // Set up Motor Potentiometers
}

bool powerCheck(){
    bool status = digitalRead(POWPIN);
    return status;
}