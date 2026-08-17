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
// the wrong color. (An earlier version of this comment said the gate "never
// fired"; that was wrong, and it contradicted the measurement quoted in
// getColor() itself.)
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
    int waitTime = 300;                     // Set to integration time * 2.5

    // DEPRECATED as a decision threshold — kept only so existing sketches that
    // read it still compile. classify() ignores it in favour of per-sensor
    // limits derived from calibration; see sensorSeparation below.
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
    // readings. (An earlier comment here claimed it rejects "most" legitimate
    // readings — that was wrong; it is roughly a third.)
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

    // Color Helper Function
    int colorIndex(char color);
};

#endif
