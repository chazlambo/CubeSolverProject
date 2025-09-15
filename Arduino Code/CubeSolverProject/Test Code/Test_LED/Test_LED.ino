#include <CubeSystem.h>

CubeSystem Cube;

void setup() {
  Cube.begin();

}

void loop() {
  for (char ch : {'R', 'G', 'B', 'W'}) {
            // Set LEDs to specified color
            colorSensor1.setLED(ch);
            colorSensor2.setLED(ch);
            delay(2000);
        }
}
