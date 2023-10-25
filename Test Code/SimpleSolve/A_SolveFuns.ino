bool solve(){
  Serial.println("Solving:");
  sol = kociemba::solve(myCube.cubeArray);
  if (sol == nullptr) {
    Serial.println("no solution found");
    return 0;
  }
  else {
    Serial.printf("Solution [%s] found in %d ms.\n", sol, (int)em);
    return 1;
  }
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
