#include "Cube.h"
Cube myCube;

char leftCol;
char backCol;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  while (!Serial.available());
  Serial.println("Starting...");

//   Testing color setting functions
    printFace('R');
    myCube.setSolved();
    printFace('R');
    myCube.setColorArray('R', "ROROROROR");
    printFace('R');
    myCube.setSolved();
    for(int i = 0; i<9; i++) {
      myCube.setSquare('R', i, 'W');
      printFace('R');
    }

  // Testing Orientation Functions
  printOrientation();
  leftCol = 'R';
  backCol = 'G';
  if (myCube.setOrientation(leftCol, backCol)) {
    Serial.println("Set Orientation failed");
  }
  printOrientation();
  leftCol = 'B';
  backCol = 'W';
  if (myCube.setOrientation(leftCol, backCol)) {
    Serial.println("Set Orientation failed");
  }
  printOrientation();

  // Testing Cube Array
  myCube.setSolved();
  int errNum =  myCube.buildCubeArray();
  if(errNum){
    Serial.print("Build Cube Array Failed, Error #");
    Serial.println(errNum);
  }
  printCubeArray();

  myCube.resetCube();
  myCube.setSolved();
  // Move cube R B with Red forward, green up
  myCube.setColorArray('R', "WWWGRRGRR");
  myCube.setColorArray('O', "YYYOOBOOB");
  myCube.setColorArray('Y', "GRRYYYYYY");
  myCube.setColorArray('G', "OOOGGGGGG");
  myCube.setColorArray('B', "RBBRBBRBB");
  myCube.setColorArray('W', "OOBWWWWWW");
  leftCol = 'Y';
  backCol = 'O';
  myCube.setOrientation(leftCol, backCol);
  myCube.buildCubeArray();
  printFace('B');
  printFace('G');
  printOrientation();
  printCubeArray();
  
}

void loop() {
  // put your main code here, to run repeatedly:

}

void printFace(char color) {
  Serial.print("Face: ");
  Serial.print(color);
  Serial.print("\t(");

  switch (color) {
    case 'R':
      for (int i = 0; i < 9; i++) {
        Serial.print(myCube.redSide[i]);
        Serial.print(" ");
      }
      break;

    case 'O':
      for (int i = 0; i < 9; i++) {
        Serial.print(myCube.orangeSide[i]);
        Serial.print(" ");
      }
      break;

  }
  Serial.println(")");
}

void printOrientation() {
  Serial.print("Left: ");
  Serial.print(leftCol);
  Serial.print("\tBack: ");
  Serial.print(backCol);
  Serial.print("\t URFDLB: (");

  for (int i = 0; i < 6; i++) {
    Serial.print(myCube.orientation[i]);
  }

  Serial.println(")");
}

void printCubeArray() {
  Serial.print("Cube Array: [");
  for(int i = 0; i < 54; i++) {
    Serial.print(myCube.cubeArray[i]);
  }
  Serial.println("]");
}
