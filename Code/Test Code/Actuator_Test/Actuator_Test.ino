#include <CubeSystem.h>

CubeSystem Cube;
bool alignCheck = true;

void setup() {
  Serial.println("Starting Program...");

  Cube.begin();

  Serial.print("\nMotors Calibrated: ");
  Serial.println(Cube.getMotorCalibration());

  printMenu();
}

void loop() {
  while (!Serial.available()) { delay(50); }  // Wait for Serial
  int input = Serial.parseInt();              // Parse input as integer
  // while(Serial.available()) {Serial.read();}  // Flush serial buffer

  if ((input <= 0) || (input > 18)) {  // Check for valid input
    Serial.println("Invalid input...");
    printMenu();  // Print menu
    return;
  }

  switch (input) {
    case 1:
      Serial.println("Toggling Ring");
      Cube.toggleRing();
      break;

    case 2:
      Serial.println("Toggling Bottom Servo");
      Cube.toggleBotServo();
      break;

    case 3:
      Serial.println("Toggling Top Servo");
      Cube.toggleTopServo();
      break;

    case 4:
      Serial.println("Moving: F");
      Cube.executeMove("F", false, alignCheck);
      break;

    case 5:
      Serial.println("Moving F'");
      Cube.executeMove("F'", false, alignCheck);
      break;

    case 6:
      Serial.println("Moving: B");
      Cube.executeMove("B", false, alignCheck);
      break;

    case 7:
      Serial.println("Moving: B'");
      Cube.executeMove("B'", false, alignCheck);
      break;

    case 8:
      Serial.println("Moving: L");
      Cube.executeMove("L", false, alignCheck);
      break;

    case 9:
      Serial.println("Moving: L'");
      Cube.executeMove("L'", false, alignCheck);
      break;

    case 10:
      Serial.println("Moving: R");
      Cube.executeMove("R", false, alignCheck);
      break;

    case 11:
      Serial.println("Moving: R'");
      Cube.executeMove("R'", false, alignCheck);
      break;

    case 12:
      Serial.println("Moving: U");
      Cube.executeMove("U", false, alignCheck);
      break;

    case 13:
      Serial.println("Moving: U'");
      Cube.executeMove("U'", false, alignCheck);
      break;

    case 14:
      Serial.println("Moving: D");
      Cube.executeMove("D", false, alignCheck);
      break;

    case 15:
      Serial.println("Moving: D'");
      Cube.executeMove("D'", false, alignCheck);
      break;

    case 16:
      Serial.println("Moving: ROTX");
      Cube.executeMove("ROTX", false, alignCheck);
      break;

    case 17:
      Serial.println("Moving: ROTZ");
      Cube.executeMove("ROTZ", false, alignCheck);
      break;

    case 18:
      Serial.println("\n=== JAM SIMULATION TEST ===");
      testJamRecovery();
      break;

    default:
      Serial.println("Unknown Error");
      return;
  }

  printMenu();
}

void testJamRecovery() {
  Serial.println("\n=== JAM SIMULATION TEST ===");
  Serial.println("This test will simulate a jam and test the recovery system.");

  Serial.println("\nStep 1: Reading starting encoder positions...");

  // Read and display starting positions
  for (int i = 0; i < 6; i++) {
    int pos = MotorEncoders[i]->scan();
    Serial.print("Motor ");
    Serial.print(i);
    Serial.print(" encoder: ");
    Serial.println(pos);
  }

  // Query user for jam position
  Serial.println("\nStep 2: JAM POSITION SETUP");
  Serial.println("Enter number of steps to move Front motor off alignment:");
  Serial.println("(Typical values: 10-80 steps, full turn = ~100 steps)");
  Serial.print("Steps: ");

  // Wait for user input
  while (!Serial.available()) {
    delay(50);
  }

  int jamSteps = Serial.parseInt();
  while (Serial.available()) { Serial.read(); }  // Flush serial buffer

  // Validate input
  if (jamSteps <= 0 || jamSteps > 100) {
    Serial.println("\nInvalid input. Using default of 30 steps.");
    jamSteps = 30;
  } else {
    Serial.println(jamSteps);
  }

  Serial.print("\nStep 3: Simulating jam by moving Front motor ");
  Serial.print(jamSteps);
  Serial.println(" steps off alignment...");

  // Enable motors and move Front motor off alignment
  cubeMotors.enableMotors();
  long pos[6];
  for (int i = 0; i < 6; i++) {
    pos[i] = cubeMotors.getPos(i);
  }
  pos[2] -= jamSteps;  // Move Front motor specified steps off alignment
  cubeMotors.moveTo(pos);
  cubeMotors.disableMotors();

  Serial.print("Front motor is now jammed ");
  Serial.print(jamSteps);
  Serial.println(" steps off alignment!");

  // Display encoder position in jammed state
  int jammedPos = MotorEncoders[2]->scan();
  Serial.print("Front motor jammed encoder position: ");
  Serial.println(jammedPos);

  delay(1000);

  Serial.println("\nStep 4: Attempting F' move (should trigger jam recovery)...");
  int result = Cube.executeMove("F", false, true);

  if (result == 0) {
    Serial.println("\nSUCCESS: Jam recovery worked!");
    Serial.println("The system detected the jam, backed out, re-homed, and completed the move.");
  } else {
    Serial.print("\nFAILURE: Move failed with error code ");
    Serial.println(result);
  }

  Serial.println("\nStep 5: Verifying final alignment...");
  bool aligned = Cube.checkAlignment();
  Serial.print("All motors aligned: ");
  Serial.println(aligned ? "YES" : "NO");

  // Display final encoder positions
  Serial.println("\nFinal encoder positions:");
  for (int i = 0; i < 6; i++) {
    int finalPos = MotorEncoders[i]->scan();
    Serial.print("Motor ");
    Serial.print(i);
    Serial.print(" encoder: ");
    Serial.println(finalPos);
  }

  Serial.println("\n=== JAM SIMULATION TEST COMPLETE ===\n");
}

void printMenu() {  // Prints out a menu of all available options
  Serial.println("\n1- Toggle Ring");
  Serial.println("2- Toggle Bottom Servo\t\t3- Toggle Top Servo");
  Serial.println("4- Front 90\t\t\t5- Front -90");
  Serial.println("6- Back 90\t\t\t7- Back -90");
  Serial.println("8- Left 90\t\t\t9- Left -90");
  Serial.println("10- Right 90\t\t\t11- Right -90");
  Serial.println("12- Up 90\t\t\t13- Up -90");
  Serial.println("14- Down 90\t\t\t15- Down -90");
  Serial.println("16- Rotate Cube(Horz)\t\t17- Rotate Cube (Vert)");
  Serial.println("18- Test Jam Recovery");
}