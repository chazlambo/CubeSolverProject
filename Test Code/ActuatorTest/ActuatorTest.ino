// Definitions and Inclusions
#include <AccelStepper.h>
#include <Servo.h>

void setup() {
  // Call Setup Function
  setupFun();
  
  printMenu();                            // Print menu
}

void loop() {
  while(!Serial.available()){delay(50);}  // Wait for Serial
  int input = Serial.parseInt();          // Parse input as integer
  
  if ((input <= 0)||(input>15)) {         // Check for valid input
    Serial.println("Invalid input...");
    printMenu();                          // Print menu
    return;
  }

  switch(input) {
    case 1:
      Serial.println("Toggling Ring");
      ringToggle();
      break;
      
    case 2:
      Serial.println("Toggling Bottom Servo");
      botServoToggle();
      break;
      
    case 3:
      Serial.println("Toggling Top Servo");
      topServoToggle();
      break;

    case 4:
      Serial.println("Moving: F");
      F90(1);
      break;

    case 5:
      Serial.println("Moving F'");
      F90(0);
      break;

    case 6:
      Serial.println("Moving: B");
      B90(1);
      break;

    case 7:
      Serial.println("Moving: B'");
      B90(0);
      break;

    case 8:
      Serial.println("Moving: L");
      L90(1);
      break;

    case 9:
      Serial.println("Moving: L'");
      L90(0);
      break;

    case 10:
      Serial.println("Moving: R");
      R90(1);
      break;

    case 11:
      Serial.println("Moving: R'");
      R90(0);
      break;

    case 12: 
      Serial.println("Moving: U");
      U90(1);
      break;

    case 13:
      Serial.println("Moving: U'");
      U90(0);
      break;

    case 14:
      Serial.println("Moving: D");
      D90(1);
      break;

    case 15:
      Serial.println("Moving: D'");
      D90(0);
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
}
