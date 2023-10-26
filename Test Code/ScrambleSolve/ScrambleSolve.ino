#include <Arduino.h>
#include "Cube.h"
#include <kociemba.h>
#include <Servo.h>
#include <AccelStepper.h>
#include <MultiStepper.h>

Cube myCube;

//Kociemba Variables
elapsedMillis em;
const char* sol;

void setup() {
  // Setup Function
  setupFun();
}

void loop() {
  myCube.resetCube();
  myCube.setSolved();
  myCube.setOrientation('G', 'O');
  myCube.buildCubeArray();
  while (Serial.available()) {Serial.read();}
  Serial.println("Place solved cube inside then enter anything");
  while (!Serial.available()) {}
  while (Serial.available()) {
    Serial.read();
  }

  extendHolders();
  long randMove;
  while (Serial.available()) {Serial.read();}
  Serial.println("Enter anything to start scramble");
  while (!Serial.available()) {}
  while (Serial.available()) {
    Serial.read();
  }

  while (!Serial.available()) {
    randMove = random(0, 12);
    switch (randMove) {
      case 0:
        executeMove("U");
        break;
      case 1:
        executeMove("U'");
        break;
      case 2:
        executeMove("R");
        break;
      case 3:
        executeMove("R'");
        break;
      case 4:
        executeMove("F");
        break;
      case 5:
        executeMove("F'");
        break;
      case 6:
        executeMove("D");
        break;
      case 7:
        executeMove("D'");
        break;
      case 8:
        executeMove("L");
        break;
      case 9:
        executeMove("L'");
        break;
      case 10:
        executeMove("B");
        break;
      case 11:
        executeMove("B'");
        break;
      default:
        break;
    }
  }

  while (Serial.available()) {Serial.read();}
  Serial.println("Enter anything to solve cube");
  while (!Serial.available()) {}
  while (Serial.available()) {
    Serial.read();
  }

  if (solve()) {
    // Split the solution string into individual moves
    String moves[50]; // Assuming a maximum of 50 moves in the string
    int moveCount = splitString(sol, ' ', moves);

    // Process each move
    em = 0;
    for (int i = 0; i < moveCount; i++) {
      executeMove(moves[i]);
    }
    Serial.print("Solved in: ");
    Serial.print(em);
    Serial.println("ms");
  }
  retractHolders();
  while (Serial.available()) {Serial.read();}
  Serial.println("Enter anything to restart scramble solver");
  while (!Serial.available()) {}
  while (Serial.available()) {
    Serial.read();
  }

}
