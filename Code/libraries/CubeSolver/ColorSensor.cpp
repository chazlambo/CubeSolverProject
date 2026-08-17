#include "ColorSensor.h"


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

    // waitTime tracks it, because the two are one setting wearing two hats:
    // wait too little after a longer integration and every sticker is read
    // mid-conversion.
    //
    // The ratio is the one this machine actually runs — 300 ms of wait for 160
    // ms of integration, 1.875x. The declaration says "integration time * 2.5",
    // which would be 400; the comment has never matched the value. Preserving
    // the behaviour rather than the comment keeps a scan that works today
    // working, and 1.875x is already well clear of one conversion.
    waitTime = (integrationMsFor(index) * 300) / 160;

    applyIntegrationTime();
}

void ColorSensor::applyIntegrationTime() {
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
    digitalWrite(ledPin, ledState);
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
    // in software could previously see it: setColorCal only rejects negatives
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

    // colorDistance divides by the white channel. A zero W makes every distance
    // inf/NaN, which used to fall through to 'U' by accident; make it explicit.
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
    // bestD <= sep (i.e. fraction 1.0) is unachievable in practice because
    // run-to-run drift is roughly 70% of the separation itself, and it rejects
    // most legitimate readings.
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
    // Write order matters. This function previously wrote the valid flag FIRST,
    // then the data, then the flag again — so the table was marked good before
    // a single value landed. Calibration takes tens of seconds and several
    // physical cube rotations, so a power loss, reset or abort part-way through
    // left a valid flag over a half-old / half-new table, and loadCalibration()
    // happily returned true for it.
    //
    // Correct order: invalidate, write data, then validate.

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

    // 3. Verify what actually landed before claiming success. The old code
    //    ended with `return loadCalibration()`, which looked like a write
    //    verify but only re-read the flag it had just written and copied the
    //    values back into calVals[] without comparing anything.
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
    // Derived limits must be recomputed after ANY change to calVals — see the
    // note in the header. resetCalibration/setColorCal/a failed saveCalibration
    // all used to leave sensorSeparation describing the previous table.
    for (int i = 0; i < 9; i++) {
        for (int j = 0; j < 7; j++) {
            for (int k = 0; k < 4; k++) {
                calVals[i][j][k] = 0;
            }
        }
    }
}