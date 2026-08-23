// CubeServo.h
#ifndef CubeServo_h
#define CubeServo_h

#include <Arduino.h>
#include <PWMServo.h>
#include <EEPROM.h>
#include "CubePump.h"

class CubeServo {
public:
    

public:
    CubeServo(int pin, int eepromAddr, unsigned int retPos, unsigned int extPos, int sweepDelay = 20);

    void begin();      // Call in setup
    void extend();     // Move to extended position
    void partial();  // TO DO IMPLEMENT LATER
    void retract();    // Move to retracted position
    void toggle();     // Toggle between extended/retracted
    bool isExtended(); // Returns true if extended

    // The tri-state isExtended() flattens away, for anything that has to SHOW
    // where the horn is rather than decide something from it.
    //
    // A diagnostics page that opens on "?" is lying by omission: the state
    // survives a reset in EEPROM and begin() has already acted on it, so the
    // machine knows perfectly well where it parked. Nothing exposed it.
    //
    //   0 = retracted, 1 = extended, 2 = partial, 3 = ejected, -1 = unknown
    //
    // -1 is a real answer and callers must render it as such. It means the
    // horn is genuinely at neither endpoint — mid-sweep, aborted, or moved by
    // previewRaw() — and the class refuses to guess.
    //
    // 2 and 3 are distinct only so a page can SHOW which pose the horn is in;
    // begin() retracts out of either. 2 keeps its old meaning, so an EEPROM
    // written by firmware that predates 3 reports an ejected servo as partial
    // — wrong by one name, safe in every other respect, and corrected the
    // first time eject() runs. No version bump needed.
    int coarseState() const { return extState; }

    // Present the cube for the operator to take. A position of its own rather
    // than a second name for partial(): the bottom servo has to hold the cube
    // clear of the ring to be gripped, and push it high enough to be picked up,
    // and those are not the same height.
    void eject();

    // --- named positions -----------------------------------------------------
    // Partial and eject default to DERIVED values (three quarters of travel,
    // and the same place as partial) so a servo nobody has tuned behaves
    // exactly as it did before these existed. Setting either one pins it, and
    // from then on it no longer follows the extend position around.
    unsigned int partialTarget() const;
    unsigned int ejectTarget()   const;
    void setPartial(unsigned int pos);
    void setEject(unsigned int pos);
    bool partialIsPinned() const { return partialExplicit; }
    bool ejectIsPinned()   const { return ejectExplicit;   }

    // --- position tuning -----------------------------------------------------
    // The retracted and extended positions are handed in at construction and
    // owned here from then on. The globals they came from (topExtPos and
    // friends) are read exactly once, so writing those later changes nothing —
    // a settings screen that tried would appear to work and move no servo.
    // These are the way in.
    unsigned int retracted()      const { return retPos; }
    unsigned int extended()       const { return extPos; }
    int          sweepStepDelay() const { return sweepDelay; }

    void setRetracted(unsigned int pos);
    void setExtended(unsigned int pos);
    void setSweepStepDelay(int ms);

    // Drive the horn to a raw position, instantly when the change is small.
    //
    // For setting a position BY EYE, which is the only way an endpoint gets
    // set: the point is watching the horn while the number changes, and a sweep
    // at 15 ms a step cannot follow a wheel. So a change small enough to BE a
    // wheel detent is written straight out, with no sweep and no delay.
    //
    // A LARGE change is not a follow and is swept instead. It cannot have come
    // from a wheel — it is the first preview after a tuning row seeded itself
    // from a stored endpoint, or a discard putting a part back — and there the
    // gap is a full travel, taken in one instant, with the cube quite possibly
    // still clamped. The threshold and its reasoning live in the .cpp.
    //
    // Callers MUST still step it: this decides HOW to move, not how far. And a
    // swept preview can be cut short by the abort chord, so after one the horn
    // may be short of the value on screen; the next preview sweeps again from
    // wherever it stopped, which is why nothing here records an arrival.
    //
    // Leaves the coarse state unknown, because after this the horn is at
    // neither endpoint. Does NOT write EEPROM; call persist() once the wheel
    // has stopped rather than burning a write per detent.
    void previewRaw(unsigned int pos);

    // Commit the tracked position to EEPROM. Worth being deliberate about:
    // begin() trusts the stored position to decide how far the first sweep has
    // to travel, so leaving it stale after a preview is what arms a full-travel
    // slam on the next power-up.
    void persist() { updateEEPROM(); }

private:
    PWMServo servo;
    int pin;                    // Servo pin
    int eepromAddr;             // Address where we store the state

    unsigned int currentPos;    // Position the servo is currently set to
    unsigned int retPos;        // Retracted position value (0-270)
    unsigned int extPos;        // Extended position value (0-270)
    int extState;               // 0 = retracted, 1 = extended, 2 = partially retracted, 3 = ejected, -1 = unknown
    int sweepDelay;             // Time in ms to delay between each sweep step

    // Pinned positions — see partialTarget() / ejectTarget().
    unsigned int partialPos      = 0;
    bool         partialExplicit = false;
    unsigned int ejectPos        = 0;
    bool         ejectExplicit   = false;

    // Returns false if the sweep was cut short by an abort. Callers MUST NOT
    // record the target position or a settled state when it returns false —
    // the horn did not get there.
    bool sweepTo(unsigned int newPos);
    void loadStateFromEEPROM();
    void updateEEPROM();
};

#endif // CubeServo_h
