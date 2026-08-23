#include "ColorSensor.h"
#include <Wire.h>

// The VEML6040 Arduino library defines these; the desktop simulator's shim
// header does not, and this file is compiled there too. Falling back to the
// datasheet values keeps the sim building without a shim change — and the
// #ifndef means the library's own definitions win wherever they exist.
#ifndef VEML6040_I2C_ADDRESS
#define VEML6040_I2C_ADDRESS 0x10
#endif
#ifndef COMMAND_CODE_WHITE
#define COMMAND_CODE_WHITE   0x0B
#endif
#ifndef COMMAND_CODE_RED
#define COMMAND_CODE_RED     0x08
#endif
#ifndef COMMAND_CODE_GREEN
#define COMMAND_CODE_GREEN   0x09
#endif
#ifndef COMMAND_CODE_BLUE
#define COMMAND_CODE_BLUE    0x0A
#endif


ColorSensor::ColorSensor(TCA9548* multiplexers[2], const int LEDPIN, int muxOrder[9], int channelOrder[9], int& eepromFlagAddress, int (&eepromAddresses)[9][7][4])
: ledPin(LEDPIN), eepromFlagAddr(eepromFlagAddress), eepromAddrRef(eepromAddresses){

    // Assign muxes
    for (int i = 0; i < 2; i++) {
        this->multiplexers[i] = multiplexers[i];
    }

    for (int i = 0; i < 9; i++) {
        // Assign mux and channel order vectors
        this->muxOrder[i]     = muxOrder[i];
        this->channelOrder[i] = channelOrder[i];
    }
    
}

int ColorSensor::begin() {
    // Begins color sensor object
    // Returns: 
    //  0 - Success
    //  1 - Failed to being VEML color sensor
    //  1X - Multiplexer X not found on I2C wire
    //  2X - Multiplexer X failed to begin
    //  3X - VEML Sensor X failed to begin

    // Copy EEPROM addresses now that they are for sure initialized
    for (int i = 0; i < 9; i++) {
        for (int j = 0; j < 7; j++) {
            for (int k = 0; k < 4; k++) {
                this->eepromAddr[i][j][k] = eepromAddrRef[i][j][k];
            }
        }
    }

    // Initialize LED pin
    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, LOW);
    invalidateLive();   // the LED just went dark and every channel is about to
                        // be walked below

    // Check if calibration can be loaded, otherwise reset to zero
    if (!loadCalibration()) {
        resetCalibration();
    }

    for (int i = 0; i < 2; i++){
        // Check if MUX is found on I2C Wire
        if(!multiplexers[i]->isConnected()){
            return 10 + i;
        }

        // Begin Mux
        if(!multiplexers[i]->begin()){
            return 20 + i;
        } 

        // Close all channels on muxes
        multiplexers[i]->setChannelMask(0x00);
    }

    // Initialize VEML sensors
    for (int sensorIdx = 0; sensorIdx < 9; sensorIdx++) {
        // Select the channel for this sensor
        int muxIdx = muxOrder[sensorIdx] - 1;
        int chan = channelOrder[sensorIdx];
        
        // Disable all channels first
        multiplexers[0]->setChannelMask(0x00);
        multiplexers[1]->setChannelMask(0x00);
        
        // Enable only this sensor's channel
        multiplexers[muxIdx]->selectChannel(chan);
        
        // Initialize this VEML sensor
        if (!veml.begin()) {
            // Disable all channels before returning error
            multiplexers[0]->setChannelMask(0x00);
            multiplexers[1]->setChannelMask(0x00);
            return 30 + sensorIdx;  // Returns 30-38 for sensor 0-8 failure
        }
        
        // Configure this sensor
        veml.setConfiguration(integrationTime);
    }
    
    // Disable all channels after initialization
    multiplexers[0]->setChannelMask(0x00);
    multiplexers[1]->setChannelMask(0x00);

    return 0;
}

void ColorSensor::setIntegrationIndex(int index) {
    if (index < 0) index = 0;
    if (index > 5) index = 5;
    integrationTime = integrationRegFor(index);

    // waitTime tracks it: wait too little after a longer integration and every
    // sticker is read mid-conversion. 1.875x (300 ms for 160 ms) is the ratio
    // this machine has always run — well clear of one conversion, and kept
    // rather than rounded to a tidier multiple so a scan that works keeps working.
    waitTime = (integrationMsFor(index) * 300) / 160;

    applyIntegrationTime();
}

void ColorSensor::applyIntegrationTime() {
    invalidateLive();   // walks every channel, and changes the window length a
                        // live settle is measured in

    for (int sensorIdx = 0; sensorIdx < 9; sensorIdx++) {
        int muxIdx = muxOrder[sensorIdx] - 1;
        int chan   = channelOrder[sensorIdx];

        multiplexers[0]->setChannelMask(0x00);
        multiplexers[1]->setChannelMask(0x00);
        multiplexers[muxIdx]->selectChannel(chan);

        // No veml.begin() here, unlike the loop in begin(). A sensor that is
        // already running does not need re-initialising, and one that is absent
        // must not turn a settings change into a hard failure — the health
        // check and the boot error codes are where a missing sensor is
        // reported.
        veml.setConfiguration(integrationTime);
    }

    multiplexers[0]->setChannelMask(0x00);
    multiplexers[1]->setChannelMask(0x00);
}

void ColorSensor::readSensor(int sensorIdx) {
    invalidateLive();   // clears both muxes below; a held live channel is gone

    // Look up which multiplexer and channel this sensor is on
    int muxIdx = muxOrder[sensorIdx] - 1;
    int chan   = channelOrder[sensorIdx];

    // Disable all channels in both muxes (just in case)
    multiplexers[0]->setChannelMask(0x00);
    multiplexers[1]->setChannelMask(0x00);

    // Tell the multiplexer to enable only this channel
    multiplexers[muxIdx]->selectChannel(chan);

    // Assign values
    currentRGBW[0] = veml.getRed();
    currentRGBW[1] = veml.getGreen();
    currentRGBW[2] = veml.getBlue();
    currentRGBW[3] = veml.getWhite();

    // Disable all channels in both muxes (just in case)
    multiplexers[0]->setChannelMask(0x00);
    multiplexers[1]->setChannelMask(0x00);
}

void ColorSensor::scanSingle(int sensorIdx) {
    invalidateLive();   // re-selects the mux and cycles the LED below, so any
                        // held live session's channel and warm LED are gone

    // Turns on LED to scan
    // Turn on illumination LED
    digitalWrite(ledPin, HIGH);

    // Look up which multiplexer and channel this sensor is on
    int muxIdx = muxOrder[sensorIdx] - 1;
    int chan   = channelOrder[sensorIdx];

    // Disable all channels in both muxes (just in case)
    multiplexers[0]->setChannelMask(0x00);
    multiplexers[1]->setChannelMask(0x00);

    // Tell the multiplexer to enable only this channel
    multiplexers[muxIdx]->selectChannel(chan);

    // Give the mux a moment to settle and veml to integrate
    veml.getRed(); veml.getGreen(); veml.getBlue(); veml.getWhite(); // Dummy read

    // ~900 ms integration wait; pump so the UI survives it.
    // An aborted wait returns after ~0 ms, and the VEML6040 integrates
    // continuously — so reading now would return the PREVIOUS integration
    // window, i.e. data from before the mux switched. A wrong-but-plausible
    // color is the worst possible output here, so bail instead.
    if (!pumpDelay(waitTime*3)) {
        digitalWrite(ledPin, LOW);
        multiplexers[0]->setChannelMask(0x00);
        multiplexers[1]->setChannelMask(0x00);
        return;
    }

    currentRGBW[0] = veml.getRed();
    currentRGBW[1] = veml.getGreen();
    currentRGBW[2] = veml.getBlue();
    currentRGBW[3] = veml.getWhite();

    // Turn off LED after reading
    digitalWrite(ledPin, LOW);

    // Disable all channels in both muxes
    multiplexers[0]->setChannelMask(0x00);
    multiplexers[1]->setChannelMask(0x00);
}

void ColorSensor::setLED(bool ledState) {
    // A live read may skip its settle only because it knows the LED has been on
    // since it lit it, and which channel is held. Anyone moving the LED behind
    // its back ends that — including turning it ON, since until this moment the
    // sensors were integrating in the dark.
    invalidateLive();

    digitalWrite(ledPin, ledState);
}

// --- Cheap presence polling -------------------------------------------------
//
// Why a sweep may read nine sensors back to back with no settle between them,
// when scanSingle() waits ~900 ms for one: scanSingle()'s wait is for the LED.
// It lights it on every call, and the register then holds a window integrated
// in the dark until two windows have passed. A presence session lights the LED
// once; after that every VEML6040 on the board — selected or not, the mux only
// gates the bus — holds a lit window of its own, so selecting a channel and
// reading it is a reading of that sensor, never of the one before it. See
// ColorSensor.h for the full contract.
//
// This buys latency with accuracy and must never be used to classify a sticker.

void ColorSensor::presenceBegin() {
    setLED(true);

    // Nothing is held between sweeps, so begin() only has to make sure nothing
    // is held NOW — a channel left selected by whatever ran before would put a
    // second sensor on the bus under the first sweep's reads.
    multiplexers[0]->setChannelMask(0x00);
    multiplexers[1]->setChannelMask(0x00);

    presenceLive    = true;
    presenceStartMs = millis();
}

int ColorSensor::presenceSweep(int white[9]) {
    // RETURNS (see the header for the reasoning):
    //  >= 0 : number of sensors that faulted; white[i] is the count or -1
    //    -1 : no session open
    //    -2 : still warming up — the LED has not been on for two windows yet
    if (!presenceLive) return -1;

    // Unsigned subtraction, so this stays correct across the millis() rollover.
    if ((uint32_t)(millis() - presenceStartMs) < (uint32_t)presenceWarmupMs()) {
        return -2;
    }

    int faults = 0;
    for (int sensorIdx = 0; sensorIdx < 9; sensorIdx++) {
        int muxIdx = muxOrder[sensorIdx] - 1;
        int chan   = channelOrder[sensorIdx];

        // Same idiom as scanSingle(): clear both muxes, then enable the one
        // channel. TCA9548::setChannelMask() skips the write when the mask is
        // already what it is asked for, so this is two transactions per
        // sensor, not three.
        //
        // A mux that refuses the select is a fault for THIS sensor: the read
        // below would go to whichever channel the mux was left on, which is a
        // plausible number for the wrong sensor — the one thing worse than no
        // number at all.
        multiplexers[0]->setChannelMask(0x00);
        multiplexers[1]->setChannelMask(0x00);
        if (!multiplexers[muxIdx]->selectChannel(chan)) {
            white[sensorIdx] = -1;
            faults++;
            continue;
        }

        // readChannelRaw() rather than veml.getWhite(): the library's read()
        // returns 0 when the transaction fails, which is exactly what a dark
        // sensor returns, so a bus fault would arrive here disguised as "the
        // cube is gone". MotorEncoder::scan() talks to the AS5600 by hand for
        // the same reason.
        const int w = readChannelRaw(COMMAND_CODE_WHITE);
        if (w < 0) {
            white[sensorIdx] = -1;
            faults++;
        } else {
            white[sensorIdx] = w;
        }
    }

    // Leave nothing selected: the other board's sweep is next on the same bus.
    multiplexers[0]->setChannelMask(0x00);
    multiplexers[1]->setChannelMask(0x00);
    return faults;
}

void ColorSensor::presenceEnd() {
    // Unconditional, so it is idempotent and safe on an abort path that does
    // not know whether a session was ever opened. The LED matters most: it is
    // the only thing here that stays lit and drawing current forever if a
    // caller forgets, and the machine has no other way to notice.
    setLED(false);
    multiplexers[0]->setChannelMask(0x00);
    multiplexers[1]->setChannelMask(0x00);
    presenceLive = false;
}

// --- Live diagnostic read ---------------------------------------------------
//
// The contract, the cost table and the reason this may skip a wait scanSingle()
// may not are all in ColorSensor.h. The short version: scanSingle() pays ~900 ms
// because it re-selects the mux and re-lights the LED every call; a live read
// holds both, so it only pays for what changed.

void ColorSensor::invalidateLive() {
    liveSensor = -1;
    liveLedOn  = false;
}

int ColorSensor::readChannelRaw(int commandCode) {
    // Deliberately no mux select here — liveRead() has already established the
    // channel, and re-selecting per channel would put the mux writes back into
    // the very loop this exists to make cheap.
    Wire.beginTransmission(VEML6040_I2C_ADDRESS);
    Wire.write(commandCode);
    if (Wire.endTransmission(false) != 0) {   // repeated start, no stop
        return -3;
    }

    int n = Wire.requestFrom((int)VEML6040_I2C_ADDRESS, 2);
    if (n != 2) {
        return -4;
    }

    // The VEML6040 returns little-endian 16-bit values.
    uint16_t lsb = Wire.read();
    uint16_t msb = Wire.read();
    return (int)((uint16_t)((msb << 8) | lsb));
}

int ColorSensor::liveSettleMsFor(int sensorIdx) const {
    if (sensorIdx < 0 || sensorIdx > 8) return 0;

    const int window = integrationMsFor(getIntegrationIndex());

    // The LED has to have been on for two windows whatever else is true: when
    // it lights, the register still holds a window integrated in the dark and
    // the window in progress is only partly lit, so the second complete window
    // is the first honest one. Identical to presenceWarmupMs(), and for the
    // identical reason — but charged only for the time not already elapsed,
    // because a page reading its fourth sensor lit the LED long ago.
    int ledDue = 2 * window;
    if (liveLedOn) {
        // Unsigned subtraction, so this stays correct across the millis()
        // rollover.
        uint32_t on = (uint32_t)(millis() - liveLedOnMs);
        ledDue = (on >= (uint32_t)(2 * window)) ? 0 : (int)((uint32_t)(2 * window) - on);
    }

    // Moving to a different sensor: one window guarantees that what comes back
    // was integrated entirely after the switch, so no window can be attributed
    // to the wrong sensor. Staying on the same one costs nothing at all.
    const int chanDue = (liveSensor == sensorIdx) ? 0 : window;

    return (ledDue > chanDue) ? ledDue : chanDue;
}

int ColorSensor::liveRead(int sensorIdx, int rgbw[4], ColorReading* out) {
    if (sensorIdx < 0 || sensorIdx > 8) return -1;

    // Presence wins. A session is a state handler watching for the cube being
    // lifted out; stealing its channel would make it read some other sensor
    // and decide the cube had gone. Refusing is the only answer that cannot be
    // mistaken for a measurement.
    if (presenceLive) return -5;

    // digitalWrite rather than setLED(), which invalidates the live session by
    // design. This is the one place allowed to move the LED without doing so,
    // because it is the thing keeping the record of when it moved.
    if (!liveLedOn) {
        digitalWrite(ledPin, HIGH);
        liveLedOn   = true;
        liveLedOnMs = millis();
    }

    // Computed before the select, so the warm-up clock is read once and the
    // answer matches what the caller was told by liveSettleMsFor().
    const int settle = liveSettleMsFor(sensorIdx);

    if (liveSensor != sensorIdx) {
        int muxIdx = muxOrder[sensorIdx] - 1;
        int chan   = channelOrder[sensorIdx];

        // Same idiom as scanSingle()/presenceSweep(): clear both muxes, then
        // enable the one channel. Unlike scanSingle() nothing clears it again
        // on the way out — that held selection is what makes the next call of
        // the same sensor free.
        multiplexers[0]->setChannelMask(0x00);
        multiplexers[1]->setChannelMask(0x00);
        multiplexers[muxIdx]->selectChannel(chan);
        liveSensor = sensorIdx;
    }

    if (settle > 0) {
        // Pumped, so the panel keeps refreshing and the abort chord is noticed
        // mid-wait. An aborted wait returns after ~0 ms with the register still
        // holding the pre-switch window — the wrong-but-plausible color
        // scanSingle() bails rather than report.
        if (!pumpDelay(settle)) {
            // Unwind completely rather than just dropping the cache: an abort
            // is the page going away, and this is the one exit that would
            // otherwise leave a channel held and the LED lit with no one left
            // to call liveEnd(). Same bail scanSingle() makes, same reason.
            liveEnd();
            return -2;
        }
    }

    static const int kCmd[4] = { COMMAND_CODE_RED,  COMMAND_CODE_GREEN,
                                 COMMAND_CODE_BLUE, COMMAND_CODE_WHITE };

    // Read into a scratch array first: a fault partway through must leave the
    // caller's rgbw[] untouched, holding the last good reading, rather than a
    // half-updated mixture of two.
    int v[4];
    for (int k = 0; k < 4; ++k) {
        v[k] = readChannelRaw(kCmd[k]);
        if (v[k] < 0) {
            // A bus fault means the mux state is no longer worth trusting; make
            // the next call re-select and re-settle rather than read whatever
            // answers next.
            invalidateLive();
            return v[k];            // -3 or -4, passed through unchanged
        }
    }

    for (int k = 0; k < 4; ++k) rgbw[k] = v[k];

    // Neither currentRGBW nor scanVals is written — see the header. A live
    // reading must never be mistakable for scan data.
    if (out) *out = classify(sensorIdx, rgbw);

    return 0;
}

void ColorSensor::liveRelease() {
    // Does NOT touch the LED, deliberately. A page alternating between the two
    // boards must release this board's channel before reading the other (all
    // four muxes are on one bus and every VEML answers at the same address),
    // but dropping the LED as well would make it re-warm on the way back and
    // charge two windows instead of one.
    multiplexers[0]->setChannelMask(0x00);
    multiplexers[1]->setChannelMask(0x00);
    liveSensor = -1;
}

void ColorSensor::liveEnd() {
    // Unconditional for the same reason as presenceEnd(): idempotent, safe on
    // an abort path, and the LED must never be left lit.
    liveRelease();
    setLED(false);      // which also clears liveLedOn, via invalidateLive()
}

void ColorSensor::scanFace() {
    RunningMedian filter[9][4] = {
        {RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans)},
        {RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans),},
        {RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans)},
        {RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans)},
        {RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans)},
        {RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans)},
        {RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans)},
        {RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans)},
        {RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans), RunningMedian(numScans)}
    };

    setLED(true);

    for (int i = 0; i < numScans; ++i) {
        // --- Dummy read all sensors first to trigger integration ---
        for (int j = 0; j < 9; ++j) {
            int muxIdx = muxOrder[j] - 1;
            int chan   = channelOrder[j];
            multiplexers[0]->setChannelMask(0x00);
            multiplexers[1]->setChannelMask(0x00);
            multiplexers[muxIdx]->selectChannel(chan);

            veml.getRed(); veml.getGreen(); veml.getBlue(); veml.getWhite();  // trigger integration
        }

        // --- Wait once for integration ---
        // Abort here means the sensors have NOT finished integrating; reading
        // them would yield the previous window's data. Leave scanVals holding
        // the previous scan and let the caller notice the abort.
        if (!pumpDelay(waitTime * 3)) {   // ~900 ms x 6 faces
            setLED(false);
            multiplexers[0]->setChannelMask(0x00);
            multiplexers[1]->setChannelMask(0x00);
            return;
        }

        // --- Now read all sensors once they've integrated ---
        for (int j = 0; j < 9; ++j) {
            int muxIdx = muxOrder[j] - 1;
            int chan   = channelOrder[j];
            multiplexers[0]->setChannelMask(0x00);
            multiplexers[1]->setChannelMask(0x00);
            multiplexers[muxIdx]->selectChannel(chan);

            currentRGBW[0] = veml.getRed();
            currentRGBW[1] = veml.getGreen();
            currentRGBW[2] = veml.getBlue();
            currentRGBW[3] = veml.getWhite();

            for (int k = 0; k < 4; ++k) {
                filter[j][k].add(currentRGBW[k]);
            }
        }
    }

    setLED(false);
    multiplexers[0]->setChannelMask(0x00);
    multiplexers[1]->setChannelMask(0x00);

    // Save medians
    for (int j = 0; j < 9; ++j) {
        for (int k = 0; k < 4; ++k) {
            scanVals[j][k] = filter[j][k].getMedian();
        }
    }
}


void ColorSensor::getFaceColors(char output[9]){
    for(int i = 0; i < 9; i++) {
        output[i] = getColor(i, scanVals[i]);
    }
}

void ColorSensor::getFaceReadings(ColorReading out[9]) const {
    for (int i = 0; i < 9; i++) {
        out[i] = classify(i, scanVals[i]);
    }
}

void ColorSensor::computeSeparations() {
    // For each sensor, the smallest distance between any two of the SIX REAL
    // colors (index 0-5 = R G B Y O W). Index 6 ('E', empty chamber) is
    // deliberately excluded: it is useful as a cube-present test but it is not
    // a sticker color, and on some sensors it sits closer to blue than blue
    // sits to anything else — which would collapse the separation figure.
    //
    // This is derived from calVals rather than stored, so it needs no EEPROM
    // and cannot fall out of sync with the calibration it describes.
    for (int s = 0; s < 9; s++) {
        float minSep = -1.0f;

        for (int a = 0; a < 6; a++) {
            if (calVals[s][a][3] <= 0) continue;            // uncalibrated entry
            for (int b = a + 1; b < 6; b++) {
                if (calVals[s][b][3] <= 0) continue;
                float d = colorDistance(calVals[s][a], calVals[s][b]);
                if (d != d) continue;                       // NaN guard
                if (minSep < 0.0f || d < minSep) minSep = d;
            }
        }

        sensorSeparation[s] = (minSep < 0.0f) ? 0.0f : minSep;
    }
}

float ColorSensor::getSensorSeparation(int sensorIdx) const {
    if (sensorIdx < 0 || sensorIdx > 8) return 0.0f;
    return sensorSeparation[sensorIdx];
}

int ColorSensor::checkSensorHealth(int sensorIdx) const {
    if (sensorIdx < 0 || sensorIdx > 8) return 1;

    // A channel that reads identically zero for every real color is a dead
    // photodiode channel, not a legitimate measurement. Board 2 sensor 2 shows
    // exactly this on green across every archived calibration run, and nothing
    // else checks for it: setColorCal only rejects negatives
    // and values above 65535, so 0 is "valid" and colorDistance computes
    // happily on two of three dimensions.
    for (int k = 0; k < 3; k++) {           // R, G, B — W is the divisor
        bool allZero = true;
        for (int c = 0; c < 6; c++) {
            if (calVals[sensorIdx][c][k] != 0) { allZero = false; break; }
        }
        if (allZero) return 1;
    }

    if (sensorSeparation[sensorIdx] < minUsableSeparation) return 2;

    return 0;
}

ColorReading ColorSensor::classify(int sensorIdx, const int rgbw[4]) const {
    ColorReading r;
    r.color = 'U';
    r.alt   = 'U';
    r.dist  = 0.0f;
    r.margin = 0.0f;
    r.confidence = 0.0f;
    r.ok = false;

    if (sensorIdx < 0 || sensorIdx > 8) return r;

    static const char colorChars[7] = { 'R', 'G', 'B', 'Y', 'O', 'W', 'E' };

    // colorDistance divides by the white channel; a zero W makes every distance
    // inf/NaN, so reject it explicitly.
    if (rgbw[3] <= 0) return r;

    int   best = -1,  second = -1;
    float bestD = 0.0f, secondD = 0.0f;

    for (int c = 0; c < 7; ++c) {
        if (calVals[sensorIdx][c][3] <= 0) continue;    // this color never calibrated
        float d = colorDistance(rgbw, calVals[sensorIdx][c]);
        if (d != d) continue;                           // NaN guard

        if (best < 0 || d < bestD) {
            second = best;  secondD = bestD;
            best   = c;     bestD   = d;
        } else if (second < 0 || d < secondD) {
            second = c;     secondD = d;
        }
    }

    if (best < 0) return r;         // nothing usable: sensor is uncalibrated

    r.color = colorChars[best];
    r.dist  = bestD;
    if (second >= 0) {
        r.alt    = colorChars[second];
        r.margin = secondD - bestD;
    }

    float sep = sensorSeparation[sensorIdx];
    if (sep <= 0.0f) {
        // No separation figure available. Report the nearest match but never
        // claim confidence in it — ok stays false.
        return r;
    }

    // Absolute test: is the reading even in the neighbourhood of a reference?
    //
    // Deliberately generous — see distanceFraction in the header. Requiring
    // bestD <= sep (i.e. fraction 1.0) refuses ~31% of correct readings,
    // because run-to-run drift is roughly 70% of the separation itself.
    bool inRange = (bestD <= distanceFraction * sep);

    // Relative test: decisively closer to one reference than to the next?
    float need = marginFraction * sep;
    if (need > 0.0f) {
        float conf = r.margin / need;
        r.confidence = (conf > 1.0f) ? 1.0f : ((conf < 0.0f) ? 0.0f : conf);
    }

    // Clipping check: a saturated channel is not a measurement.
    bool saturated = false;
    for (int k = 0; k < 4; ++k) {
        if (rgbw[k] >= saturationThreshold) { saturated = true; break; }
    }

    r.ok = inRange
        && (r.margin >= need)
        && !saturated
        && (sep >= minUsableSeparation);

    return r;
}

const int *ColorSensor::getScanValRow(int idx)
{
    return scanVals[idx]; 
}

float ColorSensor::colorDistance(const int rgbw1[4], const int rgbw2[4]) const {
    // Simple Euclidean distance in 4D space (R,G,B,W)
    float sumSq = 0;

    // for (int i = 0; i < 4; i++) {
    //     int diff = rgbw1[i] - rgbw2[i];
    //     sumSq += diff * diff;
    // }

    for (int i = 0; i < 3; i++) {
        float norm1 = float(rgbw1[i]) / rgbw1[3];
        float norm2 = float(rgbw2[i]) / rgbw2[3];
        float diff = norm1 - norm2;
        sumSq += diff * diff;
    }
    return sqrt(sumSq);
}

char ColorSensor::getColor(int sensorIdx, const int rgbw[4]) {
    // Thin wrapper over classify(), kept so existing callers and sketches work
    // unchanged. Prefer classify() / getFaceReadings() in new code — this
    // signature can only say "some color", never "I am not sure".
    //
    ColorReading r = classify(sensorIdx, rgbw);

    // Apply the ABSOLUTE gate here, but not the margin/health gates.
    //
    // Gating on the full r.ok made three sensors (board 2 #2, #4, #7, whose
    // separation is below minUsableSeparation) return 'U' for every color they
    // will ever read, including their own calibration references — that broke
    // the diagnostic sketch for exactly the sensors you would open it to
    // investigate.
    //
    // But removing the gate entirely was also wrong. Measured over the archived
    // data, the ORIGINAL colorTol gate returned 'U' for 10.8% of cross-run
    // readings and 54% of those were genuinely the wrong color. Dropping it
    // turned real rejections into confident wrong answers.
    //
    // So: reject readings that are nowhere near any reference, and leave the
    // margin and sensor-health judgements to classify(), which reports them
    // without destroying the answer.
    if (r.color != 'U' && r.color != 'E') {
        float sep = sensorSeparation[sensorIdx];
        float limit = (sep > 0.0f) ? (distanceFraction * sep) : colorTol;
        if (r.dist > limit) {
            return 'U';
        }
    }

    return r.color;
}

int ColorSensor::colorIndex(char color) {
    switch (color) {
        case 'R': return 0;
        case 'G': return 1;
        case 'B': return 2;
        case 'Y': return 3;
        case 'O': return 4;
        case 'W': return 5;
        case 'E': return 6;
        default:  return -1;
    }
}

int ColorSensor::setColorCal(int sensorIdx, char color, const int rgbw[4]) {
    // Output:
    //  0 - Success
    //  1 - Invalid sensor index
    //  2 - Invalid color
    //  3 - Invalid RGBW value
    

    // Check valid sensor index
    if (sensorIdx < 0 || sensorIdx >= 9){
        return 1;
    } 

    // Get color index
    int colorIdx = colorIndex(color);

    // Validate color index
    if(colorIdx < 0 || colorIdx > 6) {
        return 2;
    }

    // Check valid RGBW values
    for (int i = 0; i < 4; i++) {
        if(rgbw[i] < 0 || rgbw[i] > maxColorVal) {
            return 3;
        }
    }
    
    // Get calibration array for desired sensor and color    
    int* target = calVals[sensorIdx][colorIdx];

    // Set calibration array to desired RGBW
    for (int i = 0; i < 4; i++) {
        target[i] = rgbw[i];
    }

    return 0;
}

int ColorSensor::getColorCal(int sensorIdx, char color, int channel){
    // Check valid sensor index
    if (sensorIdx < 0 || sensorIdx >= 9){
        return -1;
    } 

    // Check valid channel index
    if (channel < 0 || channel >= 4){
        return -1;
    } 

    // Get color index
    int colorIdx = colorIndex(color);

    if(colorIdx >= 0 && colorIdx < 7) {
        return calVals[sensorIdx][colorIdx][channel];
    }

    return -1;
}

bool ColorSensor::loadCalibration() {
    // Check if calibration flag exists
    int flag;
    EEPROM.get(eepromFlagAddr, flag);

    if (flag != flagValue) {
        return false;
    }
    
    // Load all calibration data from EEPROM
    for (int i = 0; i < 9; i++) {           // For each sensor
        for (int j = 0; j < 7; j++) {       // For each color
            for (int k = 0; k < 4; k++) {   // For each RGBW value
                EEPROM.get(eepromAddr[i][j][k], calVals[i][j][k]);
            }
        }
    }

    // Derive the per-sensor decision limits from what we just loaded.
    computeSeparations();

    return true;
}

bool ColorSensor::saveCalibration() {
    // Write order matters: invalidate, write data, verify, then validate.
    // Calibration takes tens of seconds and several physical cube rotations,
    // so a power loss, reset or abort part-way through must not leave a valid
    // flag over a half-old / half-new table that loadCalibration() would accept.

    // 1. Invalidate. Anything that reads the table from here until the final
    //    write will correctly conclude it is not calibrated.
    int invalid = 0;
    EEPROM.put(eepromFlagAddr, invalid);

    // 2. Write all calibration data.
    for (int i = 0; i < 9; i++) {           // For each sensor
        for (int j = 0; j < 7; j++) {       // For each color
            for (int k = 0; k < 4; k++) {   // For each RGBW value
                EEPROM.put(eepromAddr[i][j][k], calVals[i][j][k]);
            }
        }
    }

    // 3. Verify what actually landed before claiming success — re-reading via
    //    loadCalibration() would only copy the values back without comparing.
    for (int i = 0; i < 9; i++) {
        for (int j = 0; j < 7; j++) {
            for (int k = 0; k < 4; k++) {
                int readback;
                EEPROM.get(eepromAddr[i][j][k], readback);
                if (readback != calVals[i][j][k]) {
                    return false;           // flag stays invalid — table is not trusted
                }
            }
        }
    }

    // 4. Validate only now that the data is known good on the device.
    EEPROM.put(eepromFlagAddr, flagValue);

    // Keep the derived limits in step with the calibration they describe.
    computeSeparations();

    return true;
}

void ColorSensor::resetCalibration() {
    // NOTE: sensorSeparation[] is NOT recomputed here (nor by setColorCal(), nor
    // by a failed saveCalibration()); it is refreshed on the next successful
    // loadCalibration()/saveCalibration(). Until then classify() is working from
    // the previous table's limits.
    for (int i = 0; i < 9; i++) {
        for (int j = 0; j < 7; j++) {
            for (int k = 0; k < 4; k++) {
                calVals[i][j][k] = 0;
            }
        }
    }
}