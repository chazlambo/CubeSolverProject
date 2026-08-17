#include "CubeServo.h"

CubeServo::CubeServo(int pin, int eepromAddr, unsigned int retPos, unsigned int extPos, int sweepDelay)
  : pin(pin), eepromAddr(eepromAddr), currentPos(retPos), retPos(retPos), extPos(extPos), extState(-1), sweepDelay(sweepDelay) {}

unsigned int CubeServo::partialTarget() const {
    // A pinned value wins. Otherwise 3/4 of the way from retracted to extended.
    //
    // The old inline expression was 3*(retPos+extPos)/4, which only equals this
    // when retPos == 0. That happens to hold today (both servos retract to 0),
    // so it was latent rather than live — but it is wrong in general and would
    // silently mis-position the horn the moment a non-zero retract position is
    // used. Signed arithmetic so an inverted ext/ret pair doesn't underflow.
    if (partialExplicit) return partialPos;
    return (unsigned int)((int)retPos + 3 * ((int)extPos - (int)retPos) / 4);
}

// Unpinned, ejecting is exactly what partial() always did. That is deliberate:
// the machines that came before this had one position for both, and a servo
// nobody has tuned must keep doing what it did.
unsigned int CubeServo::ejectTarget() const {
    return ejectExplicit ? ejectPos : partialTarget();
}

void CubeServo::begin() {
    servo.attach(pin);
    loadStateFromEEPROM();      // restores extState AND currentPos

    // currentPos is persisted both before and after every sweep, so:
    //   - after a clean shutdown it is exact
    //   - after power loss mid-sweep it holds the position the sweep started
    //     from, bounding the error to one sweep's travel
    //
    // This replaces the old behaviour, which reconstructed currentPos from the
    // coarse state alone using (extPos + retPos) / 2. For the "partially
    // retracted" state that guessed 130 while partial() had actually parked the
    // horn at 195, so the first servo.write() after a power cycle commanded an
    // instantaneous ~43 degree jump — with the linkage possibly engaged with
    // the cube. sweepTo() begins by writing the ASSUMED current angle, so an
    // inaccurate currentPos is a slam, not a slow correction.
    //
    // Note a genuinely unknown position cannot be recovered without feedback.
    // PWMServo emits no pulse until the first write(), so the horn is limp
    // until then and some movement on the first command is unavoidable.

    // Fallback when the stored position is absent or corrupt (0xFFFF on the
    // first boot after updating from firmware that only stored extState).
    //
    // It MUST be the value that guarantees a full sweep. Falling back to retPos
    // means sweepTo(retPos) is called with currentPos already == retPos, so both
    // loops are skipped and the only thing issued is the endpoint write added at
    // the bottom of sweepTo() — the horn jumps straight to the target from
    // wherever it physically is, with EEPROM then stamped "retracted".
    // (Before sweepTo() wrote its endpoint, the same fallback issued NOTHING and
    // left the cube clamped; the mechanism changed, the wrong answer did not.)
    // Assume the worst case per known state instead.
    if (currentPos > 270) {
        switch (extState) {
            case 0:  currentPos = retPos;          break;   // no sweep needed anyway
            case 1:  currentPos = extPos;          break;
            case 2:  currentPos = partialTarget(); break;
            default: currentPos = extPos;          break;   // unknown -> assume extended
        }
    }

    switch (extState) {
        case 0:     // Retracted — trust the persisted position, leave the servo limp
            break;

        case 1:     // Extended
        case 2:     // Partially retracted
        default:    // Unknown (power lost mid-sweep)
            retract();              // Always start in retracted position
            break;
    }
}

void CubeServo::extend() {
    // Set to unknown state temporarily
    extState = -1;
    updateEEPROM();

    // Sweep servo to desired position.
    // On an aborted sweep the horn is somewhere mid-travel; sweepTo() has
    // already recorded where it actually got to. Persist THAT with the state
    // left unknown rather than claiming the servo arrived — recording a
    // position it never reached is what arms a full-travel slam on the next move.
    if (!sweepTo(extPos)) { updateEEPROM(); return; }
    currentPos = extPos;

    // Update state
    extState = 1;
    updateEEPROM();
}

void CubeServo::partial(){
    // Set to unknown state temporarily
    extState = -1;
    updateEEPROM();

    // Sweep servo to desired position
    unsigned int target = partialTarget();
    if (!sweepTo(target)) { updateEEPROM(); return; }   // aborted — state stays unknown
    currentPos = target;

    // Update state
    extState = 2;
    updateEEPROM();
}

void CubeServo::eject() {
    // Set to unknown state temporarily
    extState = -1;
    updateEEPROM();

    unsigned int target = ejectTarget();
    if (!sweepTo(target)) { updateEEPROM(); return; }   // aborted — state stays unknown
    currentPos = target;

    // State 2, the same as partial(). The distinction between "presenting" and
    // "partially retracted" matters to the operator and not at all to begin(),
    // which retracts out of either one.
    extState = 2;
    updateEEPROM();
}

void CubeServo::retract() {
    // Set to unknown state temporarily
    extState = -1;
    updateEEPROM();

    // Sweep servo to desired position
    if (!sweepTo(retPos)) { updateEEPROM(); return; }   // aborted — state stays unknown
    currentPos = retPos;

    // Update state
    extState = 0;
    updateEEPROM();
}

void CubeServo::toggle() {
    if(extState==1) {  // If Extended
    retract();  // Retract
  }
  else {                // If not extended
    extend();   // Extend
  }
}

bool CubeServo::isExtended() {
    return extState == 1;
}

// --- position tuning -------------------------------------------------------
// 270 is the servo's full range and the same ceiling begin() uses to spot a
// corrupt stored position. Clamping here rather than trusting the caller means
// a settings screen cannot command the horn past its stop, whatever range it
// thinks it is offering.
static unsigned int clampServoPos(long pos) {
    if (pos < 0)   return 0;
    if (pos > 270) return 270;
    return (unsigned int)pos;
}

void CubeServo::setRetracted(unsigned int pos) { retPos = clampServoPos((long)pos); }
void CubeServo::setExtended(unsigned int pos)  { extPos = clampServoPos((long)pos); }

// Pinning is one-way on purpose. There is no "unset" because there is no
// gesture for it on the panel, and a value that silently reverted to being
// derived the next time the extend position moved would be worse than either.
void CubeServo::setPartial(unsigned int pos) {
    partialPos      = clampServoPos((long)pos);
    partialExplicit = true;
}

void CubeServo::setEject(unsigned int pos) {
    ejectPos      = clampServoPos((long)pos);
    ejectExplicit = true;
}

void CubeServo::setSweepStepDelay(int ms) {
    // Zero would turn every sweep into a slam, and the sweep is the only thing
    // keeping the linkage from snatching at the cube. One millisecond is still
    // absurdly fast; it is a floor, not a recommendation.
    if (ms < 1)   ms = 1;
    if (ms > 100) ms = 100;
    sweepDelay = ms;
}

void CubeServo::previewRaw(unsigned int pos) {
    const unsigned int target = clampServoPos((long)pos);

    // Same 270 -> 180 mapping sweepTo() uses. Writing the raw 0-270 value
    // straight to PWMServo would put the horn at two thirds of the angle asked
    // for, which reads as a broken linkage rather than a wrong number.
    servo.write(map(target, 0, 270, 0, 180));

    // Track where the horn now is. sweepTo() begins by assuming currentPos is
    // accurate; leaving it stale here would make the next extend() or retract()
    // start its sweep from a position the horn is nowhere near.
    currentPos = target;
    extState   = -1;        // neither endpoint any more
}

bool CubeServo::sweepTo(unsigned int newPos) {

    // Map 270 degree range to function that takes 180 degree input
    unsigned int currentAngle = map(currentPos, 0, 270, 0, 180);
    unsigned int newAngle = map(newPos, 0, 270, 0, 180);

    // pumpDelay() instead of delay() so the display keeps refreshing and button
    // presses are latched during the sweep. A full bottom-servo travel is 173
    // steps at 15 ms = ~2.6 s, and there are several per scan — this is one of
    // the biggest single contributors to the UI freeze.
    //
    // currentPos is updated as we go so that an abort (or a power loss) leaves
    // a position estimate that reflects where the horn actually got to, not
    // where the sweep started.
    if(currentAngle < newAngle) {                               // If servo needs to go forwards
        for(unsigned int i=currentAngle; i < newAngle; i++) {   // Sweep through all angles in between current and desired position
        servo.write(i);                                         // Write servo to new angle
        currentPos = map(i, 0, 180, 0, 270);
        if (!pumpDelay(sweepDelay)) return false;               // aborted mid-sweep
        }
    }
    else if(currentAngle > newAngle) {                          // If servo needs to go backwards
        for(unsigned int i=currentAngle; i > newAngle; i--) {   // Sweep through all angles in between current and desired position
        servo.write(i);                                         // Write servo to new angle
        currentPos = map(i, 0, 180, 0, 270);
        if (!pumpDelay(sweepDelay)) return false;               // aborted mid-sweep
        }
    }

    // Command the endpoint itself. Both loops stop one step short (`i < newAngle`
    // / `i > newAngle`), so without this the horn is left ~1.5 degrees off while
    // the caller records the exact target — and since currentPos is now
    // persisted and trusted on the next boot, that error would accumulate.
    servo.write(newAngle);
    currentPos = newPos;

    return true;    // reached the target
}

// EEPROM layout for a servo. initializeEEPROMLayout() reserves sizeof(int) == 4
// bytes per servo and only one was ever used, so the commanded position fits in
// the spare bytes without changing the layout or invalidating existing data.
//
//   +0        int8   extState  (-1 unknown, 0 retracted, 1 extended, 2 partial)
//   +1 .. +2  uint16 currentPos (0-270)
//   +3        unused
void CubeServo::loadStateFromEEPROM() {
    extState = (int8_t)EEPROM.read(eepromAddr);   // signed: -1 must read back as -1

    // Read the two position bytes explicitly rather than via EEPROM.get(), so
    // this mirrors updateEEPROM() exactly and does not depend on host endianness.
    // Never-written EEPROM reads 0xFF, giving 65535, which begin() rejects.
    uint8_t lo = EEPROM.read(eepromAddr + 1);
    uint8_t hi = EEPROM.read(eepromAddr + 2);
    currentPos = (unsigned int)(((uint16_t)hi << 8) | lo);
}

void CubeServo::updateEEPROM() {
    EEPROM.update(eepromAddr, (uint8_t)(int8_t)extState);

    // update() rather than put() so an unchanged position costs no write.
    uint16_t stored = (uint16_t)currentPos;
    uint8_t  lo = (uint8_t)(stored & 0xFF);
    uint8_t  hi = (uint8_t)(stored >> 8);
    EEPROM.update(eepromAddr + 1, lo);
    EEPROM.update(eepromAddr + 2, hi);
}
