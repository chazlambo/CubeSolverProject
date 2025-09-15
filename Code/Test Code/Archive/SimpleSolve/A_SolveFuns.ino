

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
