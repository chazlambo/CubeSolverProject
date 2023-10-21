#include "Cube.h"
Cube myCube;

char leftCol;
char backCol;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  while (!Serial.available());
  Serial.println("Starting...");

// Testing Cube Array
  myCube.setSolved(); // Set to solved
  int errNum =  myCube.buildUnorientedCubeArray();
  if(errNum){
    Serial.print("Build Unoriented Cube Array Failed, Error #");
    Serial.println(errNum);
  }
  Serial.println("Unoriented Solved Cube Array:");
  printUnorientedCubeArray();

  // Test with 2 moves (R, B) inputted in default orientation
  myCube.resetCube();
  myCube.setColorArray('W', "BBBWWRWWR", 'G');
  myCube.setColorArray('B', "BBOBBYBBY", 'R');
  myCube.setColorArray('R', "RRYRRYRRY", 'G');
  myCube.setColorArray('Y', "YYOYYOGGG", 'G');
  myCube.setColorArray('G', "RGGWGGWGG", 'O');
  myCube.setColorArray('O', "WWWOOOOOO", 'B');
  errNum = myCube.buildUnorientedCubeArray();
  if(errNum){
    Serial.print("Build Unoriented Cube Array Failed, Error #");
    Serial.println(errNum);
  }
  Serial.println("Unoriented (R, B) Cube inputted in correct orientation:");
  printUnorientedCubeArray();

  // Test with 2 moves (R, B) inputted in random orientation
  myCube.resetCube();
  myCube.setColorArray('W', "BRRBWWBWW", 'O');
  myCube.setColorArray('B', "YBBYBBOBB", 'O');
  myCube.setColorArray('R', "RRRRRRYYY", 'Y');
  myCube.setColorArray('Y', "GYYGYYGOO", 'O');
  myCube.setColorArray('G', "GGGGGGRWW", 'W');
  myCube.setColorArray('O', "WOOWOOWOO", 'W');
  errNum = myCube.buildUnorientedCubeArray();
  if(errNum){
    Serial.print("Build Unoriented Cube Array Failed, Error #");
    Serial.println(errNum);
  }
  Serial.println("Unoriented (R, B) Cube inputted in random orientation:");
  printUnorientedCubeArray();

  // Test Building with Orientation
  myCube.resetCube();
  myCube.setSolved();
  myCube.buildUnorientedCubeArray();
  printUnorientedCubeArray();
  myCube.setOrientation('G', 'Y');
  myCube.buildCubeArray();
  Serial.println("Oriented Solved Cube rotated once around X axis:");
  printCubeArray();

  // Building orientation with 2 moves (in default orientation) (R, B) inputted in random orientation
  myCube.resetCube();
  myCube.setColorArray('W', "BRRBWWBWW", 'O');
  myCube.setColorArray('B', "YBBYBBOBB", 'O');
  myCube.setColorArray('R', "RRRRRRYYY", 'Y');
  myCube.setColorArray('Y', "GYYGYYGOO", 'O');
  myCube.setColorArray('G', "GGGGGGRWW", 'W');
  myCube.setColorArray('O', "WOOWOOWOO", 'W');
  myCube.buildUnorientedCubeArray();
  myCube.setOrientation('G', 'Y');
  myCube.buildCubeArray();
  Serial.println("Oriented (90, 0, 0) (R, B) cube inputted at random orientation:");
  printColorCubeArray();

  // Building orientation with 2 moves (in default orientation) (R, B) inputted in random orientation
  myCube.setOrientation('G', 'W');
  myCube.buildCubeArray();
  Serial.println("Oriented (270, 0, 0) (R, B):");
  printColorCubeArray();

  // Building orientation with 2 moves (in default orientation) (R, B) inputted in random orientation
  myCube.setOrientation('W', 'O');
  Serial.println(myCube.buildCubeArray());
  Serial.println("Oriented (0, 90, 0) (R, B):");
  printColorCubeArray();

  // Building orientation with 2 moves (in default orientation) (R, B) inputted in random orientation
  myCube.setOrientation('B', 'Y');
  Serial.println(myCube.buildCubeArray());
  Serial.println("Oriented (90, 180, 0) (R, B):");
  printColorCubeArray();
  printCubeArray();

  // Building orientation with 2 moves (in default orientation) (R, B) inputted in random orientation
  myCube.setOrientation('O', 'B');
  Serial.println(myCube.buildCubeArray());
  Serial.println("Oriented (0, 0, 90) (R, B):");
  printColorCubeArray();
  printCubeArray();

  // Building orientation with 2 moves (in default orientation) (R, B) inputted in random orientation
  myCube.setOrientation('O', 'B');
  Serial.println(myCube.buildCubeArray());
  Serial.println("Oriented (0, 0, 90) (R, B):");
  printColorCubeArray();
  printCubeArray();


  // Building orientation with 2 moves (in default orientation) (R, B) inputted in random orientation
  myCube.setOrientation('W', 'B');
  Serial.println(myCube.buildCubeArray());
  Serial.println("Oriented (90, 180, 90) (R, B):");
  printColorCubeArray();
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

void printUnorientedCubeArray() {
  Serial.print("Unoriented Cube Array: [");
  for(int i = 0; i < 54; i++) {
    Serial.print(myCube.unorientedCubeArray[i]);
  }
  Serial.println("]\n");
}

void printColorCubeArray() {
  Serial.print("Final Cube Array: \n");
  for(int i = 0; i < 54; i++) {
    Serial.print(myCube.colorCubeArray[i]);
    if (((i+1)%9)==0) {
      Serial.println(" ");
    }
  }
  Serial.println("\n");
}

void printCubeArray() {
  Serial.print("Final Cube Array: \n");
  for(int i = 0; i < 54; i++) {
    Serial.print(myCube.cubeArray[i]);
    if (((i+1)%9)==0) {
      Serial.println(" ");
    }
  }
  Serial.println("\n");
}
