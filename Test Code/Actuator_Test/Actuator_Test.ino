#include "C:\Users\charl\OneDrive\Documents\Projects\Cube Solver Project\Arduino Code\CubeSolverProject\libraries\CubeSolver\CubeSolver.cpp"

void setup() {
  mainSetup();

  Serial.println("Starting Program...");
  //printMenu();

  Serial.print("\nMotors Calibrated: ");
  Serial.println(getMotorCalibration());
  Serial.print("Home Motors: ");
  Serial.println(homeMotors());
}

void loop() {
  while (!Serial.available()) { delay(50); }  // Wait for Serial
  int input = Serial.parseInt();              // Parse input as integer
  while(Serial.available()) {Serial.read();}  // Flush serial buffer

  if ((input <= 0) || (input > 17)) {  // Check for valid input
    Serial.println("Invalid input...");
    printMenu();  // Print menu
    return;
  }

  switch (input) {
    case 1:
      Serial.println("Toggling Ring");
      ringToggle();
      break;

    case 2:
      Serial.println("Toggling Bottom Servo");
      botServo.toggle();
      break;

    case 3:
      Serial.println("Toggling Top Servo");
      topServo.toggle();
      break;

    case 4:
      Serial.println("Moving: F");
      executeMove("F");
      break;

    case 5:
      Serial.println("Moving F'");
      executeMove("F'");
      break;

    case 6:
      Serial.println("Moving: B");
      executeMove("B");
      break;

    case 7:
      Serial.println("Moving: B'");
      executeMove("B'");
      break;

    case 8:
      Serial.println("Moving: L");
      executeMove("L");
      break;

    case 9:
      Serial.println("Moving: L'");
      executeMove("L'");
      break;

    case 10:
      Serial.println("Moving: R");
      executeMove("R");
      break;

    case 11:
      Serial.println("Moving: R'");
      executeMove("R'");
      break;

    case 12:
      Serial.println("Moving: U");
      executeMove("U");
      break;

    case 13:
      Serial.println("Moving: U'");
      executeMove("U'");
      break;

    case 14:
      Serial.println("Moving: D");
      executeMove("D");
      break;

    case 15:
      Serial.println("Moving: D'");
      executeMove("D'");
      break;

    case 16:
      Serial.println("Moving: ROT1");
      executeMove("ROT1");
      break;

    case 17:
      Serial.println("Moving: ROT2");
      executeMove("ROT2");
      break;

    default:
      Serial.println("Unknown Error");
      return;
  }
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
  Serial.println("16 - Rotate Cube(Horz)\t\t\t17- Rotate Cube (Vert)");
}
