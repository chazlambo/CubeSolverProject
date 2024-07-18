#include "cube_sensors.h"

void setupRGB(){
    for(int i=0; i<3; i++){             // Iterate through RGB pins
        pinMode(COLOR1[i], OUTPUT);     // Initialize color sensor 1 pin mode
        digitalWrite(COLOR1[i], LOW);   // Turn off color sensor 1 LED's

        pinMode(COLOR2[i], OUTPUT);     // Initialize color sensor 2 pin mode
        digitalWrite(COLOR2[i], LOW);   // Turn off color sensor 2 LED's
    }
}

void setupI2C()
{
    // Initialize I2C Bus
    Wire.begin();

    // Initialize ADC modules
    ADC1.begin(ADC_ADDRESS[0]);
    ADC2.begin(ADC_ADDRESS[1]);
    ADC3.begin(ADC_ADDRESS[2]);
    ADC4.begin(ADC_ADDRESS[3]);
    ADC5.begin(ADC_ADDRESS[4]);
    ADC6.begin(ADC_ADDRESS[5]);
}

void setupANO(){
    // Initialize ANO pins
    ANO.pinMode(ANO_SWITCH_UP, INPUT_PULLUP);
    ANO.pinMode(ANO_SWITCH_DOWN, INPUT_PULLUP);
    ANO.pinMode(ANO_SWITCH_LEFT, INPUT_PULLUP);
    ANO.pinMode(ANO_SWITCH_RIGHT, INPUT_PULLUP);
    ANO.pinMode(ANO_SWITCH_SELECT, INPUT_PULLUP);

    // Get starting encoder position
    encoder_position = ANO.getEncoderPosition();

    // Enable Interrupts
    ANO.enableEncoderInterrupt();
    ANO.setGPIOInterrupts((uint32_t)1 << ANO_SWITCH_UP, 1);
}
