#include <CubeSystem.h>

CubeSystem Cube;
int scanResult = -1;
int solveResult = -1;
int execResult  = -1;

void waitForAnyKey(const char* msg) {
  Serial.println(msg);
  while (!Serial.available()) { delay(25); }
  while (Serial.available()) { Serial.read(); }
}

void setup() {
  Cube.begin();  // homes motors if calibrated, inits servos, ring, sensors
  while (!Serial) {
    ; // Do nothing, just wait
  }
  Serial.println("Starting Program...");

  // ---- Scan phase ----
  waitForAnyKey("Insert scrambled cube then press any key to SCAN.");
  Serial.println("Starting Scan...");
  scanResult = Cube.scanCube();

  if (scanResult != 0) {
    Serial.print("Scan failed - Error: ");
    Serial.println(scanResult);
    return;
  }
  Serial.println("Scan successful.");

  // ---- Virtual solve ----
  Serial.println("Computing virtual solution...");
  solveResult = Cube.solveVirtual();   // fills internal move list
  if (solveResult != 0) {
    Serial.print("Virtual solve failed - Error: ");
    Serial.println(solveResult);
    return;
  }
  Serial.println("Virtual solution ready.");

   // ---- Load Cube ----
  waitForAnyKey("Press any key to load cube.");

  // Extend/grip sequence (adjust if your mechanism prefers a different order)
  Cube.botServoExtend();   // bottom support out
  delay(100);
  Cube.ringExtend();       // ring out to contact
  Cube.topServoExtend();   // top clamp down
  delay(100);
  

   // ---- Solve Cube ----
  waitForAnyKey("Ready to solve. Press any key to start.");

  // ---- Physical execute ----
  execResult = Cube.executeSolve();    // turns motors + virtual, with alignment checks
  if (execResult != 0) {
    Serial.print("Execute solve failed - Error: ");
    Serial.println(execResult);
    return;
  }

  Serial.println("Solve complete!");

  Cube.ringRetract();
  Cube.topServoRetract();
  delay(100);
  Cube.botServoRetract();
  delay(100);
}

void loop() {
  // no repeat
}
