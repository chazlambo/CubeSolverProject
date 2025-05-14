#include "CubeSolver.h"

void mainSetup()
{
    Serial.begin(baudRate);
    pinMode(POWPIN, INPUT);

    // TODO: Remove this
    if(!skipMotorInt){
        servoSetup();
        stepperSetup();
    }

    setupRGB();    // Sets up color sensors
    setupI2C();    // Sets up I2C devices
    //setupANO();    // Sets up ANO rotary encoder

    // Home motors
    motorHomeState = homeMotors();
}

bool powerCheck(){
    bool status = digitalRead(POWPIN);
    return status;
}

int homeMotors()
{
    // Homes motors
    //
    // Outputs:
    //      0 - Success
    //      1 - Motors not calibrated
    //      2 - Did not reach threshold in time

    int threshold = 1;
    int stepSize = 1;
    unsigned long timeout = 5000; // Timeout after 5 seconds

    // Initialize variables
    bool aligned = false;
    unsigned long t_home_start = millis();
    unsigned long t_home = millis();

    // Retrieve calibrated values and check if calibrated
    if (!getMotorCalibration()) {
        return 1;
    }

    // Start loop
    digitalWrite(ENPIN, LOW);
    while (!aligned) {
        aligned = true;

        // Check each motor
        for (int i = 0; i < numMotors; i++) {
            int currentVal = scanADC(MotorPots[i]);
            int targetVal = motorCals[i];

            if (abs(currentVal - targetVal) > threshold) {
                aligned = false;

                // If Upper Motor
                if (i == 0) { // Reverse direction for upper motor
                    if(currentVal > targetVal) {
                        pos[i] -= stepSize;
                    }
                    else {
                        pos[i] += stepSize;
                    }
                }
                // If any other motor
                else {
                    if(currentVal > targetVal) {
                        pos[i] += stepSize;
                    }
                    else {
                        pos[i] -= stepSize;
                    }
                }
            }
        }

        // Apply new positions
        multiStep.moveTo(pos);
        multiStep.runSpeedToPosition();  // Blocking run


        // Add function timeout
        t_home = millis();
        if(t_home - t_home_start > timeout) {
            return 2;
        }
    }
    digitalWrite(ENPIN, HIGH);

    // Manually reset stepper position
    upStepper.setCurrentPosition(0);
    rightStepper.setCurrentPosition(0);
    frontStepper.setCurrentPosition(0);
    downStepper.setCurrentPosition(0);
    leftStepper.setCurrentPosition(0);
    backStepper.setCurrentPosition(0);

    
    return 0;   // Return successful alignment
}
