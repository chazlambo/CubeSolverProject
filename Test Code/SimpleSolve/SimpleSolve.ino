#include <Arduino.h>
#include "Cube.h"
#include <kociemba.h>

//Kociemba Variables
elapsedMillis em;
const char* sol;

Cube myCube;

void setup() {
  // Serial
  Serial.begin(115200);
  while (!Serial.available());
  while (Serial.available()) {
    Serial.read();
  }
  Serial.println("Starting...");

  enterCube();
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

}

void loop() {
  // put your main code here, to run repeatedly:

}
