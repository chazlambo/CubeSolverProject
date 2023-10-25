#include <Arduino.h>
#include "Cube.h"
//#include <kociemba.h>

////Kociemba Variables
//elapsedMillis em;
//DMAMEM char buf479[479 * 1024]; // 479K in DMAMEM
//char  buf248[248 * 1024];       // 248K on in DTCM

// Cube Variables
Cube myCube;
const char sol;

void setup() {
  // Start Serial Communication
  Serial.begin(115200);
  Serial.println("Enter anything to begin:");
  while (!Serial.available()) {}
  while (Serial.available()) {
    Serial.read();
  }
  Serial.println("Starting...");

  //  // Kociemba Setup
  //  em = 0;
  //  kociemba::set_memory(buf479, buf248);   // removing this line slows the computation by a factor of 4 (but saves a lot of RAM...)
  //  Serial.printf("RAM buffer created in %d ms.\n", (int)em);
}

void loop() {
  myCube.resetCube();
  buildCubeFun();
  printUnorientedCubeArray();
  setOrientation();
  printColorCubeArray();

  while (true) {
    delay(100);
  };
}

int setOrientation() {
  bool orientBool = 0;
  while (!orientBool) {
    Serial.println("Enter Left Side in X form:");
    while (!Serial.available()) {}              // Wait for input
    char left = Serial.read();                  // Read Character
    while (Serial.available()) {
      Serial.read(); // Clear Buffer
    }
    Serial.print("Left Side Entered: ");
    Serial.println(left);

    Serial.println("Enter Back Side in X form:");
    while (!Serial.available()) {}              // Wait for input
    char back = Serial.read();                  // Read Character
    while (Serial.available()) {
      Serial.read(); // Clear Buffer
    }
    Serial.print("Side Entered: ");
    Serial.println(back);

    int err = myCube.setOrientation(left, back);
    if (err) {
      Serial.print("Set Orientation Error ");
      Serial.print(err);
      Serial.print(": ");
      switch (err) {
        case 1:
          Serial.println("Left color input does not correspond to a side.");
          break;

        case 2:
          Serial.println("Back color input does not correspond to a side.");
          break;

        case 3:
          Serial.println("Impossible orientation detected, no solution found.");
          break;

        case 4:
          Serial.println("Left color cannot be the same as back color");
          break;

        default:
          Serial.println("Unknown Error");
      }
    }
    else {
      orientBool = 1;
    }
  }

}

int buildCubeFun() {
  // Array Variables;
  char whiteFace[10];
  char blueFace[10];
  char redFace[10];
  char yellowFace[10];
  char greenFace[10];
  char orangeFace[10];

  // Left Variables
  char whiteLeft;
  char blueLeft;
  char redLeft;
  char yellowLeft;
  char greenLeft;
  char orangeLeft;

  // Input color arrays
  // bool cubeBuilt = 0;
  //  while (!cubeBuilt) {
  Serial.println("\nWhite");
  setSide('W', whiteFace, whiteLeft);

  Serial.println("\nBlue");
  setSide('B', blueFace, blueLeft);

  Serial.println("\nRed");
  setSide('R', redFace, redLeft);

  Serial.println("\nYellow");
  setSide('Y', yellowFace, yellowLeft);

  Serial.println("\nGreen");
  setSide('G', greenFace, greenLeft);

  Serial.println("\nOrange");
  setSide('O', orangeFace, orangeLeft);

  //    int buildErr = 99;
  //    buildErr = myCube.buildUnorientedCubeArray();
  //    Serial.println("What the fuck is this shit?:");
  //    Serial.println(buildErr);
  //    Serial.println(buildErr==0);
  //    Serial.println(buildErr>0);
  //    if(buildErr) {
  //      Serial.print("Cube Build Error ");
  //      Serial.print(buildErr);
  //      Serial.print(": ");
  //      switch (buildErr){
  //        case 1:
  //          Serial.println("Color status is unknown");
  //          printUnorientedCubeArray();
  //          break;
  //
  //        case 2:
  //          Serial.println("Invalid number of red squares");
  //          break;
  //
  //        case 3:
  //          Serial.println("Invalid number of orange squares");
  //          break;
  //
  //        case 4:
  //          Serial.println("Invalid number of yellow squares");
  //          break;
  //
  //        case 5:
  //          Serial.println("Invalid number of green squares");
  //          break;
  //
  //        case 6:
  //          Serial.println("Invalid number of blue squares");
  //          break;
  //
  //        case 7:
  //          Serial.println("Invalid number of white squares");
  //          break;
  //
  //        case 0:
  //          Serial.println("How did we get here???");
  //          cubeBuilt = 1;
  //          break;
  //
  //        default:
  //          Serial.println("Unknown Error Occured");
  //      }
  //    }
  //    else {
  //      cubeBuilt = 1;
  //    }
  //  }
}

int setSide(char color, char side[10], char &left) {
  bool faceSet = 0;
  while (!faceSet) {
    Serial.println("Enter Face in XXXXXXXXX form:");
    while (!Serial.available());
    Serial.readStringUntil('\n').toCharArray(side, 10);
    Serial.print("Face Entered: ");
    Serial.println(side);

    Serial.println("Enter left in X form:");
    while (!Serial.available()) {}              // Wait for input
    left = Serial.read();                  // Read Character
    while (Serial.available()) {
      Serial.read(); // Clear Buffer
    }
    Serial.print("Side Entered: ");
    Serial.println(left);

    int err = myCube.setColorArray(color, side, left);

    if (err) {
      Serial.print("Error Code ");
      Serial.print(err);
      Serial.print(": ");
      switch (err) {
        case 1:
          Serial.println("Middle square does not match color");
          break;

        case 2:
          Serial.println("Invalid character");
          break;

        case 3:
          Serial.println("Color input doesn't match a side (check uppercase)");
          break;

        case 4:
          Serial.println("Left side input was invalid.");
          break;

        case 5:
          Serial.println("Left side input was impossible.");
          break;

        case 6:
          Serial.println("Face entry was empty");
          break;

        default:
          Serial.println("Unknown Error");
      }
      Serial.println("Try Again...\n\n");
    }
    else {
      faceSet = 1;
      Serial.println("Face set successfully");
      Serial.print("Faces Set: ");
      Serial.print(myCube.setCount);
      Serial.print("\t\tColor Status: ");
      Serial.println(myCube.cubeColorStatus);
    }
  }
  return 0;
}


void printUnorientedCubeArray() {
  Serial.print("Unoriented Cube Array: [");
  for (int i = 0; i < 54; i++) {
    Serial.print(myCube.unorientedCubeArray[i]);
  }
  Serial.println("]\n");
}

void printColorCubeArray() {
  Serial.print("Final Cube Array: \n");
  for (int i = 0; i < 54; i++) {
    Serial.print(myCube.colorCubeArray[i]);
    if (((i + 1) % 9) == 0) {
      Serial.println(" ");
    }
  }
  Serial.println("\n");
}
