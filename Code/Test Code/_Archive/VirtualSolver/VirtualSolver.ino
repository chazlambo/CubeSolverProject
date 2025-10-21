#include <Arduino.h>
#include "Cube.h"
#include <kociemba.h>

Cube myCube;

//Kociemba Variables
elapsedMillis em;



void setup() {
  // Serial
  Serial.begin(115200);
  while (!Serial.available());
  Serial.println("Starting...");

  // Kociemba Setup
  em = 0;

  // Testing Scramble [L R U D F B L' R' U' D' F' B'] (ALL MOVES IN YBOWGR ORIENTATION)
  myCube.resetCube();
  myCube.setColorArray('Y', "YOYBYGYRY", 'G');
  myCube.setColorArray('B', "BYBBBBBWB", 'O');
  myCube.setColorArray('O', "OYOOOOOWO", 'G');
  myCube.setColorArray('W', "WRWBWGWOW", 'G');
  myCube.setColorArray('G', "GYGGGGGWG", 'R');
  myCube.setColorArray('R', "RYRRRRRWR", 'B');
  myCube.setOrientation('G', 'R');

  Serial.print("Random Scramble (Oriented: YBOWGR): ");
  Serial.println(myCube.buildCubeArray());
    for (int i = 0; i < 54; i++) {
    Serial.print(myCube.colorCubeArray[i]);
    if (((i + 1) % 9) == 0) {
      Serial.println(" ");
    }
  }

  Serial.println("Solving:");
  const char* sol = kociemba::solve(myCube.cubeArray);
  if (sol == nullptr) {
    Serial.println("no solution found");
  }
  else {
    Serial.printf("Solution [%s] found in %d ms.\n", sol, (int)em);
  }

  // Split the solution string into individual moves
  String moves[50]; // Assuming a maximum of 50 moves in the string
  int moveCount = splitString(sol, ' ', moves);

  // Process each move
  for (int i = 0; i < moveCount; i++) {
    executeMove(moves[i]);
  }

  Serial.println("Rotation completed:");
  Serial.print("Rebuilding Cube...");
  Serial.println(myCube.rebuildFromCubeArray());
  Serial.println("Rebuild complete, printing...");
  Serial.println("\nColor Cube Array:");
  for (int i = 0; i < 54; i++) {
    Serial.print(myCube.colorCubeArray[i]);
    if (((i + 1) % 9) == 0) {
      Serial.println(" ");
    }
  }
  Serial.println("\n");

}

void loop() {
  // put your main code here, to run repeatedly:

}

// Function to split a string into an array of substrings
int splitString(String input, char delimiter, String output[]) {
  int tokenIndex = 0;
  int tokenCount = 0;
  while (tokenIndex >= 0) {
    tokenIndex = input.indexOf(delimiter);
    if (tokenIndex >= 0) {
      output[tokenCount] = input.substring(0, tokenIndex);
      input = input.substring(tokenIndex + 1);
      tokenCount++;
    }
  }
  if (input.length() > 0) {
    output[tokenCount] = input;
    tokenCount++;
  }
  return tokenCount;
}

// Function to execute a single move and print the move
void executeMove(String move) {
  Serial.print("Moving: ");
  Serial.println(move);

  if (move == "U") {
    // Call the function for U move
    myCube.rotU(1);
  } else if (move == "U'") {
    // Call the function for U' move
    myCube.rotU(-1);
  } else if (move == "U2") {
    // Call the function for U2 move
    myCube.rotU(2);
  } else if (move == "R") {
    // Call the function for R move
    myCube.rotR(1);
  } else if (move == "R'") {
    // Call the function for R' move
    myCube.rotR(-1);
  } else if (move == "R2") {
    // Call the function for R2 move
    myCube.rotR(2);
  } else if (move == "F") {
    // Call the function for F move
    myCube.rotF(1);
  } else if (move == "F'") {
    // Call the function for F' move
    myCube.rotF(-1);
  } else if (move == "F2") {
    // Call the function for F2 move
    myCube.rotF(2);
  } else if (move == "D") {
    // Call the function for D move
    myCube.rotD(1);
  } else if (move == "D'") {
    // Call the function for D' move
    myCube.rotD(-1);
  } else if (move == "D2") {
    // Call the function for D2 move
    myCube.rotD(2);
  } else if (move == "L") {
    // Call the function for L move
    myCube.rotL(1);
  } else if (move == "L'") {
    // Call the function for L' move
    myCube.rotL(-1);
  } else if (move == "L2") {
    // Call the function for L2 move
    myCube.rotL(2);
  } else if (move == "B") {
    // Call the function for B move
    myCube.rotB(1);
  } else if (move == "B'") {
    // Call the function for B' move
    myCube.rotB(-1);
  } else if (move == "B2") {
    // Call the function for B2 move
    myCube.rotB(2);
    // You can add more 'if' statements for other moves as needed
  }
}
