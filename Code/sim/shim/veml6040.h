#ifndef VEML6040_H_SIM
#define VEML6040_H_SIM
#include "Wire.h"
#define VEML6040_IT_40MS    0x00
#define VEML6040_IT_80MS    0x10
#define VEML6040_IT_160MS   0x20
#define VEML6040_IT_320MS   0x30
#define VEML6040_IT_640MS   0x40
#define VEML6040_IT_1280MS  0x50
#define VEML6040_TRIG_DISABLE 0x00
#define VEML6040_TRIG_ENABLE  0x04
#define VEML6040_AF_AUTO      0x00
#define VEML6040_AF_FORCE     0x02
#define VEML6040_SD_ENABLE    0x00
#define VEML6040_SD_DISABLE   0x01
class VEML6040 {
public:
    bool     begin() { return true; }
    bool     begin(TwoWire*) { return true; }
    void     setConfiguration(uint8_t) {}
    uint16_t getRed()   { return 0; }
    uint16_t getGreen() { return 0; }
    uint16_t getBlue()  { return 0; }
    uint16_t getWhite() { return 0; }
    float    getAmbientLight() { return 0.0f; }
    uint16_t getCCT(float) { return 0; }
};
#endif
