// Libraries
#include <AccelStepper.h>
#include <MultiStepper.h>

// Pin Definitions
const int ENPIN = 0;
const int DIR1 = 1;
const int STEP1 = 2;
const int DIR2 = 3;
const int STEP2 = 4;
const int DIR3 = 5;
const int STEP3 = 6;
const int DIR4 = 7;
const int STEP4 = 8;
const int DIR5 = 9;
const int STEP5 = 10;
const int DIR6 = 11;
const int STEP6 = 12;
const int DIR7 = 28;
const int STEP7 = 29;

// Initialize Stepper Motor Objects
AccelStepper frontStepper(1, STEP1, DIR1);
AccelStepper backStepper(1, STEP2, DIR2);
AccelStepper leftStepper(1, STEP3, DIR3);
AccelStepper rightStepper(1, STEP4, DIR4);
AccelStepper upStepper(1, STEP5, DIR5);
AccelStepper downStepper(1, STEP6, DIR6);
AccelStepper ringStepper(1, STEP7, DIR7);
MultiStepper multiStep; // Top, Right, Front, Down, Left, Back

// Initialize Position Variable
int pos[6] = {0, 0, 0, 0, 0, 0};
int turnStep = 100; // Quarter Turn

// Initialize Stepper Speed Variable
int stepSpeed = 1000; // Set to 1000?

// Iterator variable
int i = 0;
int j = 0;

void setup() {
  Serial.begin(115200);

  // Initialize Enable Pin
  pinMode(ENPIN, OUTPUT);
  digitalWrite(ENPIN, HIGH);

  // Initialize Steppers and Multi Stepper
  initStepper(multiStep, upStepper);
  initStepper(multiStep, rightStepper);
  initStepper(multiStep, frontStepper);
  initStepper(multiStep, downStepper);
  initStepper(multiStep, leftStepper);
  initStepper(multiStep, backStepper);

  Serial.println("Enter anything to begin");
  while (!Serial.available());

}

bool firstLoop = 1;
void loop() {
  i = i % 12; //Loops through 0-11
  delay(20);

  if (i == 0) {
    if (firstLoop) {
      firstLoop = 0;
      j++;
    }
    else {
      j++;
      j = j % 20;
      if (j == 0) {
        delay(5000);
      }
    }

  }

  if (Serial.available()) {
    while (Serial.available()) {
      Serial.read();
    }
    while (!Serial.available()) {
      delay(10);
    }
    while (Serial.available()) {
      Serial.read();
    }
  }

  switch (i) {
    case 0: // Up
      Serial.println("U");
      pos[0] += turnStep;
      break;

    case 1: // Right
      Serial.println("R");
      pos[1] += turnStep;
      break;

    case 2: // Front
      Serial.println("F");
      pos[2] += turnStep;
      break;

    case 3: // Down
      Serial.println("D");
      pos[3] += turnStep;
      break;

    case 4: // Left
      Serial.println("L");
      pos[4] += turnStep;
      break;

    case 5: // Back
      Serial.println("B");
      pos[5] += turnStep;
      break;

    case 6: // Up
      Serial.println("U'");
      pos[0] -= turnStep;
      break;

    case 7: // Right
      Serial.println("R'");
      pos[1] -= turnStep;
      break;

    case 8: // Front
      Serial.println("F'");
      pos[2] -= turnStep;
      break;

    case 9: // Down
      Serial.println("D'");
      pos[3] -= turnStep;
      break;

    case 10: // Left
      Serial.println("L'");
      pos[4] -= turnStep;
      break;

    case 11: // Back
      Serial.println("B'");
      pos[5] -= turnStep;
      break;
  }

  digitalWrite(ENPIN, LOW);
  multiStep.moveTo(pos);
  multiStep.runSpeedToPosition();
  digitalWrite(ENPIN, HIGH);


  //delay(1000);
  i++;
}

void initStepper(MultiStepper &multiStepper, AccelStepper &newStepper) {
  newStepper.setMaxSpeed(stepSpeed);
  multiStepper.addStepper(newStepper);
}
