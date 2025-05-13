#include "cube_sensors.h"
#include "adcPot.h"

void setupRGB()
{
    for (int i = 0; i < 3; i++)
    {                                 // Iterate through RGB pins
        pinMode(COLOR1[i], OUTPUT);   // Initialize color sensor 1 pin mode
        digitalWrite(COLOR1[i], LOW); // Turn off color sensor 1 LEDs

        pinMode(COLOR2[i], OUTPUT);   // Initialize color sensor 2 pin mode
        digitalWrite(COLOR2[i], LOW); // Turn off color sensor 2 LEDs
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

void setupANO()
{
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

int scanADC(adcPot* pot)
{
    if (pot == nullptr) {
        return 0; // Handle null pointer case if needed
    }

    switch(pot->Board){                         // Based on the assigned board
        case ADC_ADDRESS[0]:
            return ADC1.analogRead(pot->Pin);   // Read value on the pin
        case ADC_ADDRESS[1]:
            return ADC2.analogRead(pot->Pin);
        case ADC_ADDRESS[2]:
            return ADC3.analogRead(pot->Pin);
        case ADC_ADDRESS[3]:
            return ADC4.analogRead(pot->Pin);
        case ADC_ADDRESS[4]:
            return ADC5.analogRead(pot->Pin);
        case ADC_ADDRESS[5]:
            return ADC6.analogRead(pot->Pin);
        default:
            return 0;
    }
}

void setLED(char color){
    switch(color) {
        case 'R':
            // Turn on Red LEDs
            digitalWrite(COLOR1[0], HIGH);
            digitalWrite(COLOR1[1], LOW);
            digitalWrite(COLOR1[2], LOW);

            digitalWrite(COLOR2[0], HIGH);
            digitalWrite(COLOR2[1], LOW);
            digitalWrite(COLOR2[2], LOW);
            break;

        case 'G':
            // Turn on Green LEDs
            digitalWrite(COLOR1[0], LOW);
            digitalWrite(COLOR1[1], HIGH);
            digitalWrite(COLOR1[2], LOW);

            digitalWrite(COLOR2[0], LOW);
            digitalWrite(COLOR2[1], HIGH);
            digitalWrite(COLOR2[2], LOW);
            break;

        case 'B':
            // Turn on Blue LEDs
            digitalWrite(COLOR1[0], LOW);
            digitalWrite(COLOR1[1], LOW);
            digitalWrite(COLOR1[2], HIGH);

            digitalWrite(COLOR2[0], LOW);
            digitalWrite(COLOR2[1], LOW);
            digitalWrite(COLOR2[2], HIGH);
            break;
        
        case 'W':
            // Turn on Red LED
            digitalWrite(COLOR1[0], HIGH);
            digitalWrite(COLOR1[1], HIGH);
            digitalWrite(COLOR1[2], HIGH);

            digitalWrite(COLOR2[0], HIGH);
            digitalWrite(COLOR2[1], HIGH);
            digitalWrite(COLOR2[2], HIGH);
            break;

        case '0':
            // Turn off all LED's
            digitalWrite(COLOR1[0], LOW);
            digitalWrite(COLOR1[1], LOW);
            digitalWrite(COLOR1[2], LOW);

            digitalWrite(COLOR2[0], LOW);
            digitalWrite(COLOR2[1], LOW);
            digitalWrite(COLOR2[2], LOW);
            break;
        
        default:
            return;
    }
}

void faceScan(adcPot * colorSensor[9]){
    int redScanVals[9];
    int greenScanVals[9];
    int blueScanVals[9];
    int whiteScanVals[9];

    int avgRed[9] = {0};
    int avgGreen[9] = {0};
    int avgBlue[9] = {0};
    int avgWhite[9] = {0};

    for(int i=0; i<numScans; i++){  // Scan each color numScans times

        setLED('R');                                    // Turn on red LED
        delay(colorDelay);                              // Wait colorDelay for sensor
        for(int j = 0; j<9; j++){                       // Read value for each sensor
            redScanVals[j] = scanADC(colorSensor[j]);
        }

        setLED('G');                                    // Turn on green LED
        delay(colorDelay);                              // Wait colorDelay for sensor
        for(int j = 0; j<9; j++){                       // Read value for each sensor
            greenScanVals[j] = scanADC(colorSensor[j]);
        }

        setLED('B');                                    // Turn on blue LED
        delay(colorDelay);                              // Wait colorDelay for sensor
        for(int j = 0; j<9; j++){                       // Read value for each sensor
            blueScanVals[j] = scanADC(colorSensor[j]);
        }

        setLED('W');                                    // Turn on all LEDs
        delay(colorDelay);                              // Wait colorDelay for sensor
        for(int j = 0; j<9; j++){                       // Read value for each sensor
            whiteScanVals[j] = scanADC(colorSensor[j]);
        }

        for(int i=0; i<9; i++) {                        // Add scan value for each color to variable to later be averaged
            avgRed[i] += redScanVals[i];
            avgGreen[i] += greenScanVals[i];
            avgBlue[i] += blueScanVals[i];
            avgWhite[i] += whiteScanVals[i];
        }
    }

    for(int i=0; i<9; i++) {                                    // For each potentiometer
        avgRed[i] /= numScans;                                    // Get the average R scan
        avgGreen[i] /= numScans;                                  // Get the average G scan
        avgBlue[i] /= numScans;                                   // Get the average B scan
        avgWhite[i] /= numScans;                                  // Get the average W scan

        int rgbw[4] = {avgRed[i], avgGreen[i], avgBlue[i], avgWhite[i]};    // Build a temporary RGBW variable

        char color = colorSensor[i]->colorParse(rgbw, colorScanTolerance);  // Get the color character based on rgbw scan values
        faceScanArray[i] = color;                                           // Write color to face scan variable.
    }
}

