#include "SimHost.h"

#include <SDL2/SDL.h>
#include <lvgl.h>   // for the LVGL heap report on M

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace sim {
namespace {

SDL_Window*   g_win     = nullptr;
SDL_Renderer* g_ren     = nullptr;
SDL_Texture*  g_tex     = nullptr;
bool          g_ready   = false;
bool          g_quit    = false;
int           g_scale   = 3;

uint16_t      g_fb[kPanelW * kPanelH];

int32_t       g_encoder = 0;
bool          g_faultArmed  = false;
bool          g_cubeToggle  = false;
bool          g_calToggle   = false;
int           g_shotIndex   = 0;

// Held-key state is read from SDL's keyboard array rather than tracked from
// events. The abort gesture is "SELECT and LEFT held together", so what matters
// is the instantaneous state of both keys, not the order their events arrived.
const uint8_t* g_keys = nullptr;

bool keyDownRaw(SDL_Scancode sc) {
    return g_keys != nullptr && g_keys[sc] != 0;
}

}  // namespace

void setScale(int scale) {
    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;
    g_scale = scale;
}

bool init() {
    if (g_ready) return true;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    g_win = SDL_CreateWindow("CubeSolver menu simulator",
                             SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             kPanelW * g_scale, kPanelH * g_scale,
                             SDL_WINDOW_SHOWN);
    if (!g_win) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
    if (!g_ren) {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }

    // Nearest, not linear: this window exists to judge a 320x240 layout, and
    // smoothing would hide exactly the single-pixel misalignments being hunted.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    // RGB565 in native byte order. CubeDisplay never calls
    // lv_draw_sw_rgb565_swap(), so what LVGL hands the flush callback under
    // LV_COLOR_DEPTH 16 is plain native-order RGB565 — the same thing this
    // texture expects. If the colors ever come out wrong here, that is a real
    // difference from the panel, not a simulator artefact.
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_RGB565,
                              SDL_TEXTUREACCESS_STREAMING, kPanelW, kPanelH);
    if (!g_tex) {
        std::fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return false;
    }

    std::memset(g_fb, 0, sizeof(g_fb));
    g_keys  = SDL_GetKeyboardState(nullptr);
    g_ready = true;
    return true;
}

void shutdown() {
    if (g_tex) SDL_DestroyTexture(g_tex);
    if (g_ren) SDL_DestroyRenderer(g_ren);
    if (g_win) SDL_DestroyWindow(g_win);
    g_tex = nullptr; g_ren = nullptr; g_win = nullptr;
    if (g_ready) SDL_Quit();
    g_ready = false;
}

bool quitRequested() { return g_quit;  }

void pumpEvents() {
    if (!g_ready) return;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            g_quit = true;
            break;

        case SDL_MOUSEWHEEL:
            // Wheel up scrolls the list up, i.e. decreases the count — the same
            // direction the arrow keys use below.
            g_encoder -= e.wheel.y;
            break;

        case SDL_KEYDOWN:
            // Key repeats are deliberately NOT filtered for the arrows: holding
            // one should keep stepping the encoder, exactly like spinning the
            // real wheel. Everything else ignores repeats.
            switch (e.key.keysym.sym) {
            case SDLK_UP:   g_encoder -= 1; break;
            case SDLK_DOWN: g_encoder += 1; break;
            case SDLK_ESCAPE: g_quit = true; break;
            default:
                if (e.key.repeat) break;
                switch (e.key.keysym.sym) {
                case SDLK_f:
                    g_faultArmed = !g_faultArmed;
                    std::printf("[sim] fault injection %s\n", g_faultArmed ? "ARMED" : "off");
                    break;
                case SDLK_c: g_cubeToggle = true; break;
                case SDLK_k: g_calToggle  = true; break;
                case SDLK_m:
                    // LVGL heap report. The simulator builds against the
                    // firmware's own lv_conf.h, so this is the Teensy's 64 KB
                    // pool, not the host's. Worth having a key for: LV_USE_LOG
                    // is 0, so a pool that fills up says nothing at all — it
                    // just silently stops drawing whatever it could not fit.
                    {
                        lv_mem_monitor_t mon;
                        lv_mem_monitor(&mon);
                        std::printf("[sim] LVGL heap: %u/%u bytes used (%u%%), "
                                    "free %u, largest free block %u, frag %u%%\n",
                                    (unsigned)(mon.total_size - mon.free_size),
                                    (unsigned)mon.total_size,
                                    (unsigned)mon.used_pct,
                                    (unsigned)mon.free_size,
                                    (unsigned)mon.free_biggest_size,
                                    (unsigned)mon.frag_pct);
                    }
                    break;
                case SDLK_p:
                    // Screenshot, for before/after layout comparisons.
                    {
                        char name[64];
                        std::snprintf(name, sizeof(name), "sim-shot-%02d.bmp", g_shotIndex++);
                        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(
                            g_fb, kPanelW, kPanelH, 16, kPanelW * 2, SDL_PIXELFORMAT_RGB565);
                        if (s) {
                            SDL_SaveBMP(s, name);
                            SDL_FreeSurface(s);
                            std::printf("[sim] wrote %s\n", name);
                        }
                    }
                    break;
                default: break;
                }
                break;
            }
            break;

        default:
            break;
        }
    }
}

void blit(const uint16_t* px, int x1, int x2, int y1, int y2) {
    if (!g_ready || px == nullptr) return;

    // Clip rather than trust. A wrong region here would walk off the end of the
    // framebuffer, and the resulting corruption would look like an LVGL bug.
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= kPanelW) x2 = kPanelW - 1;
    if (y2 >= kPanelH) y2 = kPanelH - 1;
    if (x2 < x1 || y2 < y1) return;

    const int w = x2 - x1 + 1;
    for (int y = y1; y <= y2; ++y) {
        std::memcpy(&g_fb[y * kPanelW + x1], &px[(y - y1) * w], (size_t)w * 2);
    }
}

void present() {
    if (!g_ready) return;
    SDL_UpdateTexture(g_tex, nullptr, g_fb, kPanelW * 2);
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, nullptr, nullptr);
    SDL_RenderPresent(g_ren);
}

// --- input -----------------------------------------------------------------
// SELECT and LEFT each accept two keys so the abort chord can be held
// comfortably with one hand.
bool keySelect() { return keyDownRaw(SDL_SCANCODE_RETURN) || keyDownRaw(SDL_SCANCODE_SPACE); }
bool keyLeft()   { return keyDownRaw(SDL_SCANCODE_LEFT)   || keyDownRaw(SDL_SCANCODE_BACKSPACE); }
bool keyRight()  { return keyDownRaw(SDL_SCANCODE_RIGHT); }
bool keyUp()     { return keyDownRaw(SDL_SCANCODE_W); }
bool keyDown()   { return keyDownRaw(SDL_SCANCODE_S); }

int32_t encoderPosition() { return g_encoder; }

bool consumeFaultInjection() {
    // Consumed, not merely read: an armed fault fires once and disarms, so a
    // stray press cannot leave the machine failing every operation.
    if (!g_faultArmed) return false;
    g_faultArmed = false;
    std::printf("[sim] injecting fault\n");
    return true;
}

bool consumeCubeToggle() { bool v = g_cubeToggle; g_cubeToggle = false; return v; }
bool consumeCalToggle()  { bool v = g_calToggle;  g_calToggle  = false; return v; }

// std::chrono rather than SDL_GetTicks/SDL_Delay: millis() and delay() are
// called by static initialisers and by setup() before ILI9341Driver::begin()
// has brought SDL up, and a clock that reads zero until then would make every
// elapsed-time comparison in the firmware behave strangely exactly once.
unsigned long millisNow() {
    using namespace std::chrono;
    static const steady_clock::time_point t0 = steady_clock::now();
    return (unsigned long)duration_cast<milliseconds>(steady_clock::now() - t0).count();
}

void sleepMs(unsigned long ms) {
    if (ms == 0) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

}  // namespace sim
