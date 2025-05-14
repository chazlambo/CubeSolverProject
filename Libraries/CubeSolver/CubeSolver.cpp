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

int motorHome()
{
    // Homes motors
    //
    // Outputs:
    //      0 - Success
    //      1 - Motors not calibrated
    //      2 - Did not reach threshold in time

    int threshold = 1;
    int stepSize = 5;
    int timeout = 5000; // Timeout after 5 seconds

    // Initialize variables
    bool aligned = false;
    int t_home_start = millis();

    // Retrieve calibrated values and check if calibrated
    if (!getMotorCalibrated()) {
        return 1;
    }

    // Start loop
    while (!aligned) {
        allAligned = true;

        for (int i = 0; i < numMotors; i++) {
            int currentVal = scanADC(MotorPots[i]);
            int targetVal = motorCals[i];

            if (abs(currentVal - targetVal) > threshold) {
                allAligned = false;

                if (currentVal < targetVal) {
                    pos[i] += stepSize;
                } else {
                    pos[i] -= stepSize;
                }
            }
        }

        // Apply new positions
        multiStep.moveTo(pos);
        multiStep.runSpeedToPosition();  // Blocking run
    }
    
    return false;
}
