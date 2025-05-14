#include "C:\Users\charl\OneDrive\Documents\Projects\Cube Solver Project\Arduino Code\CubeSolverProject\libraries\CubeSolver\CubeSolver.cpp"

unsigned int t = 0;
unsigned int t_buffer = 50;
unsigned int t_old = 0;

int state = 0;
bool startPrintBool = 0;
bool endPrintBool = 0;

int useMotors = 1;

void setup() {
  skipMotorInt = !useMotors;

  mainSetup();

  if(useMotors) {
    topServoExtend();
    botServoExtend();
    ringMove(2);  // Extend ring motors
  }

  Serial.println("Starting Calibration...");
  delay(100);

  Serial.println("Checking if calibrated...");
  delay(100);

  if(isMotorCalibrated()) {
    getMotorCalibration();
    Serial.println("Motors are calibrated to values:");
    
    for(int i=0; i < numMotors; i++){
      Serial.print(motorCals[i]);
      Serial.print("\t");
    }
    Serial.println();
  }
  else{
    Serial.println("Motors are not calibrated");
  }

  delay(100);
  state = 1;
}

void loop() {
  t = millis();
  
  switch(state) {
    case 1: // Waiting to begin
      // Only print statement once
      if(!startPrintBool) {
        Serial.println("\n Please enter anything to begin calibration:");
        startPrintBool = 1;
      }
      
      // If serial is available, move on.
      if(Serial.available()>0) {
        while(Serial.available()){Serial.read();} // Flush buffer

        startPrintBool = 0;
        state = 2;

        Serial.println("\n Beginning calibration shortly, enter anything to set values...");
      }

      break;

    case 2: // Printing calibration values
      if (t - t_old > t_buffer) {
        updateMotorVals();
        
        for(int i=0; i < numMotors; i++) {
          Serial.print(motorVals[i]);
          Serial.print("\t");
        }
        Serial.println();

        t_old = t;
      }

      if (Serial.available()>0) {
        while(Serial.available()){Serial.read();} // Flush buffer

        Serial.println("Setting current position to calibrated values...");
        if(setMotorCalibration()) {
          getMotorCalibration();
          Serial.println("Values set successfully.");
          state = 3;
        }
        else {
          Serial.println("Failed to set calibrated values, potentially invalid range.");
          delay(1000);
        }
      }

      break;

    case 3: // Calibration complete
      // Only print statement once
      if(!endPrintBool) {
        Serial.println("\n Calibration Complete.");

        if(useMotors) {
          topServoRetract();
          botServoRetract();
          ringMove(0);  // Extend ring motors
        }

        endPrintBool = 1;
      }
      break;

    default:
      Serial.print("Invalid State???");
      delay(2000);
      state = 1;
      break;
  }

}
