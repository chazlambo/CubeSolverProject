// CubeSystem.cpp
#include "CubeSystem.h"

CubeSystem::CubeSystem() {}

CubeSystem* CubeSystem::s_pumpOwner = nullptr;

bool CubeSystem::pumpTrampoline() {
    if (s_pumpOwner == nullptr) return true;
    return s_pumpOwner->pumpTick();
}

bool CubeSystem::pumpTick() {
    // Keep LVGL ticking, EXCEPT while a stepper is mid-move.
    //
    // Face-motor moves are no longer pumped at all (see the note at the top of
    // CubeMotors.cpp — even the throttled input read below was audible in the
    // step train), so pumpMotionOnly now only guards the RING's pumped loop.
    // A render plus an SPI flush there would steal tens of milliseconds of
    // step time mid-travel, which is what used to jam a face when face moves
    // were pumped.
    //
    // The panel is not starved: executeSolve() pumps once between moves.
    if (!pumpMotionOnly) displayUpdate();

    // While safeStop() is releasing the cube, keep refreshing the display but
    // do not detect aborts — the button that triggered this abort is still down.
    if (pumpAbortSuppressed) {
        chordHeldSince = 0;
        return true;
    }

    // Poll input, but not on every 5 ms pump: readButtons() is a full I2C
    // transaction to the seesaw on Wire1, and doing that 200x/second would
    // saturate the bus for no benefit. 25 ms is far faster than a human.
    unsigned long now = millis();
    if (now - lastInputPoll >= 25) {
        // If the last poll was long ago we were inside an unpumped region and
        // the button state in between is unknown. Restart the hold measurement
        // rather than treating two brief taps that bracket a move as one
        // continuous hold — that produced spurious aborts.
        //
        // The threshold must sit ABOVE the longest routinely-unpumped stretch,
        // or the chord can never accumulate its hold during a solve. Face
        // moves are deliberately unpumped now (see CubeMotors.cpp): a half
        // turn is ~200 ms of stepping plus settle plus the post-move encoder
        // reads, ~260 ms end to end. 400 ms clears that with margin while
        // still catching genuinely blind regions like a homing pass.
        if (now - lastInputPoll > 400) {
            chordHeldSince = 0;
        }
        lastInputPoll = now;

        // SELECT + LEFT, sampled in ONE transaction so both are read at the
        // same instant. Reading them separately makes a chord that is genuinely
        // held look intermittent whenever a press lands between the two reads.
        const uint8_t kChord = RotaryEncoder::BTN_SELECT | RotaryEncoder::BTN_LEFT;
        uint8_t btns = encoderInitialized ? menuEncoder.readButtons() : 0;
        bool held = ((btns & kChord) == kChord);

        // After clearAbort(), require a release before a hold can count again.
        // Otherwise the buttons still down from the abort that just fired
        // re-latch immediately on the next operation.
        //
        // Single exit deliberately: an earlier version returned early from this
        // branch, which made (chordMustRelease && abortRequested) a sticky cell
        // that reported "stop" from a branch whose premise is "no abort detected"
        // and could never clear while the buttons were held.
        if (chordMustRelease) {
            if (!held) chordMustRelease = false;
            chordHeldSince = 0;
        } else if (held) {
            if (chordHeldSince == 0) {
                chordHeldSince = now;
            } else if (now - chordHeldSince >= kAbortHoldMs) {
                if (!abortRequested) {
                    Serial.println(F("ABORT requested (SELECT+LEFT held)"));
                }
                abortRequested = true;
            }
        } else {
            chordHeldSince = 0;
        }
    }

    return !abortRequested;
}

void CubeSystem::begin(bool withDisplay) {    
    // Serial Communication Setup
    Serial.begin(baudRate);

    // Print the last hard fault, if there was one.
    //
    // Teensy 4.x keeps a crash record across a reset, and this is the only
    // thing that distinguishes "the firmware hung" from "the firmware
    // crashed and rebooted" — which look identical on the panel, because a
    // reboot loop redraws the same boot screen every time and never gets
    // further. We spent several bench flashes unable to tell those apart.
    //
    // Costs nothing on a clean boot: CrashReport is falsy when there is no
    // record. Firmware only — CubeSystemSim.cpp replaces this file, and the
    // desktop has no such thing.
    if (CrashReport) {
        Serial.println(F("=== crash report from the previous run ==="));
        Serial.print(CrashReport);
    }

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
    // color-sensor probes — i.e. the /RESET of an active bus participant sat
    // floating during ~5 s of traffic on the very bus it shares.
    initEncoderMuxReset();

    // Display Setup — skipped when the sketch owns the panel itself
    // (begin(kNoDisplay)); every display call below no-ops in that case.
    displayInitialized = withDisplay && cubeDisplay.begin(10000000);

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
    // so a board with a dead mux booted silently and produced garbage colors.
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
    encoderMuxOk = encoderMux.begin();   // Begins I2C Mux for encoders
    if (!encoderMuxOk) {
        Serial.println(F("WARNING: encoder mux did not respond - attempting reset"));
        resetEncoderMux();
        encoderMuxOk = encoderMux.begin();
        if (!encoderMuxOk) {
            Serial.println(F("ERROR: encoder mux unavailable. Motion will fault."));
        }
    }

    for (int i = 0; i < 7; ++i) {
        int rc = MotorEncoders[i]->begin();
        motorEncoderOk[i] = (rc == 0);
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

    // alignMotorsInternal() and calibrateMotorRotations() both use fixed size-6
    // stack arrays indexed by numMotors, which is public and mutable.
    // MotorEncoders[] has 7 entries, so setting numMotors = 7 to "include the
    // ring" would smash the stack. Clamp it once, here.
    if (numMotors > 6) numMotors = 6;
    if (numMotors < 1) numMotors = 1;

    // Input is up now; clear any latch raised while the encoder was coming online.
    clearAbort();

    // Motor Initialization
    motorHomeState = -1;
    if(getMotorCalibration()){
        // Report a failed boot homing: motors standing too far off the marks
        // otherwise time out here in silence and every later solve inherits it.
        motorHomeState = homeMotors();
        if (motorHomeState != 0) {
            Serial.print(F("WARNING: boot homing failed, code "));
            Serial.println(motorHomeState);
        }
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
    //       Color counts were fine (nine of each) but the corner/edge pieces
    //       don't correspond to real cubies — a compensating misread. Rescan.
    //  70 - Aborted by the user (SELECT+LEFT chord held during the scan)
    //  80 - Reorientation move ROTX failed (jam / align timeout / encoder fault)
    //  81 - Reorientation move ROTZ failed
    //  90 - A color sensor board failed to initialise at boot
    //  91 - Motors failed to home before the first clamp (jam / encoder fault)

    // A color board that failed begin() cannot produce trustworthy readings.
    if (!colorSensorsOk) {
        Serial.println(F("Cannot scan: a color sensor board failed to initialise"));
        // Release on the way out, like every other exit: this returns BEFORE
        // the unloadCube() at the head of the scan proper, and a cube clamped
        // from the jog page would otherwise be stranded on the error screen.
        unloadCube();
        return 90;
    }

    // Reset the virtual cube before scanning. scanFacesRecorded must be cleared
    // too: rebuildFromScan() and repairScan() are public and gate on it being 6,
    // so a scan that aborts early would otherwise leave a stale "complete" record
    // built from a mix of this scan and the previous one.
    virtualCube.resetCube();
    scanFacesRecorded = 0;
    lastFault = 0;          // stale faults must not decorate a new scan's error

    // Move and scan the cube
    //
    // Two faces per pass, one per color board, with a reorientation between:
    // [Back, Right], [Left, Down], [Up, Front].
    //
    // It used to be [Left, Back], [Up, Front], [Down, Right]. The color scanner
    // was rotated 90 degrees in CAD to make the assembly fit, which changed
    // which faces each pass sees — and NO CODE CHANGED, because nothing here
    // depends on knowing that in advance.
    //
    // That is worth understanding before touching this loop. Faces are recorded
    // in scan order and identified afterwards from their own centre color plus
    // their neighbour's (scanFaceColor / scanLeftColor, resolved by
    // setOrientation below). What the pass order actually has to preserve is the
    // GEOMETRY BETWEEN THE TWO SENSORS — sensor 2 sits to the left of sensor 1,
    // which the left1/left2 assignment below relies on. Rotating the whole
    // scanner assembly moves both sensors together and leaves that intact, so it
    // costs nothing. Moving one sensor relative to the other would not.
    //
    // The only thing that does track the physical order is the panel's face row,
    // via kScanPassFaces in CubeOpShape.cpp. Rotate the scanner again and that
    // table is the one place to update.
    char lastface1 = 'X';
    char lastface2 = 'X';

    // What the panel shows: one chip per face, filled with the color that
    // face's centre sticker actually came back as. -1 is "not read yet".
    // Lives on the object so the result screen can still show it afterwards.
    int8_t* faceChips = scanFaceChips;
    for (int f = 0; f < 6; ++f) faceChips[f] = -1;

    // Release the cube into the scan chamber BEFORE the first face is read.
    //
    // The pass loop only lowers the bottom servo on its way OUT of a
    // reorientation, so passes 2 and 3 arrive at their reading already down.
    // Pass 1 inherits whatever pose the machine was left in — the jog page,
    // the tuning editor's previews and an aborted sweep can all leave a
    // gripper part-way — and a cube held above the color sensors has them
    // integrate for ~5.4 s on an empty chamber: the scan does not fail
    // loudly, it reads nothing.
    //
    // ALL THREE grippers, via unloadCube(), which owns the release order: a
    // lone botServoRetract() leaves a cube held in the extended ring exactly
    // where it was. Unconditional, so it also recovers the state -1 case (a
    // sweep aborted mid-travel); a horn already at its stop costs no travel.
    //
    // An entry condition, not a step in the per-pass choreography below: that
    // sequence (extend, ring, partial, ROTX, ...) is mechanically load-bearing
    // and already right for the passes it runs on. Keep this above it.
    displaySetStatus("Lowering the cube");
    displayUpdate();
    unloadCube();

    // Square the face fingers BEFORE anything closes on the cube.
    //
    // The first pass reads with everything released, so the first thing to
    // grip the cube is the reorientation after it: botServoExtend() lifts the
    // cube up into the six fingers, and a finger that is standing off its mark is a
    // finger the cube cannot seat into. The motors were homed at boot, but by
    // now the operator has ejected a cube and placed another, and the jog
    // page, the dial and an aborted solve can all leave a finger anywhere. Doing
    // it here, released, means the fingers turn against nothing — a homing pass
    // from the clamped state is six real face turns the model never hears
    // about, which is why motorsHome() on the diagnostic page invalidates it.
    //
    // Skipped, not failed, when the motors are uncalibrated: the scan
    // reorientations are open loop and always were, so an uncalibrated
    // machine can still scan — it just cannot be squared first. begin() makes
    // the same call for boot homing.
    if (getMotorCalibration()) {
        displaySetStatus("Homing motors");
        displayUpdate();
        const int h = homeMotors();
        if (h == ERR_ABORTED) {
            // safeStop() de-energises the steppers before the unload, which
            // matters here: the abort return is the one path out of
            // alignMotorsInternal() that leaves them holding current.
            Serial.println(F("Scan aborted by user"));
            safeStop(ERR_ABORTED);
            return 70;
        }
        if (h != 0) {
            // Released already — the unloadCube() above ran — so unlike the
            // color-board guard there is nothing to let go of on the way out.
            Serial.print(F("Scan aborted: motors failed to home, code "));
            Serial.println(h);
            return 91;
        }
    } else {
        Serial.println(F("WARNING: motors not calibrated, scanning without homing"));
    }

    for (int i = 0; i < 3; i++) {

        // Between scan orientations: nothing is mid-travel, so this is a safe
        // point to honour an abort.
        if (abortRequested) {
            Serial.println(F("Scan aborted by user"));
            safeStop(ERR_ABORTED);
            return 70;
        }

        // Light the pair about to be read BEFORE the ~5.4 s of color
        // integration below, not after: that wait is most of a scan, and a
        // progress display that only updates once it is over is no display.
        displayFaces(faceChips, kScanPassFaces[i][0], kScanPassFaces[i][1]);
        displaySetStatus(kScanPassLabels[i]);
        displayUpdate();

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
        //
        // That argument covers the EIGHT OUTER stickers only. The centre is
        // gated hard below: it decides the face identity and the orientation
        // frame, repairScan() never touches centres by design, and a wrong
        // centre poisons all nine stickers of the face at once. November's
        // firmware refused a scan on an unconvincing centre and that gate is
        // kept.
        ColorReading r1[9], r2[9];
        colorSensor1.getFaceReadings(r1);
        colorSensor2.getFaceReadings(r2);

        // Determine face being scanned by each sensor (centre sticker).
        char face1 = r1[4].color;
        char face2 = r2[4].color;
        lastface1 = face1; lastface2 = face2;

        // Fill the two chips in with what was actually read. A face whose
        // centre came back unknown stays hollow, which is a useful thing to see
        // before the validation below rejects the scan for it.
        faceChips[kScanPassFaces[i][0]] = chipIndexForColor(face1);
        faceChips[kScanPassFaces[i][1]] = chipIndexForColor(face2);
        displayFaces(faceChips);
        displayUpdate();

        // A centre that failed the classifier's gate (`ok` is getColor()'s
        // absolute + relative test — the same test November's scan applied)
        // fails the scan here, exactly as it did then. Codes 1/2 match the
        // 'U'-centre returns just below, which are the same condition seen
        // through classify()'s "unusable" path.
        if (!r1[4].ok) {
            Serial.println(F("Scan rejected: sensor 1 face centre unconvincing"));
            return 1;
        }
        if (!r2[4].ok) {
            Serial.println(F("Scan rejected: sensor 2 face centre unconvincing"));
            return 2;
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

        // Convert readings to color arrays using the BEST GUESS per sticker.
        // A sticker the classifier is unsure about still contributes its most
        // likely color; the confidence is preserved separately so repairScan()
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
            // scan's colors and the PREVIOUS scan's orientation.
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
        // The abort is most likely to be raised during the ~5.4 s of color
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
            displaySetStatus("Rotating cube");
            displayUpdate();

            // No explicit waits here: every servo wrapper below already ends
            // in its own pumpDelay(servoDelay). November's loop carried these
            // delays because the wrappers were bare then; keeping both meant
            // 400 ms per point instead of the 200 ms the machine was tuned on.
            botServoExtend();
            ringMiddle();
            botServoPartial();
            // SCOPE, precisely: executeMove() defaults to align = false (see
            // the declaration in CubeSystem.h), and with November's
            // move-then-check structure restored an align = false move always
            // returns 0 — so these guards are currently INERT. They are kept
            // because they are the right shape for the day executeMove()
            // reports faults on blind moves again, and because removing them
            // would silently change the scan's error contract (codes 80/81).
            //
            // The scan reorientations remain open-loop, exactly as they were
            // in November. Closing that would mean passing align = true, which
            // adds an alignment pass per reorientation and changes the
            // mechanical behaviour of a machine that works. Left deliberately;
            // do it as a measured change, not a drive-by.
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
            ringRetract();  // Used to be partial?

            int mvz = executeMove("ROTZ");
            if (mvz) {
                if (mvz == 20 + ERR_ABORTED) { safeStop(ERR_ABORTED); return 70; }
                Serial.print(F("Scan aborted: ROTZ failed, code ")); Serial.println(mvz);
                safeStop(mvz);
                return 81;
            }

            botServoRetract();
        }
    }

    displayFaces(faceChips);
    displaySetStatus("Checking the cube");
    displayUpdate();

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
    // Color counts are already verified by buildUnorientedCubeArray(), but a
    // compensating misread keeps every count at 9. Check the actual pieces, and
    // if they don't hold up try to repair the scan in software before giving up
    // — a repair costs milliseconds, a rescan costs 25-40 s of mechanics.
    if (virtualCube.validatePieces() != 0 || virtualCube.validateCentres() != 0) {
        Serial.println(F("Scan produced an impossible cube - attempting repair..."));

        if (repairScan() == 0) {
            Serial.println(F("Repair succeeded: low-confidence sticker(s) reassigned."));
            // Put it on the panel too — a repaired scan is trustworthy but the
            // operator should know it happened. The sketch can read
            // lastRepairCount afterwards to say it more durably.
            char repairMsg[40];
            snprintf(repairMsg, sizeof(repairMsg), "Repaired %d sticker%s",
                     lastRepairCount, lastRepairCount == 1 ? "" : "s");
            displaySetStatus(repairMsg);
            displayUpdate();
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

int CubeSystem::repairScan() {
    // Try substituting each low-confidence sticker's runner-up color until the
    // cube validates. Singles first, then pairs (a compensating Y/W swap needs
    // exactly two substitutions).
    //
    // Centres are never candidates: they define the face and setColorArray
    // requires them to match.

    lastRepairCount = 0;

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
    // compensating misread happens BECAUSE the two colors sit ~0.03 apart, so
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
    // NOT already passed the 9-of-each color count — any single substitution
    // moves two counts off 9. On the scanCube() path the count check has already
    // run, so this loop is a no-op there; it exists for direct callers.
    for (int a = 0; a < n; a++) {
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
    for (int a = 0; a < n; a++) {
        for (int b = a + 1; b < n; b++) {
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
            lastRepairCount = (bestB >= 0) ? 2 : 1;
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

    // Settle between each quarter turn and the encoder scan that follows it.
    // executeMove() returns as soon as the steppers are de-energised, while
    // the face is still ringing from the stop. The first cut (200 ms) was
    // short enough that the four marks could be read off a face that had not
    // finished settling, and an out-of-square mark here is permanent: it
    // goes to EEPROM and every later alignment homes to it. Only three of
    // these run per sweep, so the extra wait costs well under a second.
    //
    // A blocking delay, not pumpDelay(): an aborted pumpDelay returns in ~0 ms,
    // which would scan a still-moving encoder and hand the bad reading to the
    // spacing check below — the opposite of what the settle is for.
    static const unsigned long kCalSettleMs = 300;

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
        delay(kCalSettleMs);

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

int CubeSystem::alignMotorsInternal() {
    // Internal alignment used by homeMotors(). This is November's homing loop:
    // one step per motor per pass toward the nearest calibration mark, until a
    // full pass finds every motor within tolerance. The convergence dynamics
    // are deliberately UNCHANGED from the firmware that solved reliably —
    // a proportional (walk-most-of-the-error) version existed briefly and was
    // reverted with the rest of the alignment experiments, untested.
    //
    // Outputs:
    //      0 - Success
    //      2 - Did not reach threshold in time

    bool aligned = false;
    unsigned long t_start = millis();
    unsigned long timeout = homeTimeout;
    long pos[6];

    // Initialize motor positions
    for (int i = 0; i < numMotors; i++) {
        pos[i] = cubeMotors.getPos(i);
    }

    // Keep the DISPLAY out of the correction loop, exactly as the motor move
    // loops do: a render per pass costs 10-30 ms against passes that are
    // otherwise ~10 ms of encoder reads, which is the difference between
    // November's correction rate and a third of it. The abort watch is
    // unaffected — pumpOnce() still polls the buttons under pumpMotionOnly.
    const bool wasMotionOnly = pumpMotionOnly;
    pumpMotionOnly = true;

    // Enable motors
    cubeMotors.enableMotors();

    // Consecutive encoder read failures per motor, and the first error observed
    // per motor this alignment (for the E1 diagnostic log).
    int  encFail[6]     = {0, 0, 0, 0, 0, 0};
    int  initialErr[6]  = {0, 0, 0, 0, 0, 0};
    bool haveInitial[6] = {false, false, false, false, false, false};

    // Alignment loop
    while (!aligned) {
        // Poll input once per pass and HONOUR the result (display excluded,
        // see pumpMotionOnly above). Homing can run for up to a second, and
        // it is the one motion loop that may run with the cube clamped.
        if (!pumpOnce()) {
            cubeMotors.disableMotors();
            pumpMotionOnly = wasMotionOnly;
            Serial.println(F("Alignment aborted by user"));
            return ERR_ABORTED;
        }

        aligned = true;

        // Check each motor
        for (int i = 0; i < numMotors; i++) {

            // Update encoder values.
            //
            // A negative return is an I2C fault, NOT a position. This used to
            // be `scan()` with the sign ignored, so a failed read was fed
            // straight into the target search and the direction test below —
            // stepping the motor blind, once per iteration, with no feedback,
            // until the timeout expired. That is 45-90 degrees of unintended
            // rotation at full torque with the cube clamped.
            int currentVal = MotorEncoders[i]->scanChecked();

            if (currentVal < 0 && ++encFail[i] >= 2) {
                // Second consecutive failure: the mux may be wedged, which no
                // amount of retrying at this level will clear. KEEP the result
                // of the post-reset read — an earlier version discarded it and
                // `continue`d, so a marginal bus that recovered after a reset
                // never stepped, never cleared encFail[i], and burned the whole
                // alignment timeout before reporting ERR_ALIGN_TIMEOUT — a
                // dead solve for what was actually a flaky I2C line.
                resetEncoderMux();
                currentVal = MotorEncoders[i]->scanChecked();
            }

            if (currentVal < 0) {
                if (encFail[i] >= 2) {
                    cubeMotors.disableMotors();
                    pumpMotionOnly = wasMotionOnly;
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

                // Move toward target, one step per pass — November's rate.
                // One sign convention, decided once — see kAlignStepSign in
                // CubeSystem.h.
                pos[i] += (err > 0 ? kAlignStepSign : -kAlignStepSign) * stepSize;
            }
        }

        // Apply new positions
        cubeMotors.moveTo(pos);

        // Check for timeout
        if (millis() - t_start > timeout) {
            cubeMotors.disableMotors();
            pumpMotionOnly = wasMotionOnly;
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

    pumpMotionOnly = wasMotionOnly;   // hand the display back
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

    return alignMotorsInternal();
}



bool CubeSystem::getColorCalibration(){
    // Returns if color sensors are calibrated.
    //
    // NOTE: ColorSensor::loadCalibration() returns BOOL (true == calibrated),
    // unlike MotorEncoder::loadCalibration() which returns INT (0 == success).
    // Do not test it with `!= 0` the way getMotorCalibration() does — that
    // inverts the answer.
    //
    // Both sensors are loaded unconditionally — do not short-circuit with &&,
    // or sensor 2 never gets its calibration read into RAM when sensor 1 fails.
    bool cal1 = colorSensor1.loadCalibration();
    bool cal2 = colorSensor2.loadCalibration();

    return cal1 && cal2;
}

// Restore the in-RAM color calibration after a failed/aborted calibration.
//
// scanFace() returns early on abort leaving scanVals holding the PREVIOUS
// window, and setColorCal() has already written some of that into calVals[].
// Leaving EEPROM untouched is necessary but NOT sufficient: nothing reloads
// calVals, so every subsequent scan this power cycle classifies against a
// poisoned table while the user has been told "nothing was changed".
void CubeSystem::calibrationBail(int why) {
    Serial.print(F("Color calibration bailing, code ")); Serial.println(why);
    if (!colorSensor1.loadCalibration()) colorSensor1.resetCalibration();
    if (!colorSensor2.loadCalibration()) colorSensor2.resetCalibration();
    cubeMotors.disableMotors();
}

int CubeSystem::calibrateColorSensors(){
    if (!colorSensorsOk) {
        Serial.println(F("Cannot calibrate: a color sensor board failed to initialise"));
        return 90;
    }

    // ESTABLISH the machine's physical state; do not inherit it.
    //
    // Settings > Calibration > Color Sensors is ONE menu level from the clamped
    // rest state — the sketch closes all three grippers after a successful scan
    // and after a solve — and nothing on the path here releases the cube. A
    // cube still held by the ring sits at exactly this function's own
    // empty-chamber pose (STEP 2 lifts the cube there to take the 'E'
    // reference), so the four side reads below would file an empty chamber as
    // the R/G/B/O references, and nothing downstream can tell: the result is a
    // wrong calibration, persisted, that every later scan classifies against.
    //
    // unloadCube() also puts the TOP gripper somewhere known, which matters at
    // the ROTX in STEP 2 — see the note there.
    //
    // The model reset is not optional. Every reorientation below takes
    // executeMove()'s default moveVirtual = false, so the physical cube ends up
    // somewhere virtualCube does not know about. Without the reset isReady()
    // stays true, the root menu still reads "Cube Ready", and the next Solve
    // runs ~20 moves against a model the cube no longer matches.
    //
    // After the sensor-board guard on purpose: that return moves nothing, and
    // resetting the model for it would throw away a good scan over a call that
    // never touched the machine.
    unloadCube();
    virtualCube.resetCube();
    clearSolution();

    // Outputs (calibErrorText() in the sketch decodes these):
    //  0  - Success
    //  8  - Calibration computed but would not persist — machine NOT calibrated
    //  9  - Aborted by the user; EEPROM left untouched, cube released
    //  82 - A reorientation move failed (jam / align timeout / encoder fault);
    //       cube released, EEPROM left untouched
    //  90 - A color sensor board failed to initialise at boot (nothing moved)
    //
    // No 'cube not loaded' code: nothing on this machine can tell a loaded
    // chamber from an empty one before the calibration that would let it.

    // ------------ STEP 1 ------------
    // Scan first 4 sides

    // What each sensor is expected to be looking at, per rotation.
    //
    // These are COLOR labels, not positions: whatever sensor 1 returns is
    // filed as the first color and sensor 2's as the second, with no check
    // that the cube is actually turned that way. Getting it wrong writes a bad
    // calibration to EEPROM silently, so the required loading orientation is
    // part of the procedure — see CubeSystem::kCalStartFacelets, which the
    // panel shows before this runs.
    //
    // The positions named here are POST-rotation: the color scanner was turned
    // 90 degrees in CAD, so sensor 1 now reads Back and sensor 2 reads Right,
    // where they used to read Left and Back. The table itself did not have to
    // change for that — only the orientation the cube is loaded in.
    const char faceColors[4][2] = {
        {'R', 'G'}, // Red Back, Green Right
        {'B', 'R'}, // Blue Back, Red Right
        {'O', 'B'}, // Orange Back, Blue Right
        {'G', 'O'}  // Green Back, Orange Right
    };

    // Centre the cube in the chamber: the unloadCube() above got it DOWN here;
    // this pair only sweeps the bottom horn up to partial and back, squaring
    // the cube in the bay.
    botServoPartial();
    botServoRetract();

    // Chips filled so far, one bit per color per board. Both boards sample at
    // every rotation but on different colors, so they do not fill in step.
    uint8_t calBits[2] = { 0, 0 };

    // Scan the four side faces
    for (int rot = 0; rot < kCalSideRots; rot++) {

        char calMsg[48];
        snprintf(calMsg, sizeof(calMsg), "Side faces  (%d/%d)", rot + 1, kCalSideRots);
        displaySetMessage("Sampling side faces");
        displaySetStatus(calMsg);
        calBits[0] |= (uint8_t)(1u << kCalSideColors[rot][0]);
        calBits[1] |= (uint8_t)(1u << kCalSideColors[rot][1]);
        displayChips(calBits, 2);
        displayUpdate();

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
            if (cmv1) {
                // calibrationBail() restores the colour tables and kills torque
                // but it NEVER releases — it knows nothing about what the
                // machine is holding. The cube is up on the bottom gripper
                // here, so bailing without safeStop() parks a red screen on a
                // machine that is still holding it. Same idiom scanCube() uses
                // for its reorientation failures, including the reason the
                // abort is split out: a user asking the machine to stop must
                // surface AS an abort (9, "EEPROM left untouched"), not as a
                // jam the operator goes looking for.
                if (cmv1 == 20 + ERR_ABORTED) {
                    calibrationBail(ERR_ABORTED);
                    safeStop(ERR_ABORTED);
                    return 9;
                }
                Serial.print(F("Color calibration: side ROTZ failed, code ")); Serial.println(cmv1);
                calibrationBail(cmv1);
                safeStop(cmv1);
                return 82;
            }
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

    displaySetMessage("Sampling empty slot");
    displaySetStatus("Reference reading");
    displayUpdate();

    // Scan and set color calibration values for empty face
    colorSensor1.scanFace();
    colorSensor2.scanFace();
    for (int i = 0; i < 9; i++) {
        colorSensor1.setColorCal(i, 'E', colorSensor1.getScanValRow(i));
        colorSensor2.setColorCal(i, 'E', colorSensor2.getScanValRow(i));
    }

    // Rotate cube about x-axis and retract.
    //
    // The ring is at middle and the bottom horn is down, so the ring alone is
    // holding the cube through this move — which is also why a failure here is
    // the worst one in the function to return from without releasing.
    //
    // The top gripper is clear: nothing in this function extends it and the
    // unloadCube() at the top retracts it, so the ring turns the cube with the
    // up face free — the same condition scanCube() gives its own ROTX. From
    // the clamped rest state the top servo would still be socketed on the up
    // face and this move would fight it.
    int cmv2 = executeMove("ROTX");
    if (cmv2) {
        if (cmv2 == 20 + ERR_ABORTED) {
            calibrationBail(ERR_ABORTED);
            safeStop(ERR_ABORTED);
            return 9;
        }
        Serial.print(F("Color calibration: ROTX failed, code ")); Serial.println(cmv2);
        calibrationBail(cmv2);
        safeStop(cmv2);
        return 82;
    }
    botServoExtend();
    pumpDelay(servoDelay);
    ringRetract();
    botServoRetract();
    pumpDelay(servoDelay);

    // ------------ STEP 3 ------------
    // Scan remaining top/bottom faces

    // Same again for the top and bottom faces, after the re-grip above.
    // Sensor 1 reads Back, sensor 2 reads Right.
    const char topFaces[4][2] = {
        {'Y', 'O'}, // Yellow Back, Orange Right
        {'R', 'Y'}, // Red Back, Yellow Right
        {'W', 'R'}, // White Back, Red Right
        {'O', 'W'}  // Orange Back, White Right
    };
    
    // Scan the remaining top/bottom faces
    for (int rot = 0; rot < kCalTopRots; rot++) {
        char calMsg[48];
        snprintf(calMsg, sizeof(calMsg), "Top and bottom  (%d/%d)", rot + 1, kCalTopRots);
        displaySetMessage("Sampling top and bottom");
        displaySetStatus(calMsg);
        calBits[0] |= (uint8_t)(1u << kCalTopColors[rot][0]);
        calBits[1] |= (uint8_t)(1u << kCalTopColors[rot][1]);
        displayChips(calBits, 2);
        displayUpdate();

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
            if (cmv3) {
                // As the STEP 1 ROTZ — see the reasoning there. The cube is up
                // on the bottom gripper again at this point.
                if (cmv3 == 20 + ERR_ABORTED) {
                    calibrationBail(ERR_ABORTED);
                    safeStop(ERR_ABORTED);
                    return 9;
                }
                Serial.print(F("Color calibration: top/bottom ROTZ failed, code ")); Serial.println(cmv3);
                calibrationBail(cmv3);
                safeStop(cmv3);
                return 82;
            }
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
        Serial.println(F("Color calibration aborted - EEPROM left untouched"));
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
        Serial.print(F("ERROR: color calibration failed to save ("));
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

void CubeSystem::topServoEject()
{
    // Same shape as the others, and today the same movement as
    // topServoPartial() — the top servo has no pinned Eject position for
    // ejectTarget() to use. See the declaration for why it exists anyway.
    topServo.eject();
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

void CubeSystem::botServoEject()
{
    botServo.eject();
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
    //  0 - Ran succesfully
    //  1X - Virtual move failed (X is error thrown by virtual move)
    //  2X - Home motors failed
    //
    // THIS IS NOVEMBER'S STRUCTURE, ON PURPOSE — restored after two rounds of
    // "smarter" move verification each broke solves that November completed:
    //
    //   - A retry ladder (arrival check -> selective align -> backout ->
    //     re-home, x3) whose selective alignment ran after EVERY move and
    //     whose corrections were the jerky micro-stepping the motor rework
    //     removed.
    //   - A pre-move drift gate that hard-failed the solve when readings from
    //     de-energised motors wandered around the tolerance edge — it aborted
    //     solves on a machine that was visibly square, with homeMotors() and
    //     the gate disagreeing about the same six motors seconds apart.
    //
    // November's contract is simpler and proven across full solves: turn the
    // face open loop, update the model, then CHECK — and if the check fails,
    // home everything to the nearest marks once. Only a homing that itself
    // fails stops the solve. Sporadic encoder noise costs one homing pass, not
    // a dead solve. Do not add gates or ladders here again without bench time.

    // Turn real cube side
    cubeMotors.executeMove(move);

    // Turn Virtual Cube
    int result = 0;
    if (moveVirtual) {
        result = virtualCube.executeMove(move);
        if (result) return 10 + result;
    }

    // If alignment is active and motor is misaligned
    result = 0;
    if (align && !checkAlignment()) {
        result = homeMotors();
        if (result) return 20 + result;
    }

    return 0;   // Success
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

void CubeSystem::displayFaces(const int8_t* faces, int activeA, int activeB) {
    if (displayInitialized) {
        cubeDisplay.setOpFaces(faces, activeA, activeB);
    }
}

void CubeSystem::displayChips(const uint8_t* bits, int boards) {
    if (displayInitialized) {
        cubeDisplay.setOpChips(bits, boards);
    }
}

void CubeSystem::displayProgress(int done, int total) {
    if (displayInitialized) {
        cubeDisplay.setOpProgress(done, total);
    }
}

void CubeSystem::displayUpdate() {
    if (displayInitialized) {
        cubeDisplay.update();
    }
}

void CubeSystem::displayWaitForSelect(const char* msg) {
    // A user gate must never become a no-op. Every "press SELECT to continue"
    // prompt routes through here — including the ones immediately before the
    // servos clamp the cube and before executeSolve() — so a display that
    // failed to initialise (a loose ribbon next to six vibrating steppers)
    // must fall through to another input, not return instantly and let the
    // machine clamp and solve unattended.
    //
    // Gate on the encoder as well as the display: cubeDisplay.waitForSelect()
    // spins on menuEncoder.selectPressed() with no Serial escape, so with a
    // working display and a DEAD seesaw it would wedge forever.
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

    // No display AND no encoder: Serial is the only way through.
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

    // Ring -> top -> bottom. NOT the mirror of the load sequence (load extends
    // bottom -> ring -> top; unload retracts the ring first, while the top
    // servo is still extended), so the safe order cannot be inferred at a
    // call site — it lives here once. The first two steps are the shared
    // partial release, so there is ONE copy of that order; its own abort
    // suppression nests harmlessly inside this one.
    unloadCubeKeepBottom();
    botServoRetract();      // the step the partial release deliberately omits

    pumpAbortSuppressed = wasSuppressed;
    chordHeldSince = 0;     // a still-held chord must be released to re-abort
}

void CubeSystem::unloadCubeKeepBottom() {
    // Suppressed for the same reason unloadCube() suppresses: this runs on
    // success paths (the post-solve display spin) with the cube still held.
    const bool wasSuppressed = pumpAbortSuppressed;
    pumpAbortSuppressed = true;

    // The front of unloadCube()'s order, minus botServoRetract() — the cube
    // stays up on the bottom gripper, which is the entire point.
    ringRetract();
    topServoRetract();

    pumpAbortSuppressed = wasSuppressed;
    chordHeldSince = 0;     // a still-held chord must be released to re-abort
}

void CubeSystem::safeStop(int faultCode) {
    // Put the machine into a state that is safe to leave unattended.
    lastFault = faultCode;

    // The unload must not be pumped-out: abortRequested is a LATCH, and while
    // it is set pumpDelay() returns immediately, so every servo sweep in
    // unloadCube() would issue one servo.write() and bail — cube still
    // clamped while this prints "halted safely". Suspend the latch AND the
    // detector (see pumpAbortSuppressed in CubeSystem.h: the buttons are still
    // down from the 1.5 s hold) for the unload, then restore the latch so the
    // caller and the UI still see that an abort happened.
    const bool wasAborted = abortRequested;
    pumpAbortSuppressed = true;
    abortRequested = false;
    chordHeldSince = 0;

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
    // chordHeldSince is zeroed so a still-held chord must be released and
    // held again to raise a new abort, rather than re-arming ~1.5 s from now.
    pumpAbortSuppressed = false;
    chordHeldSince = 0;
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
    //       14 - A corner or edge is not a real cubie -> a COLOR was misread,
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
    //  1XX - executeMove() failed at a move (XX is its code); cube released,
    //        model invalidated. 105 = aborted between moves, 125 = aborted
    //        inside a move's re-homing.

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
        // A 21-move solve is the longest the machine does anything for without
        // saying so. The bar is what makes "nearly finished" readable from
        // across the room; the move name is for the bench.
        char moveMsg[48];
        snprintf(moveMsg, sizeof(moveMsg), "Move %d/%d   %s",
                 i + 1, solutionLength, solveMoves[i].c_str());
        displaySetStatus(moveMsg);
        displayProgress(i, solutionLength);

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
            // A failed move still turned the PHYSICAL cube (every executeMove
            // error return fires after the physical move has run), and a 2X
            // means the machine could not even home afterwards — so real cube
            // and model can no longer be trusted to agree. Leaving
            // solveMoves[] and cubeReady intact meant a caller could invoke
            // executeSolve() again, restart at index 0, and replay the entire
            // solution onto a cube that was already partway through it —
            // scrambling it and risking a hard jam.
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
