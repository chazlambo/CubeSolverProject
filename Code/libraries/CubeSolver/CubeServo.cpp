#include "CubeServo.h"

CubeServo::CubeServo(int pin, int eepromAddr, unsigned int retPos, unsigned int extPos, int sweepDelay)
  : pin(pin), eepromAddr(eepromAddr), currentPos(retPos), retPos(retPos), extPos(extPos), extState(-1), sweepDelay(sweepDelay) {}

unsigned int CubeServo::partialTarget() const {
    // A pinned value wins. Otherwise 3/4 of the way from retracted to extended:
    // retPos plus 3/4 of the span, NOT 3*(retPos+extPos)/4, which only agrees
    // when retPos == 0. Signed arithmetic so an inverted ext/ret pair doesn't
    // underflow.
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
    // Guessing it from the coarse state instead (e.g. the midpoint, 130, for
    // "partial" when partial() parks the horn at 195) makes the first
    // servo.write() after a power cycle an instantaneous ~43 degree jump, with
    // the linkage possibly engaged with the cube: sweepTo() begins by writing
    // the ASSUMED current angle, so an inaccurate currentPos is a slam, not a
    // slow correction.
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
    // Assume the worst case per known state instead.
    if (currentPos > 270) {
        switch (extState) {
            case 0:  currentPos = retPos;          break;   // no sweep needed anyway
            case 1:  currentPos = extPos;          break;
            case 2:  currentPos = partialTarget(); break;
            case 3:  currentPos = ejectTarget();   break;   // untuned, so == partialTarget()
            // Unknown means power was lost MID-SWEEP — extState is stamped -1
            // before every sweep — so the horn is somewhere between the two
            // endpoints and nothing knows where. The midpoint is the right
            // guess: whichever way the first retract() then sweeps, it travels
            // at most half the range at 15 ms a step.
            //
            // Assuming extPos instead would be strictly worse. An
            // interrupted RETRACT stores full clamp, and the first PWM pulse
            // throws the horn to 205/260 in one write onto a cube still
            // between the grippers. An interrupted EXTEND stores retPos,
            // sweepTo() skips both loops as already-there, and the endpoint
            // write snaps the horn fully open — the cube drops. A virgin or
            // erased EEPROM closes both grippers at boot.
            default: currentPos = (extPos + retPos) / 2; break;
        }
    }

    switch (extState) {
        case 0:     // Retracted — trust the persisted position, leave the servo limp
            break;

        case 1:     // Extended
        case 2:     // Partially retracted
        case 3:     // Ejected — presenting a cube, so certainly still holding one
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

    // State 3 — distinct from partial (2) only so a page can name the pose;
    // begin() treats both alike. See coarseState() in the header.
    extState = 3;
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
    // Tests for the extended endpoint rather than listing the others, so the
    // added ejected state (3) needs nothing here: toggle is a two-position
    // gesture, and everything that is not extended — retracted, partial,
    // ejected, unknown — extends, as it did when eject() recorded 2.
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

// The largest preview that is still allowed to be instant, in the 0-270 domain.
//
// Wheel-following MUST stay instant — a sweep at 15 ms a step cannot follow a
// wheel, which is the whole reason previewRaw() exists. But the FIRST preview
// after entering a tuning row is not a follow: the editor seeds its working
// value from the STORED ENDPOINT, not from where the horn is standing, so the
// opening detent on Top Servo > Retract asks for 1 degree while the horn is
// parked at 205 with the ring and the bottom gripper still closed on the cube.
// That is exactly the instantaneous full-travel slam begin() guards against on
// the boot path, arriving through a different door.
//
// Eight degrees tells the two apart:
//   - A wheel cannot exceed it. Every servo row in CubeTuneTable.cpp steps by
//     1 degree, and the editor collapses a burst of detents into ONE step
//     however fast the wheel is spun, so a genuine follow is always 1. Eight
//     leaves room for a servo row that later adopts the coarser step the ring
//     rows use (5) without collapsing every detent into a sweep.
//   - It cannot jam anything. Eight degrees is 3% of full travel — five steps
//     of the 180-degree range the horn is actually commanded in — a nudge
//     wherever in the linkage it happens. The travels this catches are the
//     205 and 260 degree ones.
//
// Anything larger is the first preview of a session, or a discard putting a
// part back where it started, or something else with no business slamming a
// horn: all of them are moves, and moves sweep.
static const unsigned int kPreviewInstantSpan = 8;

void CubeServo::previewRaw(unsigned int pos) {
    const unsigned int target = clampServoPos((long)pos);

    // Unknown BEFORE the move, not after. From here the horn is at neither
    // endpoint, and the sweep below pumps the UI — so a page repainted
    // mid-travel must not still be reading "retracted". No updateEEPROM()
    // beside it: a preview costs no write, per detent or otherwise.
    extState = -1;

    const long delta  = (long)target - (long)currentPos;
    const long travel = delta < 0 ? -delta : delta;

    if (travel > (long)kPreviewInstantSpan) {
        // Too far to be a wheel detent, so move it like a move. sweepTo()
        // updates currentPos as it steps and returns false if the abort chord
        // cut it short; either way currentPos already says where the horn
        // actually got to, so there is nothing to record here — and nothing
        // MAY be recorded, because the horn may not have arrived.
        sweepTo(target);
        return;
    }

    // Same 270 -> 180 mapping sweepTo() uses. Writing the raw 0-270 value
    // straight to PWMServo would put the horn at two thirds of the angle asked
    // for, which reads as a broken linkage rather than a wrong number.
    servo.write(map(target, 0, 270, 0, 180));

    // Track where the horn now is. sweepTo() begins by assuming currentPos is
    // accurate; leaving it stale here would make the next extend() or retract()
    // start its sweep from a position the horn is nowhere near.
    currentPos = target;
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
    // An abort STOPS THE OPERATION, it does not stop the horn. Returning the
    // instant pumpDelay() reports the chord would leave the horn mid-travel
    // while the caller acts on the abort — and in a scan reorientation the very
    // next thing is ringMiddle(), whose ~450 steps are deliberately NOT
    // abortable, so the ring would close at the grip plane on a cube still
    // sitting below it. So finish the travel at the same rate on plain delay()
    // — the abort is already latched, and pumping again would return instantly
    // — then report it. The mechanism is settled before the caller unwinds.
    bool aborted = false;
    if(currentAngle < newAngle) {                               // If servo needs to go forwards
        for(unsigned int i=currentAngle; i < newAngle; i++) {   // Sweep through all angles in between current and desired position
        servo.write(i);                                         // Write servo to new angle
        currentPos = map(i, 0, 180, 0, 270);
        if (aborted)                        delay(sweepDelay);
        else if (!pumpDelay(sweepDelay))    aborted = true;
        }
    }
    else if(currentAngle > newAngle) {                          // If servo needs to go backwards
        for(unsigned int i=currentAngle; i > newAngle; i--) {   // Sweep through all angles in between current and desired position
        servo.write(i);                                         // Write servo to new angle
        currentPos = map(i, 0, 180, 0, 270);
        if (aborted)                        delay(sweepDelay);
        else if (!pumpDelay(sweepDelay))    aborted = true;
        }
    }

    // Command the endpoint itself. Both loops stop one step short (`i < newAngle`
    // / `i > newAngle`), so without this the horn is left ~1.5 degrees off while
    // the caller records the exact target — and since currentPos is now
    // persisted and trusted on the next boot, that error would accumulate.
    servo.write(newAngle);
    currentPos = newPos;

    // false still means "the operator asked to stop", so callers unwind exactly
    // as before and leave extState unknown. The horn IS at the target now, so
    // that is merely pessimistic — the next boot retracts, which is safe.
    return !aborted;
}

// EEPROM layout for a servo. initializeEEPROMLayout() reserves sizeof(int) == 4
// bytes per servo and only one was ever used, so the commanded position fits in
// the spare bytes without changing the layout or invalidating existing data.
//
//   +0        int8   extState  (-1 unknown, 0 retracted, 1 extended, 2 partial,
//                               3 ejected)
//
// 3 was appended, never renumbered, so bytes from older builds keep their
// meaning (see coarseState() in the header).
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
