#include <CubeSystem.h>

CubeSystem Cube;
int scanResult = -1;

void setup() {
  Cube.begin();
  Serial.println("Starting Program...");

  Serial.println("Enter scrambled cube and press anything");
  while (!Serial.available()) { delay(50); }  // Wait for Serial
  while(Serial.available()) {Serial.read();}  // Flush serial buffer

  Serial.println("Starting Scan...");
  scanResult = Cube.scanCube();
  if(scanResult==0){
    Serial.println("Scan successful");
  }
  else {
    Serial.print("Scan failed - Error: ");
    Serial.println(scanResult);
  }  

}

void loop() {
  // put your main code here, to run repeatedly:

}
