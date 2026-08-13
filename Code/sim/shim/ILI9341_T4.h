// ILI9341_T4.h — desktop shim, backed by SDL.
//
// This is the piece that lets CubeDisplay.cpp run unmodified. LVGL still
// creates its own display and flush callback inside CubeDisplay::begin(); the
// only thing replaced is the panel underneath it, so the layout code being
// tuned here is byte-for-byte the layout code that ships.
//
// (LVGL ships its own SDL backend, lv_sdl_window_create(). Using that would
// bypass CubeDisplay's display creation entirely and the simulator would be
// exercising LVGL's setup rather than the firmware's.)
#ifndef ILI9341_T4_H_SIM
#define ILI9341_T4_H_SIM

#include "Arduino.h"
#include "SimHost.h"

namespace ILI9341_T4 {

class DiffBuffBase { public: virtual ~DiffBuffBase() {} };
template <int N> class DiffBuffStatic : public DiffBuffBase { char storage[N]; };

class ILI9341Driver {
public:
    ILI9341Driver(int, int, int, int, int, int, int, int) {}

    void output(void*) {}
    bool begin(uint32_t) { return sim::init(); }

    void setFramebuffer(uint16_t*) {}
    void setDiffBuffers(DiffBuffBase*, DiffBuffBase*) {}
    void setRotation(int) {}
    void setRefreshRate(int) {}
    void setVSyncSpacing(int) {}
    void clear(uint16_t) {}

    void updateRegion(bool redrawNow, uint16_t* px, int x1, int x2, int y1, int y2) {
        sim::blit(px, x1, x2, y1, y2);
        if (redrawNow) sim::present();
    }
};

}  // namespace ILI9341_T4
#endif
