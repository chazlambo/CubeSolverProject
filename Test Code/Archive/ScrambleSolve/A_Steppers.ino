// Libraries


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

// Initialize Ring Stepper Variables
int ringPos = 0;
int ringStepSpeed = 800;
int ringStepAccel = 400;
int ringRetPos = 0;
int ringExtPos = 400;
int ringMovePos = 0;
bool ringExtended = 0;

void stepperSetup() {
  // Initialize Enable Pin
  pinMode(ENPIN, OUTPUT);
  digitalWrite(ENPIN, HIGH);

  // Initialize Cube Steppers and Multi Stepper
  initStepper(multiStep, upStepper);
  initStepper(multiStep, rightStepper);
  initStepper(multiStep, frontStepper);
  initStepper(multiStep, downStepper);
  initStepper(multiStep, leftStepper);
  initStepper(multiStep, backStepper);

  // Initialize Ring Stepper
  initRingStepper(ringStepper); 
}

void initRingStepper(AccelStepper &ringStep){
  // Initialize Ring Position
  ringStep.setMaxSpeed(ringStepSpeed);
  ringStep.setAcceleration(ringStepAccel);
  digitalWrite(ENPIN, LOW);
  ringStep.runToNewPosition(ringExtPos);
  ringStep.runToNewPosition(ringRetPos);
  digitalWrite(ENPIN, HIGH);
  ringExtended = 0;
}

void initStepper(MultiStepper &multiStepper, AccelStepper &newStepper) {
  newStepper.setMaxSpeed(stepSpeed);
  multiStepper.addStepper(newStepper);
}

void ringExtend() {
  digitalWrite(ENPIN, LOW); // Enable motors
  if(ringExtended) {        // If ring is already extended
    return;                 // Exit Function
  }
  ringStepper.runToNewPosition(ringExtPos); // Extend Ring
  ringExtended = 1;                         // Set ring state
  digitalWrite(ENPIN, HIGH);                // Disable motors
}

void ringRetract() {
  digitalWrite(ENPIN, LOW); // Enable motors
  if(!ringExtended){        // If ring is already retracted
    return;                 // Exit function
  }   
  ringStepper.runToNewPosition(ringRetPos); // Retract Ring
  ringExtended = 0;                         // Set ring state
  digitalWrite(ENPIN, HIGH);                // Disable motors
}
