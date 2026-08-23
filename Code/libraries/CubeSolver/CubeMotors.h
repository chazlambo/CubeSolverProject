#ifndef CUBEMOTORS_H
#define CUBEMOTORS_H

#include <Arduino.h>
#include <AccelStepper.h>
#include <MultiStepper.h>
#include <EEPROM.h>


class CubeMotors {
public:
    // Constructor
    CubeMotors(int enpin, int step_pins[7], int dir_pins[7], int ringStateEEPROMAddress);

    // Cube Moves
    enum CubeMove {
        MOVE_U, MOVE_Up, MOVE_U2,
        MOVE_R, MOVE_Rp, MOVE_R2,
        MOVE_F, MOVE_Fp, MOVE_F2,
        MOVE_D, MOVE_Dp, MOVE_D2,
        MOVE_L, MOVE_Lp, MOVE_L2,
        MOVE_B, MOVE_Bp, MOVE_B2,
        MOVE_ROT_X, MOVE_ROT_Z, MOVE_ALL,
        MOVE_INVALID
    };

    // Public methods
    void begin();
    void enableMotors();
    void disableMotors();
    void homeRingStepper(AccelStepper &ringStep);
    void ringMove(int state);
    void ringToggle();
    long getPos(int posIdx);

    // Turn ONE face motor continuously at a constant rate, for the post-solve
    // display screen. Not for cube moves — there is no arrival position, no
    // coordination with the other five and no alignment check.
    //
    // Why it exists: everything else here goes through MultiStepper, which
    // moves at the speed needed to arrive together and then stops. Asking it
    // for a few steps at a time gives a few MILLISECONDS of motion followed by
    // a long wait — the display spin was 4 steps at full speed every 80 ms,
    // motion 5% of the time, which reads as a stutter rather than a turn.
    // runSpeed() instead emits one evenly-spaced step whenever the next one is
    // due, so the rate is set by the clock and not by how often it is called.
    //
    // spinService() must be called often — every pass of the caller's loop.
    // spinEnd() writes the motor's real position back into the pos[] this
    // class hands to MultiStepper; skip it and the next cube move starts from
    // a stale idea of where D is. It does NOT drop torque: the caller owns
    // that, because letting go mid-spin gives the cube's momentum to the
    // nearest detent and loses the count the squaring depends on.
    void spinBegin(int motorIdx, float stepsPerSec);
    long spinService();                 // steps emitted since spinBegin()
    void spinEnd();
    void moveTo(long newPos[6]);
    void resetMotorPos();
    void executeMove(String moveString);

    // --- motion tuning -------------------------------------------------------
    // Changing one takes effect on the very next move. That is what makes
    // tuning them from a settings screen worth doing at all — you turn the
    // wheel, then turn a face, and see the difference immediately.
    //
    // stepDelay, rotDelay and the ring positions manage that by being READ at
    // the point of use. The three SPEEDS cannot: AccelStepper caches them, so
    // the setters push them into the steppers — and into all six face motors
    // at once. They are the only path: tuneLoadAll() runs after begin(), so a
    // stored value never reaches the stepper through initialisation either.
    // MultiStepper takes each move's speed from each stepper's own maxSpeed and
    // does not ramp, so a tuned ceiling on one motor (D, after spinEnd()
    // restores it) with the other five still at 1000 is an unramped D-only
    // turn and a stall that shows up only after an idle screen.
    //
    // Every setter clamps. These drive real steppers, and a zero speed is a
    // machine that hangs mid-move with the grippers closed.
    int  getTurnStep()     const { return turnStep; }
    int  getStepSpeed()    const { return stepSpeed; }
    int  getStepAccel()    const { return stepAccel; }
    int  getStepDelay()    const { return defaultStepDelay; }
    int  getRotStepDelay() const { return rotStepDelay; }
    int  getRingSpeed()    const { return ringStepSpeed; }
    int  getRingAccel()    const { return ringStepAccel; }
    int  getEnableDwell()  const { return enableDwellMs; }

    // --- acceleration ramp ---------------------------------------------------
    // 0 = no ramp: the MultiStepper constant-speed move the machine has always
    // made, byte for byte. Anything else is steps/s^2 and routes face moves
    // through runRamped() below, which accelerates from rest and decelerates to
    // rest on every participating motor.
    //
    // The number has to be LARGE to be useful. AccelStepper's first step
    // interval is c0 = 0.676 * sqrt(2 / accel): at 2000 the first step alone
    // waits 21 ms and reaching 1000 steps/s takes 250 steps — a quarter turn
    // never cruises. At 20000-25000 the ramp is ~20 steps each end and costs
    // ~40 ms on a quarter turn; at 100000 it is a 5-step ramp, effectively the
    // unramped move with the start and stop taken off. The ceiling exists so
    // a tuning screen cannot ask for a ramp shorter than one step.
    void setStepAccel(int v) { stepAccel = clampTune(v, 0, 200000); }

    // --- hold torque across a move train -------------------------------------
    // Between moves the face motors are de-energised, so five fingers hold
    // their layers with detent torque alone while the sixth turns. holdBegin()
    // makes disableMotors() a no-op until holdEnd(), so a caller that owns a
    // sequence of moves (a solve, a scramble, a bench run) can keep every
    // finger energised from its first move to its last. The enable pin is
    // shared by all seven drivers, so this is all-or-nothing by construction.
    //
    // A bool, not a refcount: nothing nests, and a counter that leaks leaves
    // six motors energised on a clamped cube behind an error screen. Every
    // failure path that releases the cube must call holdEnd() first.
    void holdBegin() { motHeld = true; enableMotors(); }
    void holdEnd()   { motHeld = false; disableMotors(); }
    bool isHeld() const { return motHeld; }

    // Milliseconds to wait after a real off->on transition of the enable pin
    // before any step is issued. A rotor that was pushed while de-energised
    // snaps to the driver's phase when power returns; stepping during the
    // snap is a way to lose sync on the first step. 0 = today's behaviour
    // (no wait). Exists for the bench to measure whether it matters; it is not
    // on the tuning screen.
    void setEnableDwell(int ms) { enableDwellMs = clampTune(ms, 0, 200); }

    // Steps per quarter turn is READ-ONLY on purpose, and there is no setter to
    // add later without thinking about this: it is a property of the gearing,
    // not a preference. A wrong value does not make solves worse, it makes them
    // impossible — every face ends up mis-indexed and the cube jams. It belongs
    // in the source next to the pin assignments, changed once per machine by
    // someone holding a calculator, not on a screen next to the ring speed.
    void setStepSpeed(int v)    { stepSpeed        = clampTune(v, 50, 5000); applyStepSpeed(); }
    void setStepDelay(int v)    { defaultStepDelay = clampTune(v, 0,  500);  }
    void setRotStepDelay(int v) { rotStepDelay     = clampTune(v, 0,  500);  }
    void setRingSpeed(int v)    { ringStepSpeed    = clampTune(v, 50, 5000); applyRingMotion(); }
    void setRingAccel(int v)    { ringStepAccel    = clampTune(v, 50, 5000); applyRingMotion(); }

    // Where the ring actually is, in the same state numbering ringMove() takes:
    //
    //   0 = retracted, 1 = halfway, 2 = extended, 3 = partial, -1 = in motion
    //
    // Exists so a diagnostics page can open showing the real position instead
    // of "?" until the operator moves something. The state is persisted to
    // EEPROM across a reset and initRingStepper() has already reconciled the
    // carriage with it by the time anything can ask, so it is worth trusting.
    //
    // -1 is the "move started, has not finished" sentinel ringMove() writes
    // before it travels; seeing it means power was lost mid-move and the ring
    // has not been re-homed yet, so show it as unknown rather than picking a
    // position.
    int  getRingState()      const { return ringState;      }

    // Ring positions, in steps from the retracted position homeRingStepper()
    // establishes. Unlike the speeds these are mechanical: the ring has to
    // clear the cube at retract and grip it at extend, and getting one wrong
    // drives the carriage into a hard stop.
    // 2000 is well past the travel and exists only to stop a runaway, not to
    // describe the machine — the useful range is far narrower and is what the
    // tuning screen offers.
    int  getRingRetPos()     const { return ringRetPos;     }
    int  getRingPartialPos() const { return ringPartialPos; }
    int  getRingHalfPos()    const { return ringHalfPos;    }
    int  getRingExtPos()     const { return ringExtPos;     }

    void setRingRetPos(int v)     { ringRetPos     = clampTune(v, 0, 2000); }
    void setRingPartialPos(int v) { ringPartialPos = clampTune(v, 0, 2000); }
    void setRingHalfPos(int v)    { ringHalfPos    = clampTune(v, 0, 2000); }
    void setRingExtPos(int v)     { ringExtPos     = clampTune(v, 0, 2000); }

private:
    static int clampTune(int v, int lo, int hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // Private helper methods
    void initStepper(MultiStepper &multiStepper, AccelStepper &newStepper);
    void initRingStepper(AccelStepper &ringStep);

    CubeMove parseMove(const String& move);
    AccelStepper* stepperFor(int idx);   // U R F D L B, or nullptr

    // Push the cached speeds into the steppers. All six face motors together —
    // a tuned ceiling on one of them and not the others is a stall waiting for
    // whichever move happens to use it.
    void applyStepSpeed();
    void applyRingMotion();

    // Ramped equivalent of multiStep.moveTo(pos)+runSpeedToPosition(): every
    // stepper whose target differs from where it stands accelerates to
    // stepSpeed and decelerates to rest. Participants stay in lockstep because
    // a face move gives them all the same distance — one face, or two (ROTX,
    // ROTZ) or six (ALL) at exactly one quarter turn each — so identical
    // profiles emit identical step trains.
    void runRamped();

private:
    // Stepper Objects
    AccelStepper upStepper;
    AccelStepper rightStepper;
    AccelStepper frontStepper;
    AccelStepper downStepper;
    AccelStepper leftStepper;
    AccelStepper backStepper;
    AccelStepper ringStepper;
    MultiStepper multiStep;

    // Motor Pins
    int EN_PIN;
    int STEP_PINS[7];
    int DIR_PINS[7];
    int ringStateEEPROMAddress;

    // Position and State Variables
    long pos[6] = {0, 0, 0, 0, 0, 0};
    int expectedCalIndex[6] = {0,0,0,0,0,0};
    int turnStep = 100;

    int stepSpeed = 1000;
    int stepAccel = 0;          // 0 = constant speed (see setStepAccel)
    // Bench-proven: solves were consistent at 20 ms, and it is tunable from the
    // Parameters screen if a face ever overshoots.
    int defaultStepDelay = 20;
    int rotStepDelay = 60;
    int enableDwellMs = 0;

    bool motEnableState = false;
    bool motHeld = false;

    int ringPos = 0;
    int ringStepSpeed = 800;
    int ringStepAccel = 400;
    int ringRetPos = 0;
    int ringPartialPos = 200;
    int ringHalfPos = 450;
    int ringExtPos = 800;
    int ringState = 0;

    // Continuous-spin state; see spinBegin().
    AccelStepper* spinStepper = nullptr;
    int  spinIdx  = -1;
    long spinBase = 0;
    
};

#endif
