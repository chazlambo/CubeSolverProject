// Function to insert cube into machine
void extendHolders() {
  botServoExtend();
  delay(1000);
  ringExtend();
  topServoExtend();
  delay(1000);
}

void retractHolders() {
  topServoRetract();
  delay(1000);
  ringRetract();
  botServoRetract();
  delay(1000);
}

// Function to execute a single move and print the move
void executeMove(String move) {
  Serial.print("Moving: ");
  Serial.println(move);

  if (move == "U") {
    // Call the function for U move
    pos[0] += turnStep;
    myCube.rotU(1);
    
  } else if (move == "U'") {
    // Call the function for U' move
    pos[0] -= turnStep;
    myCube.rotU(-1);
    
  } else if (move == "U2") {
    // Call the function for U2 move
    pos[0] += turnStep*2;
    myCube.rotU(2);
    
  } else if (move == "R") {
    // Call the function for R move
    pos[1] += turnStep;
    myCube.rotR(1);
    
  } else if (move == "R'") {
    // Call the function for R' move
    pos[1] -= turnStep;
    myCube.rotR(-1);
    
  } else if (move == "R2") {
    // Call the function for R2 move
    pos[1] += turnStep*2;
    myCube.rotR(2);
    
  } else if (move == "F") {
    // Call the function for F move
    pos[2] += turnStep;
    myCube.rotF(1);
    
  } else if (move == "F'") {
    // Call the function for F' move
    pos[2] -= turnStep;
    myCube.rotF(-1);
    
  } else if (move == "F2") {
    // Call the function for F2 move
    pos[2] += turnStep*2;
    myCube.rotF(2);
    
  } else if (move == "D") {
    // Call the function for D move
    pos[3] += turnStep;
    myCube.rotD(1);
    
  } else if (move == "D'") {
    // Call the function for D' move
    pos[3] -= turnStep;
    myCube.rotD(-1);
    
  } else if (move == "D2") {
    // Call the function for D2 move
    pos[3] += turnStep*2;
    myCube.rotD(2);
    
  } else if (move == "L") {
    // Call the function for L move
    pos[4] += turnStep;
    myCube.rotL(1);
    
  } else if (move == "L'") {
    // Call the function for L' move
    pos[4] -= turnStep;
    myCube.rotL(-1);
    
  } else if (move == "L2") {
    // Call the function for L2 move
    pos[4] += turnStep*2;
    myCube.rotL(2);
    
  } else if (move == "B") {
    // Call the function for B move
    pos[5] += turnStep;
    myCube.rotB(1);
    
  } else if (move == "B'") {
    // Call the function for B' move
    pos[5] -= turnStep;
    myCube.rotB(-1);
    
  } else if (move == "B2") {
    // Call the function for B2 move
    pos[5] += turnStep*2;
    myCube.rotB(2);
  }

  // Move Steppers to position
  digitalWrite(ENPIN, LOW);
  multiStep.moveTo(pos);
  multiStep.runSpeedToPosition();
  digitalWrite(ENPIN, HIGH);
  
}
