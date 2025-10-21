#include <AccelStepper.h>

// Define a stepper and the pins it will use
const int directionPin = 9;
const int stepPin = 10;
const int enPin = 0;

int stepSpeed = 800; // 800 default, 20k for cube??
int stepAccel = 400; // 400 default, 20k for cube??
int startPos = 0;
int pos = 400/4; // 400 for full rotation
int movePos = 0;
AccelStepper stepper(1, stepPin, directionPin);

void setup()
{  
  Serial.begin(115200);
  Serial.println("Starting Motor");
  pinMode(enPin, OUTPUT);

  Serial.println("Enter anything to enable motor");
  while(!Serial.available());
  while(Serial.available()) {
      Serial.read();
  }
  digitalWrite(enPin, LOW);
  
  Serial.println("Initializing Motor");
  stepper.setMaxSpeed(stepSpeed);
  stepper.setAcceleration(stepAccel);
  stepper.runToNewPosition(pos);
  stepper.runToNewPosition(startPos);


  Serial.println("Motor enabled, enter anything to begin");
  while(!Serial.available());
  while(Serial.available()) {
      Serial.read();
  }
  
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
