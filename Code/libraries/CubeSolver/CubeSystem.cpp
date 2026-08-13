// CubeSystem.cpp
#include "CubeSystem.h"

CubeSystem::CubeSystem() {}

CubeSystem* CubeSystem::s_pumpOwner = nullptr;

bool CubeSystem::pumpTrampoline() {
    if (s_pumpOwner == nullptr) return true;
    return s_pumpOwner->pumpTick();
}

bool CubeSystem::pumpTick() {
    // Keep LVGL ticking. This is the whole point — without it the display is
    // frozen for the entire scan and the entire solve.
    displayUpdate();

    // While safeStop() is releasing the cube, keep refreshing the display but
    // do not detect aborts — the button that triggered this abort is still down.
    if (pumpAbortSuppressed) {
        selectHeldSince = 0;
        return true;
    }

    // Poll input, but not on every 5 ms pump: each selectPressed() is a full
    // I2C transaction to the seesaw on Wire1, and doing that 200x/second would
    // saturate the bus for no benefit. 25 ms is far faster than a human.
    unsigned long now = millis();
    if (now - lastInputPoll >= 25) {
        // If the last poll was long ago we were inside an unpumped region (a
        // stepper move, a homing pass) and the button state in between is
        // unknown. Restart the hold measurement rather than treating two brief
        // taps that bracket a move as one continuous hold — that produced
        // spurious aborts.
        if (now - lastInputPoll > 200) {
            selectHeldSince = 0;
        }
        lastInputPoll = now;

        bool held = encoderInitialized && menuEncoder.selectPressed();

        // After clearAbort(), require a release before a hold can count again.
        // Otherwise the SELECT press that STARTED this operation is still down,
        // the timer restarts, and an abort fires ~1 s into the thing the user
        // just asked for.
        //
        // Single exit deliberately: an earlier version returned early from this
        // branch, which made (selectMustRelease && abortRequested) a sticky cell
        // that reported "stop" from a branch whose premise is "no abort detected"
        // and could never clear while the button was held.
        if (selectMustRelease) {
            if (!held) selectMustRelease = false;
            selectHeldSince = 0;
        } else if (held) {
            if (selectHeldSince == 0) {
                selectHeldSince = now;
            } else if (now - selectHeldSince >= kAbortHoldMs) {
                if (!abortRequested) {
                    Serial.println(F("ABORT requested (SELECT held)"));
                }
                abortRequested = true;
            }
        } else {
            selectHeldSince = 0;
        }
    }

    return !abortRequested;
}

void CubeSystem::begin() {    
    // Serial Communication Setup
    Serial.begin(baudRate);

    // Power Setup
    // pinMode(POWPIN, INPUT);
    // Serial.println("Waiting for power.");
    // bool powerCheck = 0;

    // while (!powerCheck){
    //     powerCheck = digitalRead(POWPIN);
    //     delay(20);
    // }
    
    // I2C Setup for color sensors and motor encoders
    // Wire (I2C0) uses pins: 18 (SDA0), 19 (SCL0)
    Wire.begin();
    Wire.setClock(100000);  // 100kHz for stability
    Serial.println("Wire (I2C0) initialized on pins 18/19 at 100kHz");

    // Release the encoder mux from reset BEFORE anything else touches Wire.
    // This used to run after cubeMotors.begin(), both servo sweeps and both
    // colour-sensor probes — i.e. the /RESET of an active bus participant sat
    // floating during ~5 s of traffic on the very bus it shares.
    initEncoderMuxReset();

    // Display Setup
    displayInitialized = cubeDisplay.begin(10000000);

    // Register the cooperative pump IMMEDIATELY after the display comes up.
    // topServo.begin()/botServo.begin() below are full servo sweeps and
    // homeMotors() drives all six steppers; registering after them left the
    // display frozen for ~5-10 s right after "display initialized".
    // encoderInitialized is still false here, so pumpTick() refreshes the
    // display but does not poll input until the seesaw is up.
    s_pumpOwner = this;
    systemPump  = &CubeSystem::pumpTrampoline;
    clearAbort();


    // Motor Setup
    cubeMotors.begin();

    // Servo Setup
    topServo.begin();
    botServo.begin();

    // Color Sensors Setup
    // begin() builds a diagnostic code (0 ok / 1X mux not found / 2X mux begin
    // failed / 3X sensor X failed) and both return values used to be discarded,
    // so a board with a dead mux booted silently and produced garbage colours.
    colorSensorsOk = true;
    int cs1 = colorSensor1.begin();
    int cs2 = colorSensor2.begin();
    if (cs1 != 0) {
        Serial.print(F("ERROR: colorSensor1.begin() failed, code ")); Serial.println(cs1);
        colorSensorsOk = false;
    }
    if (cs2 != 0) {
        Serial.print(F("ERROR: colorSensor2.begin() failed, code ")); Serial.println(cs2);
        colorSensorsOk = false;
    }

    // Motor Encoder Setup (ENC_MUX_RST was already driven, right after Wire.begin)
    if (!encoderMux.begin()) {           // Begins I2C Mux for encoders
        Serial.println(F("WARNING: encoder mux did not respond - attempting reset"));
        resetEncoderMux();
        if (!encoderMux.begin()) {
            Serial.println(F("ERROR: encoder mux unavailable. Motion will fault."));
        }
    }

    for (int i = 0; i < 7; ++i) {
        int rc = MotorEncoders[i]->begin();
        if (rc != 0) {
            Serial.print("MotorEncoder "); Serial.print(i);
            Serial.print(" begin() failed rc="); Serial.println(rc);
        }
    }

    // Begin Rotary Encoder on Wire1 (separate I2C bus)
    // The return value matters: with a dead seesaw, selectPressed() reads
    // undefined data and displayWaitForSelect()'s Serial/encoder fallback would
    // either hang or advance on noise.
    encoderInitialized = menuEncoder.begin();
    if (!encoderInitialized) {
        Serial.println(F("WARNING: menu encoder (seesaw) not found on Wire1."));
        Serial.println(F("         SELECT prompts will fall back to Serial input."));
    }

    // alignMotorsInternal(), executeMove() and calibrateMotorRotations() all use
    // fixed size-6 stack arrays indexed by numMotors, which is public and
    // mutable. MotorEncoders[] has 7 entries, so setting numMotors = 7 to
    // "include the ring" would smash the stack. Clamp it once, here.
    if (numMotors > 6) numMotors = 6;
    if (numMotors < 1) numMotors = 1;

    // Input is up now; clear any latch raised while the encoder was coming online.
    clearAbort();

    // Motor Initialization
    motorHomeState = -1;
    if(getMotorCalibration()){
        homeMotors();
    }

}

int CubeSystem::scanCube(){
    // Scans cube colors and builds virtual cube for solving
    // Outputs:
    //  0 - Success
    //  1 - Color Sensor 1 encountered an invalid face value
    //  2 - Color Sensor 2 encountered an invalid face value
    //  3 - Opposite faces scanned next to eachother (scan error)
    //  1X - Sensor 1 failed to set color array
    //  2X - Sensor 2 failed to set color array
    //  3X - Failed to set orientation
    //  4X - Failed to build unoriented cube array
    //  5X - Failed to build cube array
    //  60 - Cube is physically impossible and could not be repaired.
    //       Colour counts were fine (nine of each) but the corner/edge pieces
    //       don't correspond to real cubies — a compensating misread. Rescan.
    //  70 - Aborted by the user (SELECT held during the scan)
    //  80 - Reorientation move ROTX failed (jam / align timeout / encoder fault)
    //  81 - Reorientation move ROTZ failed
    //  90 - A colour sensor board failed to initialise at boot

    // Reset the virtual cube before scanning.
    // scanFacesRecorded must be cleared too: rebuildFromScan() and repairScan()
    // are public and gate on it being 6, so a scan that aborts early would
    // otherwise leave a stale "complete" record built from a mix of this scan
    // and the previous one.
    // A colour board that failed begin() cannot produce trustworthy readings.
    // This flag was previously set and never consulted, so a missing mux was
    // reported at boot and then scanned from anyway.
    if (!colorSensorsOk) {
        Serial.println(F("Cannot scan: a colour sensor board failed to initialise"));
        return 90;
    }

    virtualCube.resetCube();
    scanFacesRecorded = 0;
    lastFault = 0;          // stale faults must not decorate a new scan's error

    // Move and scan the cube
    // Scan order is [L, B], [U, F], [D, R]
    char lastface1 = 'X';
    char lastface2 = 'X';

    for (int i = 0; i < 3; i++) {
        // Between scan orientations: nothing is mid-travel, so this is a safe
        // point to honour an abort.
        if (abortRequested) {
            Serial.println(F("Scan aborted by user"));
            safeStop(ERR_ABORTED);
            return 70;
        }

        // Scan Sensors
        colorSensor1.scanFace();
        colorSensor2.scanFace();

        // Classify every sticker on both faces, keeping the full reading.
        //
        // Deliberately NOT getColor()/getFaceColors() here: those collapse a
        // low-confidence result to 'U', which setColorArray rejects, which
        // aborts the whole scan below — BEFORE the validation and repair step
        // at the end of this function ever runs. Building from the best guess
        // and letting piece-level validation reject an impossible cube is
        // strictly better: it makes repairScan() reachable, and an impossible
        // cube is caught either way.
        ColorReading r1[9], r2[9];
        colorSensor1.getFaceReadings(r1);
        colorSensor2.getFaceReadings(r2);

        // Determine face being scanned by each sensor (centre sticker).
        char face1 = r1[4].color;
        char face2 = r2[4].color;
        lastface1 = face1; lastface2 = face2;

        if (!r1[4].ok || !r2[4].ok) {
            Serial.println(F("WARNING: low confidence identifying a face centre"));
        }

        // TODO: DEBUG (REMOVE LATER)
        Serial.print("face1: "); Serial.println(face1);
        Serial.print("face2: "); Serial.println(face2);
        
        
        if (face1 == 'U') return 1;
        if (face2 == 'U') return 2;

        // Determine face left of the sensor (for orientation)
        char left1 = face2;     // Sensor 2 is to the left of Sensor 1
        char left2 = face1;     // Sensor 2 is read upside down, making sensor 1 to the left of it

        // TODO: DEBUG (REMOVE LATER)
        Serial.print("left1: "); Serial.println(left1);
        Serial.print("left2: "); Serial.println(left2);

        // Check for impossible face pairing: opposite faces scanned adjacent
        if (left2 == face2) return 3;

        // Convert readings to colour arrays using the BEST GUESS per sticker.
        // A sticker the classifier is unsure about still contributes its most
        // likely colour; the confidence is preserved separately so repairScan()
        // knows which stickers to reconsider first.
        char face1_colors[9];
        char face2_colors[9];
        for (int k = 0; k < 9; k++) {
            face1_colors[k] = r1[k].color;
            face2_colors[k] = r2[k].color;
        }

        // Record the full classification (best, runner-up, confidence) for both
        // faces so repairScan() can retry in software if validation fails.
        {
            int f1 = 2 * i;         // faces are recorded in scan order
            int f2 = 2 * i + 1;
            for (int k = 0; k < 9; k++) {
                scanColor[f1][k] = face1_colors[k];
                scanAlt[f1][k]   = r1[k].alt;
                scanConf[f1][k]  = r1[k].confidence;

                scanColor[f2][k] = face2_colors[k];
                scanAlt[f2][k]   = r2[k].alt;
                scanConf[f2][k]  = r2[k].confidence;
            }
            scanFaceColor[f1] = face1;  scanLeftColor[f1] = left1;
            scanFaceColor[f2] = face2;  scanLeftColor[f2] = left2;
            // NOT set to 6 here on the last face: scanOrientLeft/Back are
            // written further down, and rebuildFromScan()/repairScan() gate on
            // this reaching 6. Setting it early would let them build from this
            // scan's colours and the PREVIOUS scan's orientation.
            scanFacesRecorded = (f2 == 5) ? 5 : (f2 + 1);
        }

        // TODO: DEBUG (REMOVE LATER)
        Serial.print("face1_colors: ");
        for (int i = 0; i < 9; i++) {
        Serial.print(face1_colors[i]); Serial.print(' ');
        }
        Serial.print("\nface2_colors: ");
        for (int i = 0; i < 9; i++) {
        Serial.print(face2_colors[i]); Serial.print(' ');
        }
        Serial.println();

        // Update virtual cube
        int res1 = virtualCube.setColorArray(face1, face1_colors, left1);
        int res2 = virtualCube.setColorArray(face2, face2_colors, left2);
        if(res1) return 10+res1;
        if(res2) return 20+res2;

        // Rotate the cube to next scanning orientation.
        //
        // Check the abort BEFORE this block, not just at the top of the loop.
        // The abort is most likely to be raised during the ~5.4 s of colour
        // integration above; entering the reorientation with the latch set
        // would run the steppers (which never consult the pump) while every
        // servo sweep returned instantly — rotating the cube twice without the
        // bottom servo ever lifting it.
        if (abortRequested) {
            Serial.println(F("Scan aborted by user"));
            safeStop(ERR_ABORTED);
            return 70;
        }

        if (i < 2) {
            botServoExtend();
            pumpDelay(servoDelay);
            ringMiddle();
            botServoPartial();
            pumpDelay(servoDelay);
            // Check these returns — they used to be discarded entirely.
            //
            // SCOPE, precisely: executeMove() defaults to align = false (see the
            // declaration in CubeSystem.h), so no alignment pass runs here and
            // the jam-recovery ladder is never entered. The only faults these
            // guards can catch are an encoder read failure (24) and a user abort
            // (25). They CANNOT detect a jam or a face that failed to turn — the
            // scan reorientations remain open-loop, exactly as they were.
            //
            // Closing that would mean passing align = true, which adds an
            // alignment pass per reorientation and changes the mechanical
            // behaviour of a machine that currently works. Left as-is
            // deliberately; do it as a measured change, not a drive-by.
            int mvx = executeMove("ROTX");
            if (mvx) {
                // A user abort must surface AS an abort. Collapsing it into 80
                // told the operator the machine had jammed when they had simply
                // asked it to stop.
                if (mvx == 20 + ERR_ABORTED) { safeStop(ERR_ABORTED); return 70; }
                Serial.print(F("Scan aborted: ROTX failed, code ")); Serial.println(mvx);
                safeStop(mvx);
                return 80;
            }

            botServoExtend();
            pumpDelay(servoDelay);
            ringRetract();  // Used to be partial?

            int mvz = executeMove("ROTZ");
            if (mvz) {
                if (mvz == 20 + ERR_ABORTED) { safeStop(ERR_ABORTED); return 70; }
                Serial.print(F("Scan aborted: ROTZ failed, code ")); Serial.println(mvz);
                safeStop(mvz);
                return 81;
            }

            botServoRetract();
            pumpDelay(servoDelay);
        }
    }

    // Set Orientation of Cube
    char rightColor = lastface2;
    char backColor  = lastface1;

    // The cube's left face is opposite the right face
    char leftColor;
    switch (rightColor) {
    case 'R': leftColor = 'O'; break;
    case 'O': leftColor = 'R'; break;
    case 'G': leftColor = 'B'; break;
    case 'B': leftColor = 'G'; break;
    case 'W': leftColor = 'Y'; break;
    case 'Y': leftColor = 'W'; break;
    default:  leftColor = 'U'; // Invalid fallback
    }

    // Remember the orientation arguments so repairScan() can replay the build.
    // Only now is the record genuinely complete — see the note at the assignment
    // inside the scan loop.
    scanOrientLeft = leftColor;
    scanOrientBack = backColor;
    scanFacesRecorded = 6;

    // Build Unoriented Cube Array
    int e = virtualCube.buildUnorientedCubeArray();
    if (e)   return 40 + e;

    // Pass corrected orientation to virtual cube
    e = virtualCube.setOrientation(leftColor, backColor);
    if (e)   return 30 + e;

    // Build Cube Array
    e = virtualCube.buildCubeArray();
    if (e)   return 50 + e;

    // Physical-plausibility check.
    //
    // Colour counts are already verified by buildUnorientedCubeArray(), but a
    // compensating misread keeps every count at 9. Check the actual pieces, and
    // if they don't hold up try to repair the scan in software before giving up
    // — a repair costs microseconds, a rescan costs ~20 seconds of mechanics.
    if (virtualCube.validatePieces() != 0 || virtualCube.validateCentres() != 0) {
        Serial.println(F("Scan produced an impossible cube - attempting repair..."));

        if (repairScan() == 0) {
            Serial.println(F("Repair succeeded: low-confidence sticker(s) reassigned."));
        } else {
            Serial.println(F("Repair failed - rescan required."));
            return 60;      // impossible cube, not repairable
        }
    }

    // TODO: DEBUG (REMOVE LATER)
    Serial.println("=== UNORIENTED CUBE ARRAY ===");
    virtualCube.printUnorientedCubeArray();

    Serial.println("=== ORIENTED CUBE ARRAY ===");
    virtualCube.printCubeArray();

    return 0; // Success
}

int CubeSystem::rebuildFromScan() {
    // Replay the recorded scan into virtualCube. Same sequence scanCube() uses,
    // minus any mechanics — this is pure computation.
    if (scanFacesRecorded != 6) return 1;

    virtualCube.resetCube();

    for (int f = 0; f < 6; f++) {
        char tmp[9];
        for (int k = 0; k < 9; k++) tmp[k] = scanColor[f][k];

        int e = virtualCube.setColorArray(scanFaceColor[f], tmp, scanLeftColor[f]);
        if (e) return 10 + e;
    }

    int e = virtualCube.buildUnorientedCubeArray();
    if (e) return 40 + e;

    e = virtualCube.setOrientation(scanOrientLeft, scanOrientBack);
    if (e) return 30 + e;

    e = virtualCube.buildCubeArray();
    if (e) return 50 + e;

    return 0;
}

int CubeSystem::repairScan(int maxSingles, int maxPairs) {
    // Try substituting each low-confidence sticker's runner-up colour until the
    // cube validates. Singles first, then pairs (a compensating Y/W swap needs
    // exactly two substitutions).
    //
    // Centres are never candidates: they define the face and setColorArray
    // requires them to match.

    if (scanFacesRecorded != 6) return 1;

    struct Cand { int f; int k; float conf; };
    Cand cand[54];
    int n = 0;

    for (int f = 0; f < 6; f++) {
        for (int k = 0; k < 9; k++) {
            if (k == 4) continue;                       // centre sticker
            char a = scanAlt[f][k];
            if (a == 'U' || a == 'E') continue;         // no usable alternative
            if (a == scanColor[f][k]) continue;         // nothing would change
            cand[n].f = f; cand[n].k = k; cand[n].conf = scanConf[f][k];
            n++;
        }
    }
    if (n == 0) return 2;

    // Ascending confidence — least trustworthy stickers first.
    for (int a = 0; a < n - 1; a++) {
        int m = a;
        for (int b = a + 1; b < n; b++) {
            if (cand[b].conf < cand[m].conf) m = b;
        }
        if (m != a) { Cand t = cand[a]; cand[a] = cand[m]; cand[m] = t; }
    }

    // Keep the originals so every failed attempt can be rolled back exactly.
    char original[6][9];
    for (int f = 0; f < 6; f++)
        for (int k = 0; k < 9; k++) original[f][k] = scanColor[f][k];

    const int limSingle = (n < maxSingles) ? n : maxSingles;
    const int limPair   = (n < maxPairs)   ? n : maxPairs;

    // Enumerate EVERY candidate repair and pick the LEAST CONFIDENT one.
    //
    // Three approaches were measured on realistic scans (real scrambles, real
    // per-sensor runner-ups derived from the archived calibration data, a
    // genuine compensating misread injected). Fired / correct / wrong, n=250:
    //
    //   first match wins, narrow window   250 / 250 / 0   <- unsafe in general
    //   first match wins, full search     250 / 250 / 0   <- unsafe in general
    //   unique answer required, narrow     207 / 207 / 0
    //   unique answer required, full         2 /   2 / 0  <- refuses 99.2%
    //
    // Requiring a UNIQUE validating substitution does not work with a full
    // search, and the reason is structural rather than noise: only
    // count-preserving complementary pairs (X->Y paired with Y->X) can validate
    // at all, and on a scrambled cube there are typically two interchangeable
    // choices on each side. So ~4 substitutions validate every time and the
    // uniqueness test never passes. Widening the search is exactly what pulls
    // those alternatives into view.
    //
    // First-match-wins is not the answer either: when the misread stickers do
    // NOT happen to be the least confident, it picks a wrong-but-piece-valid
    // cube (measured 4 wrong out of 5 fires).
    //
    // The discriminator is the confidence the classifier already computed. A
    // compensating misread happens BECAUSE the two colours sit ~0.03 apart, so
    // both offending stickers necessarily have near-zero margin. Selecting the
    // validating substitution with the lowest summed confidence picked the true
    // pair in 200 of 200 trials.
    int   matches      = 0;
    int   bestA = -1, bestB = -1;       // bestB < 0 means a single substitution
    float bestSum      = 0.0f;
    float secondSum    = 0.0f;

    // Ambiguity guard: if the two best candidates are this close in summed
    // confidence, the reading genuinely does not distinguish them and a rescan
    // is the honest answer.
    const float kAmbiguityMargin = 0.05f;

    // --- single substitutions ---
    //
    // Note these can only fire when repairScan() is called on a cube that has
    // NOT already passed the 9-of-each colour count — any single substitution
    // moves two counts off 9. On the scanCube() path the count check has already
    // run, so this loop is a no-op there; it exists for direct callers.
    for (int a = 0; a < limSingle; a++) {
        int f = cand[a].f, k = cand[a].k;
        scanColor[f][k] = scanAlt[f][k];

        if (rebuildFromScan() == 0 &&
            virtualCube.validatePieces()  == 0 &&
            virtualCube.validateCentres() == 0) {
            float sum = scanConf[f][k];
            matches++;
            if (bestA < 0 || sum < bestSum) {
                secondSum = (bestA < 0) ? sum : bestSum;
                bestSum = sum; bestA = a; bestB = -1;
            } else if (matches == 2 || sum < secondSum) {
                secondSum = sum;
            }
        }

        scanColor[f][k] = original[f][k];
    }

    // --- pair substitutions (the compensating-misread case) ---
    for (int a = 0; a < limPair; a++) {
        for (int b = a + 1; b < limPair; b++) {
            int fa = cand[a].f, ka = cand[a].k;
            int fb = cand[b].f, kb = cand[b].k;

            scanColor[fa][ka] = scanAlt[fa][ka];
            scanColor[fb][kb] = scanAlt[fb][kb];

            if (rebuildFromScan() == 0 &&
                virtualCube.validatePieces()  == 0 &&
                virtualCube.validateCentres() == 0) {
                float sum = scanConf[fa][ka] + scanConf[fb][kb];
                matches++;
                if (bestA < 0 || sum < bestSum) {
                    secondSum = (bestA < 0) ? sum : bestSum;
                    bestSum = sum; bestA = a; bestB = b;
                } else if (matches == 2 || sum < secondSum) {
                    secondSum = sum;
                }
            }

            scanColor[fa][ka] = original[fa][ka];
            scanColor[fb][kb] = original[fb][kb];
        }
    }

    if (matches >= 1) {
        // Refuse only on a genuine tie — two candidates the reading cannot tell
        // apart. Anything else, take the least confident (most likely misread).
        if (matches > 1 && (secondSum - bestSum) < kAmbiguityMargin) {
            Serial.print(F("  ambiguous: "));
            Serial.print(matches);
            Serial.print(F(" repairs validate and the best two are within "));
            Serial.print(secondSum - bestSum, 3);
            Serial.println(F(" confidence - refusing to guess"));
        } else {
            int fa = cand[bestA].f, ka = cand[bestA].k;
            scanColor[fa][ka] = scanAlt[fa][ka];
            if (bestB >= 0) {
                int fb = cand[bestB].f, kb = cand[bestB].k;
                scanColor[fb][kb] = scanAlt[fb][kb];
                Serial.print(F("  repaired pair: face "));
                Serial.print(fa); Serial.print('/'); Serial.print(ka);
                Serial.print(F(" and face "));
                Serial.print(fb); Serial.print('/'); Serial.print(kb);
            } else {
                Serial.print(F("  repaired sticker face "));
                Serial.print(fa); Serial.print(F(" pos ")); Serial.print(ka);
            }
            Serial.print(F("  (confidence "));
            Serial.print(bestSum, 3);
            Serial.print(F(", next best "));
            Serial.print(matches > 1 ? secondSum : 9.999f, 3);
            Serial.println(F(")"));

            rebuildFromScan();
            return 0;
        }
    }

    // Nothing worked — restore the original scan so the caller sees what was
    // actually read rather than the last failed guess.
    for (int f = 0; f < 6; f++)
        for (int k = 0; k < 9; k++) scanColor[f][k] = original[f][k];
    rebuildFromScan();

    return 3;
}

bool CubeSystem::powerCheck() {
    return digitalRead(POWPIN);
}

bool CubeSystem::getMotorCalibration() {
    // Returns if motor encoders are calibrated
    for (int i = 0; i < numMotors; i++) {
        if (MotorEncoders[i]->loadCalibration() != 0) return false;
    }
    return true;
}

int CubeSystem::calibrateMotorRotations(){
    int rawVals[6][4];

    cubeMotors.resetMotorPos();

    // Scan current position.
    // A failed read here would be sorted into the calibration set and written
    // to EEPROM as a permanent bad reference, so abort instead.
    for (int i = 0; i < numMotors; i++) {
        rawVals[i][0] = MotorEncoders[i]->scanChecked();
        if (rawVals[i][0] < 0) {
            Serial.print(F("Calibration aborted: encoder "));
            Serial.print(i);
            Serial.println(F(" unreadable"));
            return ERR_ENCODER_FAULT;
        }
    }

    // Rotate and scan 3 more times
    for (int step = 1; step < 4; step++) {
        // Catches an outright move failure (invalid move, encoder fault). It
        // canNOT confirm the face actually turned: executeMove defaults to
        // align = false, and alignment is unavailable here by definition — this
        // function is what produces the calibration alignment depends on.
        // A silently-missed turn still has to be caught by the spacing check
        // further down, which rejects consecutive marks that aren't ~1024
        // counts apart.
        int mv = executeMove("ALL");
        if (mv != 0) {
            Serial.print(F("Calibration aborted: move failed, code "));
            Serial.println(mv);
            return mv;
        }
        delay(200);

        for (int i = 0; i < numMotors; i++) {
            rawVals[i][step] = MotorEncoders[i]->scanChecked();
            if (rawVals[i][step] < 0) {
                Serial.print(F("Calibration aborted: encoder "));
                Serial.print(i);
                Serial.println(F(" unreadable"));
                return ERR_ENCODER_FAULT;
            }
        }
    }
    executeMove("ALL"); // Return to original position

    // Sort and rearrange each motor's vector
    for (int i = 0; i < numMotors; i++) {
        // Sort ascending
        std::sort(rawVals[i], rawVals[i] + 4); // Sorts each motors calibrated values

        // Rotate: [a, b, c, d] → [b, c, d, a] so ~90 val is first
        int reordered[4] = {
            rawVals[i][1],
            rawVals[i][2],
            rawVals[i][3],
            rawVals[i][0]   
        };

        // Write into MotorEncoders.
        // setCalibration() rejects out-of-range values by returning non-zero
        // and LEAVING THE PREVIOUS VALUE IN PLACE — so an unchecked failure
        // here silently produces a half-old / half-new calibration set that is
        // then written to EEPROM as if it were good.
        for (int j = 0; j < 4; j++) {
            if (MotorEncoders[i]->setCalibration(j, reordered[j]) != 0) {
                Serial.print(F("Calibration aborted: motor "));
                Serial.print(i);
                Serial.print(F(" rejected value "));
                Serial.println(reordered[j]);
                return ERR_ENCODER_FAULT;
            }
        }

        // Write calibrated values to EEPROM
        if (!MotorEncoders[i]->saveCalibration()) {
            Serial.print(F("Calibration aborted: EEPROM save failed for motor "));
            Serial.println(i);
            return ERR_ENCODER_FAULT;
        }
    }

    return 0;
}

int CubeSystem::alignMotorsInternal(bool selectiveAlign) {
    // Internal alignment function used by both homeMotors() and alignMotors()
    // 
    // selectiveAlign = false: Align all motors (full homing)
    // selectiveAlign = true:  Only align motors marked in motorMoved[]
    //
    // Outputs:
    //      0 - Success
    //      2 - Did not reach threshold in time

    bool aligned = false;
    unsigned long t_start = millis();
    unsigned long timeout = selectiveAlign ? alignTimeout : homeTimeout;
    long pos[6];

    // Initialize motor positions
    for (int i = 0; i < numMotors; i++) {
        pos[i] = cubeMotors.getPos(i);
    }

    // Enable motors
    cubeMotors.enableMotors();

    // Consecutive encoder read failures per motor, and the first error observed
    // per motor this alignment (for the E1 diagnostic log).
    int  encFail[6]     = {0, 0, 0, 0, 0, 0};
    int  initialErr[6]  = {0, 0, 0, 0, 0, 0};
    bool haveInitial[6] = {false, false, false, false, false, false};

    // Alignment loop
    while (!aligned) {
        // Service the display and poll input once per pass, and HONOUR the
        // result. Alignment can run for up to alignTimeout (500 ms) per move and
        // is the longest pumped leg of the jam-recovery ladder, so ignoring an
        // abort here means the user's request is deferred by up to 500 ms per
        // retry, three retries deep.
        if (!pumpOnce()) {
            cubeMotors.disableMotors();
            Serial.println(F("Alignment aborted by user"));
            return ERR_ABORTED;
        }

        aligned = true;

        // Check each motor
        for (int i = 0; i < numMotors; i++) {

            // Skip if selective alignment and motor didn't move
            if (selectiveAlign && !motorMoved[i]) {
                continue;
            }

            // Update encoder values.
            //
            // A negative return is an I2C fault, NOT a position. This used to
            // be `scan()` with the sign ignored, so a failed read was fed
            // straight into the target search and the direction test below —
            // stepping the motor blind, once per iteration, with no feedback,
            // until the timeout expired. That is 45-90 degrees of unintended
            // rotation at full torque with the cube clamped, repeated by the
            // retry ladder in executeMove().
            int currentVal = MotorEncoders[i]->scanChecked();

            if (currentVal < 0 && ++encFail[i] >= 2) {
                // Second consecutive failure: the mux may be wedged, which no
                // amount of retrying at this level will clear. KEEP the result
                // of the post-reset read — an earlier version discarded it and
                // `continue`d, so a marginal bus that recovered after a reset
                // never stepped, never cleared encFail[i], and burned the whole
                // alignment timeout before reporting ERR_ALIGN_TIMEOUT. That
                // made executeMove diagnose a jam and run backout + re-home x3
                // for what was actually a flaky I2C line.
                resetEncoderMux();
                currentVal = MotorEncoders[i]->scanChecked();
            }

            if (currentVal < 0) {
                if (encFail[i] >= 2) {
                    cubeMotors.disableMotors();
                    Serial.print(F("FAULT: encoder "));
                    Serial.print(i);
                    Serial.println(F(" unreadable - aborting alignment"));
                    return ERR_ENCODER_FAULT;
                }
                // Never step on a reading we don't have, and never report
                // success while a motor's position is unknown.
                aligned = false;
                continue;
            }
            encFail[i] = 0;

            // Find the closest of the 4 calibration values to move to
            int minDiff = 4096;
            int targetVal = MotorEncoders[i]->getCalibration(0);

            // Loop through each of the 4 aligned positions
            for (int j = 0; j < 4; j++) {

                // Skip the starting position if in selective mode
                if (selectiveAlign && motorMoved[i] && j == startCalIndex[i]) {
                    continue;
                }

                int calVal = MotorEncoders[i]->getCalibration(j);
                int diff = abs(encError(currentVal, calVal));

                if (diff < minDiff) {
                    minDiff = diff;
                    targetVal = calVal;
                }
            }

            // Signed, wraparound-safe error to the chosen target.
            int err = encError(currentVal, targetVal);

            if (!haveInitial[i]) {          // E1: record the pre-correction error
                initialErr[i]  = err;
                haveInitial[i] = true;
            }

            // Check if within tolerance
            if (abs(err) > motorAlignmentTol) {
                aligned = false;

                // Move toward target. One sign convention, decided once — see
                // kAlignStepSign in CubeSystem.h.
                pos[i] += (err > 0 ? kAlignStepSign : -kAlignStepSign) * stepSize;
            }
        }

        // Apply new positions
        cubeMotors.moveTo(pos);

        // Check for timeout
        if (millis() - t_start > timeout) {
            cubeMotors.disableMotors();
            return ERR_ALIGN_TIMEOUT;
        }
    }

    // E1 diagnostic: how far off each motor was BEFORE this alignment corrected
    // it. Over many solves the distribution of these numbers distinguishes
    // missed-steps-during-motion from drift-while-de-energised.
    if (debugAlignLog) {
        for (int i = 0; i < numMotors; i++) {
            if (!haveInitial[i]) continue;
            Serial.print(F("[align] motor "));
            Serial.print(i);
            Serial.print(F(" err="));
            Serial.print(initialErr[i]);
            Serial.print(F(" counts ("));
            Serial.print(initialErr[i] / 10.24f, 1);   // 4096 counts / 400 steps per rev
            Serial.println(F(" steps)"));
        }
    }

    // Disable motors
    cubeMotors.disableMotors();

    // Reset stepper position tracking
    cubeMotors.resetMotorPos();

    return 0;   // Success
}

int CubeSystem::homeMotors() {
    // Homes all motors from unknown position (full homing procedure)
    // Outputs:
    //      0 - Success
    //      1 - Motors not calibrated
    //      2 - Did not reach threshold in time

    // Check if motors are calibrated
    if (!getMotorCalibration()) {
        return 1;
    }

    // Clear all tracking variables for fresh start
    for (int i = 0; i < 6; i++) {
        startCalIndex[i] = -9999;
        motorMoved[i] = false;
    }

    // Call internal alignment with full homing mode
    return alignMotorsInternal(false);
}

int CubeSystem::alignMotors() {
    // Re-aligns motors after a move (only aligns motors that moved)
    // Outputs:
    //      0 - Success
    //      1 - Motors not calibrated
    //      2 - Did not reach threshold in time

    // Check if motors are calibrated
    if (!getMotorCalibration()) {
        return 1;
    }

    // Determine starting calibration index for each motor
    for (int i = 0; i < numMotors; i++) {
        int cur = MotorEncoders[i]->scanChecked();
        if (cur < 0) {
            Serial.print(F("FAULT: encoder "));
            Serial.print(i);
            Serial.println(F(" unreadable - cannot determine start index"));
            return ERR_ENCODER_FAULT;
        }

        int bestIdx = 0;
        int bestDiff = 9999;

        for (int j = 0; j < 4; j++) {
            int cal = MotorEncoders[i]->getCalibration(j);
            int diff = abs(encError(cur, cal));
            if (diff < bestDiff) {
                bestDiff = diff;
                bestIdx = j;
            }
        }

        startCalIndex[i] = bestIdx;
    }

    // Call internal alignment with selective mode
    return alignMotorsInternal(true);
}

bool CubeSystem::getColorCalibration(){
    // Returns if color sensors are calibrated.
    //
    // NOTE: ColorSensor::loadCalibration() returns BOOL (true == calibrated),
    // unlike MotorEncoder::loadCalibration() which returns INT (0 == success).
    // This function was written with the int convention and applied to the bool
    // one, so `loadCalibration() != 0` was true for a CALIBRATED sensor and it
    // returned the exact inverse of the truth in both directions.
    //
    // Both sensors are loaded unconditionally — do not short-circuit with &&,
    // or sensor 2 never gets its calibration read into RAM when sensor 1 fails.
    bool cal1 = colorSensor1.loadCalibration();
    bool cal2 = colorSensor2.loadCalibration();

    return cal1 && cal2;
}

// Restore the in-RAM colour calibration after a failed/aborted calibration.
//
// scanFace() returns early on abort leaving scanVals holding the PREVIOUS
// window, and setColorCal() has already written some of that into calVals[].
// Leaving EEPROM untouched is necessary but NOT sufficient: nothing reloads
// calVals, so every subsequent scan this power cycle classifies against a
// poisoned table while the user has been told "nothing was changed".
void CubeSystem::calibrationBail(int why) {
    Serial.print(F("Colour calibration bailing, code ")); Serial.println(why);
    if (!colorSensor1.loadCalibration()) colorSensor1.resetCalibration();
    if (!colorSensor2.loadCalibration()) colorSensor2.resetCalibration();
    cubeMotors.disableMotors();
}

int CubeSystem::calibrateColorSensors(){
    if (!colorSensorsOk) {
        Serial.println(F("Cannot calibrate: a colour sensor board failed to initialise"));
        return 90;
    }

    // Outputs:
    //  0 - Success
    //  1 - Cube not loaded (NOT IMPLEMENTED)

    // ------------ STEP 1 ------------
    // Scan first 4 sides

    // Order of faces being scanned in step 1
    const char faceColors[4][2] = {
        {'R', 'G'}, // Red Left, Green Back
        {'B', 'R'}, // Blue Left, Red Back
        {'O', 'B'}, // Orange Left, Blue Back
        {'G', 'O'}  // Green Left, Orange Back
    };

    // Center cube in chamber
    botServoPartial();
    botServoRetract();

    // Scan the four side faces
    for (int rot = 0; rot < 4; rot++) {

        // Scan current face configuration
        colorSensor1.scanFace();
        colorSensor2.scanFace();

        // Set color calibration values for designated faces
        for (int i = 0; i < 9; i++) {
            colorSensor1.setColorCal(i, faceColors[rot][0], colorSensor1.getScanValRow(i));
            colorSensor2.setColorCal(i, faceColors[rot][1], colorSensor2.getScanValRow(i));
        }

        // Rotate cube orientation (skip last time)
        if (rot < 3) {
            botServoPartial();
            //topServoExtend();
            delay(500);
            int cmv1 = executeMove("ROTZ");
            if (cmv1) { calibrationBail(cmv1); return 82; }
            //topServoRetract();
            botServoRetract();
            delay(500);
        }
    }

    // ------------ STEP 2 ------------
    // Scan empty slot

    // Rotate cube 
    botServoExtend();
    ringMiddle();
    pumpDelay(servoDelay);
    botServoRetract();
    pumpDelay(servoDelay);

    // Scan and set color calibration values for empty face
    colorSensor1.scanFace();
    colorSensor2.scanFace();
    for (int i = 0; i < 9; i++) {
        colorSensor1.setColorCal(i, 'E', colorSensor1.getScanValRow(i));
        colorSensor2.setColorCal(i, 'E', colorSensor2.getScanValRow(i));
    }

    // Rotate cube about x-axis and retract
    int cmv2 = executeMove("ROTX");
            if (cmv2) { calibrationBail(cmv2); return 82; }
    botServoExtend();
    pumpDelay(servoDelay);
    ringRetract();
    botServoRetract();
    pumpDelay(servoDelay);

    // ------------ STEP 3 ------------
    // Scan remaining top/bottom faces

    // Order of faces being scanned in step 3
    const char topFaces[4][2] = {
        {'Y', 'O'}, // Yellow Left, Orange Back
        {'R', 'Y'}, // Red Left, Yellow Back
        {'W', 'R'}, // White Left, Red Back
        {'O', 'W'}  // Orange Left, White Back
    };
    
    // Scan the four side faces
    for (int rot = 0; rot < 4; rot++) {
        colorSensor1.scanFace();
        colorSensor2.scanFace();

        // Set color calibration values for designated faces
        for (int i = 0; i < 9; i++) {
            colorSensor1.setColorCal(i, topFaces[rot][0], colorSensor1.getScanValRow(i));
            colorSensor2.setColorCal(i, topFaces[rot][1], colorSensor2.getScanValRow(i));
        }

        // Rotate cube orientation (skip last time)
        if (rot < 3) {
            botServoExtend();
            //topServoExtend();
            pumpDelay(servoDelay);
            int cmv3 = executeMove("ROTZ");
            if (cmv3) { calibrationBail(cmv3); return 82; }
            //topServoRetract();
            botServoRetract();
            pumpDelay(servoDelay);
        }
    }

    // Never persist a calibration that was interrupted.
    //
    // scanFace() returns early on abort with scanVals holding the PREVIOUS
    // window's data, and setColorCal() has already stored some of it. Writing
    // that to EEPROM would destroy a good calibration and replace it with
    // garbage that saveCalibration()'s read-back verify cannot detect — it only
    // checks EEPROM matches RAM, never that the values are plausible.
    if (abortRequested) {
        Serial.println(F("Colour calibration aborted - EEPROM left untouched"));
        calibrationBail(ERR_ABORTED);
        safeStop(ERR_ABORTED);
        return 9;
    }

    // Save calibrated values to EEPROM.
    //
    // saveCalibration() now deliberately leaves the valid flag CLEARED if the
    // read-back verify fails, so discarding its return value would tell the
    // user their multi-minute calibration succeeded while leaving the machine
    // uncalibrated with no diagnostic.
    bool ok1 = colorSensor1.saveCalibration();
    bool ok2 = colorSensor2.saveCalibration();
    if (!ok1 || !ok2) {
        Serial.print(F("ERROR: colour calibration failed to save ("));
        if (!ok1) Serial.print(F("sensor 1 "));
        if (!ok2) Serial.print(F("sensor 2"));
        Serial.println(F(")"));
        calibrationBail(8);
        return 8;   // calibration not persisted — machine is NOT calibrated
    }

    // Report per-sensor health now that separations have been recomputed.
    for (int b = 0; b < 2; b++) {
        ColorSensor& cs = (b == 0) ? colorSensor1 : colorSensor2;
        for (int s = 0; s < 9; s++) {
            int h = cs.checkSensorHealth(s);
            if (h != 0) {
                Serial.print(F("WARNING: board ")); Serial.print(b + 1);
                Serial.print(F(" sensor ")); Serial.print(s);
                Serial.println(h == 1 ? F(" has a dead channel")
                                      : F(" separation too small to classify"));
            }
        }
    }

    return 0;
}

void CubeSystem::topServoExtend() {
    topServo.extend();
    pumpDelay(servoDelay);
}

void CubeSystem::topServoRetract() {
    topServo.retract();
    pumpDelay(servoDelay);
}

void CubeSystem::topServoPartial()
{
    topServo.partial();
    pumpDelay(servoDelay);
}

void CubeSystem::botServoExtend() {
    botServo.extend();
    pumpDelay(servoDelay);
}

void CubeSystem::botServoRetract() {
    botServo.retract();
    pumpDelay(servoDelay);
}

void CubeSystem::botServoPartial()
{
    botServo.partial();
    pumpDelay(servoDelay);
}

void CubeSystem::toggleTopServo() {
    topServo.toggle();
    pumpDelay(servoDelay);
}

void CubeSystem::toggleBotServo() {
    botServo.toggle();
    pumpDelay(servoDelay);
}

void CubeSystem::toggleRing() {
    cubeMotors.ringToggle();
    pumpDelay(servoDelay);
}

void CubeSystem::ringExtend() {
    cubeMotors.ringMove(2);
}

void CubeSystem::ringPartial(){
    cubeMotors.ringMove(3);
}

void CubeSystem::ringMiddle() {
    cubeMotors.ringMove(1);
}

void CubeSystem::ringRetract() {
    cubeMotors.ringMove(0);
}   

int CubeSystem::executeMove(const String &move, bool moveVirtual, bool align) {
    // Outputs:
    //  0 - Ran successfully
    //  1X - Virtual move failed (X is error thrown by virtual move)
    //  2X - Alignment failed after retries
    //  3 - Move string is invalid

    // Pre-check alignment if alignment is requested
    if (align) {
        if (!checkAlignment()) {
            Serial.println("Motors misaligned before move - homing now");
            int homeResult = homeMotors();
            if (homeResult != 0) {
                Serial.println("ERROR: Pre-move homing failed");
                return 20 + homeResult;
            }
        }
    }

    const int MAX_RETRIES = 3;
    int retryCount = 0;

    while (retryCount < MAX_RETRIES) {
        // Honour an abort before starting another retry. Without this the
        // ladder runs its full course — up to 3 x (move + align + backout +
        // rehome) is ~11 s of driving a jammed, clamped cube after the user has
        // already asked the machine to stop.
        if (abortRequested) {
            cubeMotors.disableMotors();
            Serial.println(F("Move aborted by user"));
            return 20 + ERR_ABORTED;
        }

        // Store starting encoder positions.
        // These become the backout reference and the startCalIndex basis, so a
        // bad reading here corrupts both the alignment target and the jam
        // recovery. Fail the move rather than proceed on a guess.
        int startPositions[6];
        for (int i = 0; i < numMotors; i++) {
            startPositions[i] = MotorEncoders[i]->scanChecked();
            if (startPositions[i] < 0) {
                cubeMotors.disableMotors();
                Serial.print(F("FAULT: encoder "));
                Serial.print(i);
                Serial.println(F(" unreadable before move - aborting"));
                return 20 + ERR_ENCODER_FAULT;
            }
        }

        // Reset motorMoved flags
        for (int i = 0; i < 6; i++) {
            motorMoved[i] = false;
        }

        // Mark motors that this move affects
        if (move == "U" || move == "U'" || move == "U2")
            motorMoved[0] = true;
        else if (move == "R" || move == "R'" || move == "R2")
            motorMoved[1] = true;
        else if (move == "F" || move == "F'" || move == "F2")
            motorMoved[2] = true;
        else if (move == "D" || move == "D'" || move == "D2")
            motorMoved[3] = true;
        else if (move == "L" || move == "L'" || move == "L2")
            motorMoved[4] = true;
        else if (move == "B" || move == "B'" || move == "B2")
            motorMoved[5] = true;
        else if (move == "ROTX") {
            motorMoved[4] = true;   // L
            motorMoved[1] = true;   // R (inverse)
        }
        else if (move == "ROTZ") {
            motorMoved[0] = true;   // U
            motorMoved[3] = true;   // D (inverse)
        }
        else if (move == "ALL") {
            for (int i = 0; i < 6; i++)
                motorMoved[i] = true;
        }
        else {
            return 3; // Invalid move
        }

        // Execute the physical move
        cubeMotors.executeMove(move);

        // Check if alignment is needed
        if (align) {
            // Determine starting calibration indices
            for (int i = 0; i < numMotors; i++) {
                if (!motorMoved[i]) continue;

                int bestIdx = 0;
                int bestDiff = 9999;

                for (int j = 0; j < 4; j++) {
                    int cal = MotorEncoders[i]->getCalibration(j);
                    int diff = abs(encError(startPositions[i], cal));
                    if (diff < bestDiff) {
                        bestDiff = diff;
                        bestIdx = j;
                    }
                }
                startCalIndex[i] = bestIdx;
            }

            // Try to align
            int alignResult = alignMotorsInternal(true);
            
            if (alignResult == 0) {
                // Alignment successful!
                break;
            }
            
            // Alignment failed - likely jammed
            Serial.print("Alignment failed, attempt ");
            Serial.print(retryCount + 1);
            Serial.println(" - backing out and re-homing");

            // Step 1: Back out only the moved motor(s) to relieve jam
            bool backoutSuccess = backoutMove(startPositions);
            
            if (!backoutSuccess) {
                Serial.println("ERROR: Failed to back out of jammed position");
                return 20 + alignResult;
            }

            Serial.println("Backout complete - now homing all motors");

            // Step 2: Clear motorMoved flags so homeMotors aligns ALL motors
            for (int i = 0; i < 6; i++) {
                motorMoved[i] = false;
            }

            // Step 3: Re-home all motors from their current positions
            int homeResult = homeMotors();
            if (homeResult != 0) {
                Serial.println("ERROR: Re-homing failed");
                return 20 + homeResult;
            }

            retryCount++;
            
            if (retryCount >= MAX_RETRIES) {
                Serial.println("ERROR: Max retries exceeded");
                return 20 + alignResult;
            }
            
            // Loop will retry the move
            pumpDelay(100);
        }
        else {
            // No alignment requested, just do the move once
            break;
        }
    }

    // Execute virtual move if requested
    if (moveVirtual) {
        int result = virtualCube.executeMove(move);
        if (result) return 10 + result;
    }

    return 0;   // Success
}

bool CubeSystem::backoutMove(int targetPositions[6]) {
    // Try to return motors to their starting positions
    // Returns true if successful, false if failed
    
    const unsigned long BACKOUT_TIMEOUT = 2000;
    unsigned long startTime = millis();
    
    long pos[6];
    for (int i = 0; i < 6; i++) {
        pos[i] = cubeMotors.getPos(i);
    }
    
    cubeMotors.enableMotors();
    
    bool aligned = false;
    while (!aligned && (millis() - startTime < BACKOUT_TIMEOUT)) {
        // This loop had no pump at all: up to 2 s of completely frozen display
        // while the machine works a jammed, clamped cube — and no way for the
        // user's abort to be seen during the longest leg of the recovery ladder.
        if (!pumpOnce()) {
            cubeMotors.disableMotors();
            Serial.println(F("Backout aborted by user"));
            return false;
        }

        aligned = true;
        
        for (int i = 0; i < numMotors; i++) {
            if (!motorMoved[i]) continue;

            // Encoder faults abort the backout rather than being stepped on.
            // This path is only reached when a face is ALREADY stuck, so
            // driving blind here is the worst possible time to do it.
            int currentPos = MotorEncoders[i]->scanChecked();
            if (currentPos < 0) {
                cubeMotors.disableMotors();
                Serial.print(F("FAULT: encoder "));
                Serial.print(i);
                Serial.println(F(" unreadable during backout - aborting"));
                return false;
            }

            int err = encError(currentPos, targetPositions[i]);

            if (abs(err) > motorAlignmentTol) {
                aligned = false;

                // Same sign convention as alignMotorsInternal().
                //
                // This block previously handled the 0/4095 seam correctly but
                // used the OPPOSITE sign, so backout drove away from the start
                // position on every iteration — burning the full 2 s timeout
                // pushing a jammed face further into its jam before giving up.
                pos[i] += (err > 0 ? kAlignStepSign : -kAlignStepSign) * stepSize;
            }
        }

        cubeMotors.moveTo(pos);
    }

    cubeMotors.disableMotors();

    if (!aligned) {
        Serial.println("Backout timeout - could not return to start");
        return false;
    }
    
    // Reset position tracking
    cubeMotors.resetMotorPos();
    return true;
}

bool CubeSystem::checkAlignment() {
    // Returns true if all motors are aligned
    // Returns false if one or more motors is misaligned
    
    for (int i = 0; i < numMotors; i++) {
        // An unreadable encoder means alignment is UNKNOWN, not "aligned".
        // Report misaligned so the caller re-homes rather than proceeding.
        int currentVal = MotorEncoders[i]->scanChecked();
        if (currentVal < 0) {
            Serial.print(F("FAULT: encoder "));
            Serial.print(i);
            Serial.println(F(" unreadable during alignment check"));
            return false;
        }

        bool aligned = false;

        for (int j = 0; j < 4; j++) {
            int calVal = MotorEncoders[i]->getCalibration(j);
            int diff = abs(encError(currentVal, calVal));

            if (diff <= motorAlignmentTol) {
                aligned = true;
                break;
            }
        }

        if (!aligned) {
            return false;
        }
    }

    return true;
}

void CubeSystem::displaySetMessage(const char* msg) {
    if (displayInitialized) {
        cubeDisplay.setMessage(msg);
    }
}

void CubeSystem::displaySetStatus(const char* msg) {
    if (displayInitialized) {
        cubeDisplay.setStatus(msg);
    }
}

void CubeSystem::displayClearStatus() {
    if (displayInitialized) {
        cubeDisplay.clearStatus();
    }
}

void CubeSystem::displayUpdate() {
    if (displayInitialized) {
        cubeDisplay.update();
    }
}

void CubeSystem::displayWaitForSelect(const char* msg) {
    // A user gate must never become a no-op.
    //
    // This function used to be nothing but `if (displayInitialized) { ... }`
    // with no else. Every "press SELECT to continue" prompt in the solve
    // sketches routes through here — including the one immediately before the
    // servos clamp the cube and the one immediately before executeSolve(). If
    // the display failed to initialise (a loose ribbon on a machine with six
    // steppers vibrating next to it) every gate returned instantly and the
    // machine scanned, clamped and ran a full solve unattended.
    // Gate on the encoder as well as the display. cubeDisplay.waitForSelect()
    // spins on menuEncoder.selectPressed() with no Serial escape, so with a
    // working display and a DEAD seesaw — the exact case begin() warns about —
    // it would wedge forever on a device that never answers.
    if (displayInitialized && encoderInitialized) {
        cubeDisplay.waitForSelect(msg);
        return;
    }

    // --- Fallback: display and/or encoder unavailable. ---
    //
    // The display may still be WORKING here (display ok + dead encoder takes
    // this path), so drive it rather than leaving the previous screen frozen
    // while Serial claims there is no display.
    if (displayInitialized) {
        cubeDisplay.setMessage(msg);
        cubeDisplay.setStatus("Send any character over Serial to continue");
        cubeDisplay.update();
    }

    Serial.println();
    Serial.print(displayInitialized ? F("[NO ENCODER] ") : F("[NO DISPLAY] "));
    Serial.println(msg);
    Serial.println(F("Press SELECT, or send any character over Serial, to continue..."));

    // Drain anything already buffered so a stale byte cannot satisfy the gate.
    while (Serial.available()) {
        Serial.read();
    }

    if (encoderInitialized) {
        // Same release / press / release sequence CubeDisplay uses, so a button
        // already held down when we arrive cannot immediately satisfy the gate.
        while (menuEncoder.selectPressed()) {
            if (Serial.available()) { while (Serial.available()) Serial.read(); return; }
            delay(10);
        }
        while (!menuEncoder.selectPressed()) {
            if (Serial.available()) { while (Serial.available()) Serial.read(); return; }
            delay(10);
        }
        while (menuEncoder.selectPressed()) {
            if (Serial.available()) { while (Serial.available()) Serial.read(); break; }
            displayUpdate();
            delay(10);
        }
        return;
    }

    // No display AND no encoder: Serial is the only way through. Blocking here
    // is correct — it is a gate, and silently continuing is what this whole
    // function exists to prevent.
    Serial.println(F("WARNING: no display and no menu encoder. Serial input required."));

    // With neither a display nor an encoder there is no way to obtain user
    // confirmation, so this machine cannot be operated safely. Blocking is the
    // right answer — silently continuing is the bug this whole function exists
    // to prevent — but block from a KNOWN SAFE STATE and say so periodically,
    // rather than hanging opaquely.
    //
    // On a Teensy on a bench supply Serial.available() is never non-zero, so
    // without the repeated message this is indistinguishable from a crash.
    //
    // Deliberately not blinking the onboard LED: that is pin 13, which is also
    // the display SCK, and driving it would fight the SPI peripheral.
    cubeMotors.disableMotors();

    unsigned long lastNotice = millis();
    while (!Serial.available()) {
        displayUpdate();        // no-op if there is no display; keeps it alive if there is
        if (millis() - lastNotice >= 5000) {
            lastNotice = millis();
            Serial.println(F("  ...still waiting for Serial input. "
                             "Motors are disabled; the machine is idle and safe."));
        }
        delay(10);
    }
    while (Serial.available()) {
        Serial.read();
    }
}

bool CubeSystem::displayReady() {
    return displayInitialized;
}

void CubeSystem::unloadCube() {
    // Suppress abort detection for the duration.
    //
    // This has to live HERE, not only in safeStop(), because the sketch's
    // success path calls unloadCube() directly. A latched abort would otherwise
    // turn every servo sweep into a single servo.write() and a bail, and the
    // display would read "Solved!" with the cube still clamped.
    const bool wasSuppressed = pumpAbortSuppressed;
    pumpAbortSuppressed = true;

    // Release the cube.
    //
    // The order is mechanically load-bearing and is NOT the mirror of the load
    // sequence (load extends bottom -> ring -> top; unload retracts ring first,
    // while the top servo is still extended). Because the safe orderings are
    // not a simple stack they cannot be inferred at a call site, so they live
    // here once.
    //
    // Previously this sequence existed only on the success path of the solve
    // sketches, so any error parked the machine indefinitely with both servos
    // extended and the ring engaged, gripping the cube.
    ringRetract();
    topServoRetract();
    botServoRetract();

    pumpAbortSuppressed = wasSuppressed;
    selectHeldSince = 0;    // a still-held button must be released to re-abort
}

void CubeSystem::safeStop(int faultCode) {
    // Put the machine into a state that is safe to leave unattended.
    lastFault = faultCode;

    // CRITICAL: the unload must not be pumped-out.
    //
    // abortRequested is a LATCH — nothing clears it until the UI does. While it
    // is set, pumpDelay() returns false immediately without waiting, so every
    // servo sweep inside unloadCube() would issue a single servo.write() and
    // bail. The cube would stay clamped while this function printed "halted
    // safely" — the exact failure safeStop() exists to prevent.
    //
    // Suspend the latch AND the detector for the duration of the unload, then
    // restore the latch so the caller and the UI still see that an abort
    // happened. Suppressing the detector is essential: the gesture is a 1 s
    // hold, so the button is still down here, and a stale selectHeldSince would
    // re-latch on the first pump inside unloadCube().
    const bool wasAborted = abortRequested;
    pumpAbortSuppressed = true;
    abortRequested = false;
    selectHeldSince = 0;

    // 1. Kill torque first so nothing fights the unload.
    cubeMotors.disableMotors();

    // 2. Release the cube. ringRetract() re-energises the ring stepper for its
    //    own travel; disable again afterwards.
    unloadCube();
    cubeMotors.disableMotors();

    // 3. Invalidate all derived state. After a fault the true cube state is
    //    unknown, so isReady() must go false and the stale solution must go —
    //    otherwise a caller can replay a partially-executed solution.
    virtualCube.resetCube();
    clearSolution();

    // Restore the latch. The UI clears it when the user acknowledges the fault.
    // selectHeldSince is zeroed so a still-held button must be released and
    // held again to raise a new abort, rather than re-arming ~1 s from now.
    pumpAbortSuppressed = false;
    selectHeldSince = 0;
    abortRequested = wasAborted;

    if (faultCode != 0) {
        Serial.print(F("safeStop: cube released, machine halted safely, fault code "));
        Serial.println(faultCode);
    }
}

void CubeSystem::clearSolution(){
    // Resets the class solution string
    solutionLength = 0;
    for (int i = 0; i < maxMoves; ++i) {
        solveMoves[i] = "";
    }
}

int CubeSystem::solveVirtual(){
    // Updates the solution for the current state of the cube
    // Outputs:
    //  0 - Success
    //  1X - Virtual Cube Solve Error, where X is -(solveCube's return):
    //       11 - Cube is not ready (never scanned / not built)
    //       12 - No solution: illegal cube, or the solver timed out
    //       13 - Centres not canonical -> the ORIENTATION was misread. Rescan.
    //       14 - A corner or edge is not a real cubie -> a COLOUR was misread,
    //            in a way that still left nine of each. Rescan.
    //       15 - Solution longer than maxMoves
    //
    // 13 and 14 both mean "the cube I think I have cannot exist", and both are
    // scan faults rather than solver faults — the right response is to rescan,
    // not to retry the solve.

    // Clear current solution
    clearSolution();

    // Solve Cube
    int solveOutput = virtualCube.solveCube(solveMoves, maxMoves);
    if(solveOutput < 0){
        clearSolution();
        return 10 - solveOutput;
    }

    solutionLength = solveOutput;

    return 0;
}

int CubeSystem::executeSolve(){
    // If the cube has been solved for, executes moves to solve it
    // Outputs:
    //  0 - Success
    //  1 - Cube is not ready
    //  2 - No solution
    //  1XX - 

    // Both of these run AFTER the caller has clamped the cube (the sketch's
    // Loading state extends bottom servo, ring and top servo before entering
    // Executing), so they must release it. Without safeStop() the Error screen
    // returns to the menu with the cube still gripped, contradicting both the
    // contract on safeStop() and the README.
    if (!virtualCube.isReady()) {
        safeStop(1);
        return 1; // Cube not ready
    }

    if (solutionLength == 0 || solveMoves[0].length() == 0) {
        safeStop(2);
        return 2; // No moves found
    }

    // Execute each move
    for (int i = 0; i < solutionLength; i++) {
        // Refresh the display and sample the button between moves as well as
        // inside the alignment loop, so progress is visible and an abort is
        // noticed even on a move that aligns on the first pass.
        pumpOnce();

        // Between-move is the only mechanically safe point to stop: the face is
        // at a detent and nothing is mid-travel.
        if (abortRequested) {
            Serial.print(F("Solve aborted by user before move "));
            Serial.println(i + 1);
            safeStop(ERR_ABORTED);
            return 100 + ERR_ABORTED;
        }

        int e = 0;
        e = executeMove(solveMoves[i], 1, 1);

        if (e) {
            // A failed move leaves the PHYSICAL cube turned (executeMove applies
            // the virtual move last, and three of its error returns fire after
            // at least one physical move) while the model still reflects the
            // pre-move state. Leaving solveMoves[] and cubeReady intact meant a
            // caller could invoke executeSolve() again, restart at index 0, and
            // replay the entire solution onto a cube that was already partway
            // through it — scrambling it and risking a hard jam.
            //
            // After a failed move the true cube state is unknown, so the only
            // correct next step is a rescan. safeStop() enforces that by
            // resetting the virtual cube and clearing the solution.
            Serial.print(F("Solve aborted at move "));
            Serial.print(i + 1);
            Serial.print(F(" of "));
            Serial.print(solutionLength);
            Serial.print(F(" ("));
            Serial.print(solveMoves[i]);
            Serial.print(F("), code "));
            Serial.println(100 + e);

            safeStop(100 + e);
            return 100 + e;
        }
    }

    return 0;
}
