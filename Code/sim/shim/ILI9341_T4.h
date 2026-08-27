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

    // setFramebuffer() and waitUpdateAsyncComplete() are the two halves of
    // CubeDisplay::repaintAll() and of the boot-frame wait in begin(), and both
    // are no-ops here for the same reason update() below is: there is no diff
    // engine to reset and no DMA to wait for. So repaintAll() on the desktop is
    // only the whole-screen invalidate that follows, and the wait is nothing.
    // Both are bench-only behaviour — confirm changes to them on hardware.
    void setFramebuffer(uint16_t*) {}
    void waitUpdateAsyncComplete() {}
    void setDiffBuffers(DiffBuffBase*, DiffBuffBase*) {}
    void setRotation(int) {}
    void setRefreshRate(int) {}
    void setVSyncSpacing(int) {}
    void clear(uint16_t) {}

    // The panel watchdog (CubeDisplay::panelHealthTick) reads these. The
    // simulated panel can neither lose its init nor fall asleep, so it reports
    // permanently healthy and the reassert is nothing — meaning the watchdog's
    // repair paths, like everything else this shim fakes, are proven only on
    // the bench.
    int selfDiagStatus() { return 0xC0; }
    void sleep(bool) {}

    void updateRegion(bool redrawNow, uint16_t* px, int x1, int x2, int y1, int y2) {
        sim::blit(px, x1, x2, y1, y2);
        if (redrawNow) sim::present();
    }

    // Push a whole frame, bypassing the differential path.
    //
    // A no-op here beyond presenting what has already been blitted, and that is
    // faithful rather than lazy: on the real panel this exists because the
    // driver only ever transmits pixels that differ from its own framebuffer,
    // so anything on the glass it does not know about survives until something
    // draws over it. The simulator has no glass and no diff engine — sim::blit
    // writes every pixel it is given — so there is nothing here for a forced
    // redraw to repair. The signature exists so CubeDisplay compiles unchanged.
    void update(const uint16_t*, bool = false) { sim::present(); }
};

}  // namespace ILI9341_T4
#endif
