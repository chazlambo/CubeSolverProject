#include <CubeSystem.h>

CubeSystem Cube;

// Motor names for display
const char* motorNames[] = {"Up", "Right", "Front", "Down", "Left", "Back", "Ring"};

// Map characters to motor indices
int motorFromChar(char c) {
    switch (tolower(c)) {
        case 'u': return 0;
        case 'r': return 1;
        case 'f': return 2;
        case 'd': return 3;
        case 'l': return 4;
        case 'b': return 5;
        case 'o': return 6;  // 'o' for Ring
        default:  return -1; // invalid
    }
}

// State tracking
int selectedMotor = -1;   // -1 means waiting for input
bool printing = false;

void setup() {
    Cube.begin();
    Serial.begin(115200);
    Serial.println("Cube system initialized.");
    Serial.println("Enter motor letter (u=Up, r=Right, f=Front, d=Down, l=Left, b=Back, o=Ring):");
}

void loop() {
    // Check if serial input is available
    if (Serial.available() > 0) {
        String input = Serial.readStringUntil('\n');
        input.trim();

        // If already printing, any message stops printing
        if (printing) {
            selectedMotor = -1;
            printing = false;
            Serial.println("Stopped printing. Waiting for next motor letter...");
            while (Serial.available() > 0) Serial.read(); // clear buffer
            return;
        }

        // Otherwise, interpret as motor selection
        if (input.length() > 0) {
            int motorIdx = motorFromChar(input.charAt(0));
            if (motorIdx >= 0) {
                selectedMotor = motorIdx;
                printing = true;
                Serial.print("Now printing ");
                Serial.print(motorNames[selectedMotor]);
                Serial.println(" motor encoder values...");
            } else {
                Serial.println("Invalid input. Enter one of: u r f d l b o");
            }
        }

        while (Serial.available() > 0) Serial.read(); // clear buffer
    }

    // If in printing mode, print encoder
    if (printing && selectedMotor >= 0) {
        int val = MotorEncoders[selectedMotor]->scan();

        if (val >= 0) {
            Serial.print(motorNames[selectedMotor]);
            Serial.print(" Motor Encoder Value: ");
            Serial.println(val);
        } else {
            Serial.print("Error reading ");
            Serial.print(motorNames[selectedMotor]);
            Serial.print(" motor encoder, code: ");
            Serial.println(val);
        }

        delay(200);
    }
}
