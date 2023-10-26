

// Servo Variables
const int TOPSERVO = 37;
const int BOTSERVO = 36;

// Servo Variables
unsigned int topExtPos = 240;
unsigned int topRetPos = 0;
boolean topExtBool = 0;

unsigned int botExtPos = 250;
unsigned int botRetPos = 0;
boolean botExtBool = 0;

int sweepDelay = 15;
int sweepStep = 1;

// Servo Definitions
Servo topServo;
Servo botServo;


void servoSetup() {
  // Servo Attachments
  topServo.attach(TOPSERVO);
  topServo.write(topRetPos);
  botServo.attach(BOTSERVO);
  botServo.write(botRetPos);
}

void topServoExtend(){
  if(topExtBool) {
    return;;
  }
  int angle = map(topExtPos, 0, 270, 0, 180);
  topServo.write(angle);
  topExtBool = 1;
}

void topServoRetract(){
  if(!topExtBool) {
    return;
  }
  int angle = map(topRetPos, 0, 270, 0, 180);
  topServo.write(angle);
  topExtBool = 0;
}

void botServoExtend(){
  if(botExtBool) {
    return;;
  }
  int angle = map(botExtPos, 0, 270, 0, 180);
  botServo.write(angle);
  botExtBool = 1;
}

void botServoRetract() {
  if(!botExtBool) {
    return;
  }
  int angle = map(botRetPos, 0, 270, 0, 180);
  botServo.write(angle);
  botExtBool = 0;
}
