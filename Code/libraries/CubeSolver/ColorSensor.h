#ifndef ColorSensor_h
#define ColorSensor_h

#include <Arduino.h>
#include <TCA9548.h>
#include <veml6040.h>
#include <EEPROM.h>
#include <RunningMedian.h>
#include "CubePump.h"

// Result of classifying one sticker.
//
// getColor() returns a bare char, which throws away everything needed to tell
// "definitely red" from "0.004 closer to yellow than to white".
//
// Measured on the archived calibration data, the minimum distance between two
// DIFFERENT colors on a given sensor ranges 0.005-0.145 (median ~0.05) — every
// one of them below the old global colorTol of 0.15. So that gate could not
// separate one color from another. It was NOT inert, though: over a cross-run
// set it still returned 'U' for 10.8% of readings, 54% of which were genuinely
// the wrong color.
//
// `margin` and `confidence` are what let a caller say "sticker 23 is uncertain,
// rescan or repair it" instead of choosing between a bare letter and nothing.
struct ColorReading {
    char  color;        // best match, or 'U' if unusable
    char  alt;          // runner-up — the candidate for constraint repair
    float dist;         // chromaticity distance to `color`
    float margin;       // dist(alt) - dist(color); small == ambiguous
    float confidence;   // margin scaled by this sensor's own separation, [0,1]
    bool  ok;           // passed both the absolute and the relative test
};

class ColorSensor {
public:
    ColorSensor(TCA9548* multiplexers[2], const int LEDPIN, int muxOrder[9], int channelOrder[9], int& eepromFlagAddress, int (&eepromAddresses)[9][7][4]);

    int begin();

    // Scan Functions
    void setLED(bool ledState);
    void scanSingle(int sensorIdx);
    void scanFace();
    void getFaceColors(char output[9]);
    const int* getScanValRow(int idx);

    // --- Cheap presence polling ---
    //
    // A three-call session for asking "is there still something in front of
    // this board?" often enough to notice a cube being lifted out. scanSingle()
    // cannot answer that: it lights the LED on every call and then
    // pumpDelay(waitTime * 3), roughly 900 ms, because until the LED has been
    // on for two windows the VEML6040's register still holds a window it
    // integrated in the dark. Polling it costs ~1 s of latency per look and
    // hammers both muxes.
    //
    // What makes the cheap version legal is that every VEML6040 integrates
    // CONTINUOUSLY, whether or not its mux channel happens to be selected —
    // the TCA9548 gates the I2C lines, not the sensor behind them. Hold the
    // LED on and all nine registers on the board hold a lit window no older
    // than one integration period (160 ms by default). So after ONE warm-up,
    // selecting a channel and reading it at once is a valid reading of that
    // sensor: the window came from that sensor's own chip, under light that
    // was already on. There is no per-switch settle to pay, which is what lets
    // a sweep of all nine cost a few milliseconds of bus time rather than nine
    // integration windows. (liveRead() charges one window per switch anyway,
    // out of caution a diagnostic page can afford; a presence sweep cannot,
    // and does not need to — a reading up to one window old is exactly as
    // good for "is the cube still here" as a fresh one.)
    //
    // This deliberately trades color accuracy for latency: one channel instead
    // of four, no median filter, no calibration lookup. It must NEVER be used
    // to classify a sticker — scanFace() + classify() is the only thing allowed
    // to say what color something is.
    //
    // A scanSingle()/scanFace()/applyIntegrationTime() call in the middle of a
    // session invalidates it (they clear both muxes and drive the LED), so
    // re-open the session with presenceBegin() afterwards.

    // Open a session: LED on, both muxes cleared, warm-up clock started.
    void presenceBegin();

    // One cheap look at all nine sensors. Selects each channel in turn, reads
    // its WHITE register, and leaves both muxes CLEARED on the way out. That
    // last part is what lets the other board be swept next: all four color
    // muxes share one Wire bus and every VEML6040 answers at the same address,
    // so a channel left held here would put two sensors on the bus during the
    // other board's reads.
    //
    // WHITE, not a color. With the LED on, a cube face a few millimetres away
    // reflects strongly and W reads high; with the cube gone there is nothing
    // within reach to reflect and W collapses to whatever ambient light leaks
    // into the chamber. W is also the largest and least color-dependent
    // channel, so one threshold behaves the same for a red sticker as for a
    // white one — exactly what a presence test wants and what a classifier
    // must not settle for.
    //
    // No pumpDelay and no settling: nine mux selects and nine two-byte reads,
    // on the order of 10 ms at 100 kHz — safe to call from a state handler
    // every ~200 ms.
    //
    // NEGATIVE means "no reading", never "no cube". A caller that treats a bus
    // fault as a low W would decide the cube had gone because the I2C bus
    // hiccuped; consuming a fault as a measurement is how the alignment loop
    // was once made to drive blind. Faults are reported PER SENSOR so that one
    // flaky channel costs one reading, not the whole sweep.
    //   >= 0 : the number of sensors that could NOT be read this sweep (0 =
    //          all nine good). white[i] holds 0-65535 for each sensor read
    //          and -1 for each that faulted.
    //     -1 : no session open — presenceBegin() was not called; white[] untouched
    //     -2 : session still warming up, see presenceWarmupMs(); white[] untouched
    int presenceSweep(int white[9]);

    // Close the session: LED off, both muxes deselected. Idempotent — safe to
    // call twice, and safe to call on an abort path that does not know whether
    // a session was ever opened.
    void presenceEnd();

    bool presenceOpen() const { return presenceLive; }

    // How long after presenceBegin() before presenceSweep() stops returning -2.
    // Two integration windows: when the LED lights, the sensor's register still
    // holds a window integrated in the dark, and the window in progress is only
    // partly lit, so the second complete window is the first honest one.
    // Exposed so a caller can size its own "still waiting" tolerance instead of
    // guessing, and so it tracks setIntegrationIndex() automatically.
    int presenceWarmupMs() const { return 2 * integrationMsFor(getIntegrationIndex()); }

    // --- Live diagnostic read (the Color Sensors page) ---
    //
    // Full RGBW plus a classification, fast enough that a person can watch the
    // numbers move — the page that answers "what is this sensor seeing right
    // now", not "what color is this sticker".
    //
    // scanSingle() cannot do that job. It re-selects the mux and lights the LED
    // on every call, so it must pumpDelay(waitTime * 3) — about 900 ms — every
    // call. An 18-sensor sweep costs ~16 s and a page parked on ONE sensor
    // still refreshes barely once a second.
    //
    // The saving is the one presenceSweep() already proves: the VEML6040
    // integrates CONTINUOUSLY, and one LED lights all nine sensors on a board.
    // Hold the LED on and leave the channel selected and the register already
    // holds a fully-lit window of the sensor you are pointing at. So the
    // settling cost is charged only for what actually changed:
    //
    //   same sensor, LED already warm : 0 ms — four I2C reads, ~1 ms of bus
    //   different sensor, LED warm    : one integration window (160 ms default)
    //   LED cold (first read, or      : two windows — the same figure and the
    //   something else moved it)        same argument as presenceWarmupMs()
    //
    // One window rather than none on a channel change: with the LED already on
    // the window sitting in the register was integrated under the same light,
    // so in principle it is usable immediately. Waiting one window makes that
    // argument unnecessary — it guarantees the window read was integrated
    // entirely after the switch — and it still costs a fifth of scanSingle().
    //
    // WHAT THIS IS ALLOWED TO BE WRONG ABOUT, AND scanFace() IS NOT.
    // There is NO median filter. scanFace() takes numScans windows per sensor
    // and keeps the median of each channel; liveRead() takes one window and
    // hands it straight over, so a single noisy window shows through and can
    // flip the classified letter for one frame. On a live readout that is
    // honest — it is what the sensor just said, and a value that twitches is
    // itself the diagnosis. In a scan it is a mis-solved cube.
    // ** Do not wire liveRead() into the scan path. ** scanFace() + classify()
    // remains the only thing allowed to decide what color a sticker is.
    // It also writes NEITHER currentRGBW NOR scanVals, deliberately, so a live
    // reading can never be picked up later by code that thinks it is holding
    // scan data.
    //
    // AGAINST A PRESENCE SESSION. Both hold a channel selected and both drive
    // the LED, on the same board, so they cannot both be live. Presence wins:
    // while a session is open on this object liveRead() refuses with -5 rather
    // than stealing the channel from a state handler deciding whether the cube
    // has been lifted out. The other direction needs nothing remembered by
    // hand — presenceBegin(), presenceEnd(), setLED(), scanSingle(),
    // scanFace(), readSensor(), applyIntegrationTime() and begin() all move the
    // muxes or the LED, and every one of them invalidates the live session, so
    // the next liveRead() re-selects and re-settles instead of trusting a
    // channel that is no longer selected.
    //
    // ACROSS THE TWO BOARDS. This object cannot see the other one, so a caller
    // sweeping both boards must call liveRelease() on the board it is leaving
    // before reading the other — same shared-bus reason as presenceSweep().

    // One live read. rgbw[] is filled on success; pass `out` to also get the
    // classification — the same classify() the scan path uses, only the
    // measurement feeding it is cheaper.
    //
    // Leaves the LED on and the channel held: that is the whole point, and it
    // is what makes the next call free. Call liveEnd() when the page closes.
    //
    // Blocks for the settle above through pumpDelay(), so the panel keeps
    // refreshing and the SELECT+LEFT abort chord is still noticed.
    //
    // NEGATIVE IS A FAULT, NEVER DATA — MotorEncoder.h's rule, for the reason
    // that applies with particular force here: a failed I2C read hands back 0,
    // and an RGBW of zeros classifies as whichever reference is darkest. The
    // page would show a confident wrong color instead of an error.
    //   0 : ok — rgbw[] (and *out, if given) are valid
    //  -1 : sensorIdx out of range
    //  -2 : the settle was aborted, so the window is still the pre-switch one;
    //       nothing was read and nothing was written. The session is closed on
    //       the way out (liveEnd()), because an abort is the page leaving
    //  -3 : I2C write to the VEML failed (bus fault, sensor gone)
    //  -4 : I2C read from the VEML returned the wrong byte count
    //  -5 : a presence session is open on this board; refused
    int liveRead(int sensorIdx, int rgbw[4], ColorReading* out = nullptr);

    // Release the held channel, LEAVING THE LED ALONE. This is the call to make
    // on the board you are leaving before you read the other one; dropping the
    // LED too would make it re-warm on the way back and charge two windows
    // instead of one.
    void liveRelease();

    // Release the channel AND the LED — page exit, and any abort path.
    // Idempotent, and safe when no live read ever happened.
    void liveEnd();

    // What the next liveRead() of this sensor will cost, in milliseconds, so a
    // page can size its tick instead of guessing. 0 means the channel is held
    // and the LED is already warm.
    int liveSettleMsFor(int sensorIdx) const;

    // EEPROM Calibration Functions
    bool loadCalibration();
    bool saveCalibration();
    void resetCalibration();

    // Calibration Get/Set
    int setColorCal(int sensorIdx, char color, const int rgbw[4]);
    int getColorCal(int sensorIdx, char color, int channel);

    // Parse Color
    char getColor(int sensorIdx, const int rgbw[4]);

    // Full classification with confidence. getColor() is a thin wrapper over
    // this, so existing callers keep working unchanged.
    ColorReading classify(int sensorIdx, const int rgbw[4]) const;

    // Per-sticker readings for the most recent scanFace().
    void getFaceReadings(ColorReading out[9]) const;

    // Minimum distance between any two of the six real colors, for one sensor.
    // Derived from calVals, so it needs no EEPROM of its own — it is recomputed
    // whenever calibration is loaded or saved. Returns 0 if uncalibrated.
    float getSensorSeparation(int sensorIdx) const;

    // Health check for a sensor: reports a channel that reads identically zero
    // across all six real colors (a dead photodiode channel), or a separation
    // too small to classify reliably.
    //  0 - healthy
    //  1 - a channel is stuck at zero for every color
    //  2 - inter-color separation below minUsableSeparation
    int checkSensorHealth(int sensorIdx) const;

    // Push integrationTime out to all nine VEML sensors, and rescale waitTime
    // with it.
    //
    // Needed because begin() is the ONLY thing that ever calls
    // setConfiguration(). Writing integrationTime at runtime without this
    // changes the number a settings screen displays and nothing about how long
    // the sensors actually integrate for — the reading would go on being taken
    // the old way, silently.
    //
    // Costs nine mux selects and nine I2C writes, so it is a deliberate call
    // after a change, not something to do per scan.
    void applyIntegrationTime();

    // Register value for an integration-time index, 0..5 -> 40..1280 ms.
    static int  integrationRegFor(int index) { return (index & 0x07) << 4; }
    static int  integrationMsFor(int index)  { return 40 << (index & 0x07); }
    int  getIntegrationIndex() const { return (integrationTime >> 4) & 0x07; }
    void setIntegrationIndex(int index);

// make private
public:
    TCA9548* multiplexers[2]; // The two muxes on each boards
    VEML6040 veml;                      // Color sensor object
    int ledPin;                         // LED output pin for the board
    int muxOrder[9];                    // Map from cube face square to mux {UL, UM, UR, ML, MM, MR, DL, DM, DR}
    int channelOrder[9];                // Map from cube face square to I2C channel {UL, UM, UR, ML, MM, MR, DL, DM, DR}

    // Calibration values saved on EEPROM
    int calVals[9][7][4];

    // Scan values
    int currentRGBW[4];
    int scanVals[9][4];
    int numScans = 1;
    int integrationTime = VEML6040_IT_160MS; // Integration time (40MS, 80MS, 160MS, 320MS, 640MS, 1280MS) (Time per scan per sensor)
    int waitTime = 300;                     // Integration time * 1.875 — see setIntegrationIndex()

    // Presence session state (see presenceBegin). While a session is live the
    // LED is on and the warm-up clock is running; no channel is held between
    // sweeps, because presenceSweep() clears both muxes on its way out.
    bool     presenceLive    = false;
    uint32_t presenceStartMs = 0;

    // Live-read session state (see liveRead). liveSensor is the channel this
    // object believes is currently selected, -1 when nothing is held — not
    // having to re-establish it IS the saving. liveLedOn/liveLedOnMs timestamp
    // the illumination going on, because a window integrated before the LED lit
    // is a window of the dark, and the register hands it back looking exactly
    // like a very dark sticker.
    int      liveSensor  = -1;
    bool     liveLedOn   = false;
    uint32_t liveLedOnMs = 0;

    // Not the decision threshold any more: classify() ignores it in favour of
    // per-sensor limits derived from calibration (sensorSeparation below).
    // getColor() still falls back to it for a sensor with no separation
    // figure, and the Parameters screen still edits it (CubeTuneTable.cpp).
    //
    // Why: 0.15 is wider than the entire inter-color separation of this
    // hardware (measured 0.005-0.145 across all 18 sensors), so it could only
    // ever reject gross faults, never a genuine yellow/white ambiguity.
    float colorTol = 0.15;

    int maxColorVal = 65535;

    // --- Per-sensor decision limits (derived, not stored) ---

    // Minimum distance between any two of the six real colors, per sensor.
    // Recomputed by computeSeparations() on every calibration load/save.
    float sensorSeparation[9] = {0,0,0,0,0,0,0,0,0};

    // A reading must be at least this fraction of the sensor's own separation
    // closer to its best match than to the runner-up. Scales automatically: a
    // clean sensor (sep 0.14) demands a ~0.05 margin, a marginal one (sep 0.03)
    // demands ~0.01.
    //
    // This relative test is the one that does the real work — it is what
    // distinguishes "definitely red" from "0.004 closer to yellow than white".
    float marginFraction = 0.35f;

    // Absolute gate, expressed as a MULTIPLE of the sensor's separation. This is
    // a gross-fault detector ("is this reading anywhere near any reference?"),
    // not a precision test — the margin test above handles ambiguity.
    //
    // Chosen from a measured FRR/FAR sweep over the archived calibration data
    // (false-reject = a correct reading refused; false-accept = a wrong reading
    // admitted with ok == true):
    //
    //     fraction   FRR      accepted-and-wrong   FAR
    //        1.0     30.9%          92             5.6%
    //        1.5     23.5%         141             7.8%
    //        2.0     18.8%         167             8.9%   <- knee
    //        3.0     12.4%         198             9.9%
    //        inf      0.0%         261            12.2%
    //
    // 3.0 more than doubled the absolute false-accept count versus 1.0 while
    // retaining only ~24% of the gate's discriminating power. 2.0 is the knee.
    //
    // Do not tighten to 1.0 either: measured run-to-run drift is ~70% of the
    // separation, so a radius of exactly `sep` refuses 30.9% of CORRECT
    // readings.
    float distanceFraction = 2.0f;

    // Sensors below this separation cannot classify reliably no matter what the
    // margin says. Board 2 sensor 2 measures ~0.005 in every archived run.
    float minUsableSeparation = 0.02f;

    // True ADC clipping. The VEML6040 is a 16-bit part and maxColorVal is 65535.
    //
    // This was briefly set to 235 on the mistaken belief that the ~244 ceiling
    // seen in the archived calibration data was register saturation. It is not
    // — 244 is 0.4% of full scale, and that ceiling is the optical/integration
    // dynamic range at VEML6040_IT_160MS. Thresholding at 235 marked 222 of the
    // stored reference values "saturated", nearly all of them yellow, orange and
    // white, which made those three faces unidentifiable and stopped the machine
    // scanning at all.
    //
    // If the W-channel ceiling is worth investigating, do it on the bench (lower
    // integration time or LED current) — it is an optics question, not a
    // clipping one.
    int saturationThreshold = 65000;

    // Recompute sensorSeparation[] from calVals. Called automatically.
    void computeSeparations();

    // EEPROM Variables
    int& eepromFlagAddr;                // Reference to flag address
    int (&eepromAddrRef)[9][7][4];     // Reference to source array
    int eepromAddr[9][7][4];           // Local copy of addresses
    int flagValue = 122;
    float colorDistance(const int rgbw1[4], const int rgbw2[4]) const;
    void readSensor(int sensorIdx);

    // Anything that moves the muxes or the LED behind a live read's back calls
    // this, so a stale cached channel can never be trusted into a color.
    void invalidateLive();

    // One VEML data register, read by hand. veml.getRed() and friends return 0
    // when the transaction fails, which is indistinguishable from a dark
    // sensor — the same reason presenceSweep() does its own I2C.
    //  >= 0    : the 16-bit count
    //  -3 / -4 : as liveRead()
    int readChannelRaw(int commandCode);

    // Color Helper Function
    int colorIndex(char color);
};

#endif
