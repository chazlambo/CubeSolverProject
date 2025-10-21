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
  // Build Cube
  // Testing Scramble [L R U D F B L' R' U' D' F' B'] (ALL MOVES IN YBOWGR ORIENTATION)
  myCube.resetCube();
  myCube.setColorArray('Y', "YOYBYGYRY", 'G');
  myCube.setColorArray('B', "BYBBBBBWB", 'O');
  myCube.setColorArray('O', "OYOOOOOWO", 'G');
  myCube.setColorArray('W', "WRWBWGWOW", 'G');
  myCube.setColorArray('G', "GYGGGGGWG", 'R');
  myCube.setColorArray('R', "RYRRRRRWR", 'B');
  myCube.setOrientation('G', 'R');
  myCube.buildCubeArray();

  while (Serial.available()) {
    Serial.read();
  }
  Serial.println("Place cube inside then enter anything");
  while (!Serial.available()) {}
  while (Serial.available()) {
    Serial.read();
  }
  extendHolders();

  while (Serial.available()) {
    Serial.read();
  }
  Serial.println("Enter anything to begin solve");
  while (!Serial.available()) {}
  while (Serial.available()) {
    Serial.read();
  }

  if (solve()) {
    // Split the solution string into individual moves
    String moves[50]; // Assuming a maximum of 50 moves in the string
    int moveCount = splitString(sol, ' ', moves);

    // Process each move
    for (int i = 0; i < moveCount; i++) {
      executeMove(moves[i]);
    }

    rebuildFun();
  }

  retractHolders();
  Serial.print("Solution: ");
  Serial.println(sol);

  while (true) {}



}
