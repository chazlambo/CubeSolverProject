#include <Servo.h>

const int servoPin = 36;

Servo myservo;  // create servo object to control a servo
// twelve servo objects can be created on most boards

int pos = 0;    // variable to store the servo position
unsigned int angle = 0;


void setup() {
  myservo.attach(servoPin);  // attaches the servo on pin 9 to the servo object

  Serial.begin(115200);
  Serial.println("Starting");
  myservo.write(angle);
}

void loop() {
  Serial.println("Enter angle to write to servo: ");
  while(!Serial.available()){
    delay(10);
    }
  angle = Serial.parseInt();
  while(Serial.available()) {
    Serial.read();
  }
  Serial.print("User input was: ");
  Serial.println(angle);
  if(angle > 270) { angle = 270;} // (242 for top servo) (252 for bottom servo)

  // Convert to this servo
  Serial.print("Writing servo to: ");
  Serial.println(angle);
  angle = map(angle, 0, 270, 0, 180);
  myservo.write(angle);
  
  
}
