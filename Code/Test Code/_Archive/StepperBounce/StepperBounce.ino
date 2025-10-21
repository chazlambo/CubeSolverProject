#include <AccelStepper.h>

// Define a stepper and the pins it will use
const int directionPin = 3;
const int stepPin = 4;

int stepSpeed = 800; // 1000 for fast
int stepAccel = 400; // 2000 for really fast
int startPos = 0;
int pos = 400; // 400 for full rotation
int movePos = 0;
AccelStepper stepper(1, stepPin, directionPin);

void setup()
{  
  Serial.begin(115200);
  Serial.println("Starting Program");
  while(!Serial.available());
  while(Serial.available()) {
    Serial.read();
  }
  stepper.setMaxSpeed(stepSpeed);
  stepper.setAcceleration(stepAccel);
  stepper.runToNewPosition(pos);
  stepper.runToNewPosition(startPos);
}

void loop()
{
    movePos = pos;
    while(!Serial.available()){}
    while(Serial.available()) {
      Serial.read();
    }
    
    Serial.print("Moving to: ");
    Serial.println(movePos);
    stepper.runToNewPosition(movePos);

    
    movePos = 0;
    while(!Serial.available()){}
    while(Serial.available()) {
      Serial.read();
    }
    
    Serial.print("Moving to: ");
    Serial.println(movePos);
    stepper.runToNewPosition(movePos);    
}
