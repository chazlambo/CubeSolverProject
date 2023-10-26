int setOrientation() {
  while (Serial.available()) {
    Serial.read();
  }
  Serial.println("Enter Left Character:");
  while (!Serial.available()) {}              // Wait for input
  char left = Serial.read();                  // Read Character
  while (Serial.available()) {
    Serial.read(); // Clear Buffer
  }
  Serial.print("Left Side Entered: ");
  Serial.println(left);

  Serial.println("Enter Back Character:");
  while (!Serial.available()) {}              // Wait for input
  char back = Serial.read();                  // Read Character
  while (Serial.available()) {
    Serial.read(); // Clear Buffer
  }
  Serial.print("Side Entered: ");
  Serial.println(back);

  Serial.print("Orientation: ");
  Serial.print(myCube.setOrientation(left, back));
  Serial.print(" - ");
  Serial.println(myCube.orientation);
}

void setSide(char color, char side[9], char &left) {
  bool faceSet = 0;
  while (!faceSet) {
    Serial.println("Enter Face (9 characters):");
    while (!Serial.available());
    for (int i = 0; i < 9; i++) {
      side[i] = Serial.read();
    }
    while (Serial.available() > 0) {
      Serial.read();
    }
    Serial.print("Face Entered: ");
    Serial.println(side);

    Serial.println("Enter left character:");
    while (!Serial.available()) {}          // Wait for input
    left = Serial.read();                   // Read Character
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
}

void printColorCubeArray() {
  Serial.print("Color Cube Array: \n");
  for (int i = 0; i < 54; i++) {
    Serial.print(myCube.colorCubeArray[i]);
    if (((i + 1) % 9) == 0) {
      Serial.println(" ");
    }
  }
  Serial.println("\n");
}

void rebuildFun() {
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
