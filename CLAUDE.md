# Notes for coding agents

`README.md` is the project's own documentation — board, wiring, build, first
run, error codes. Read it first. This file covers only the things that are not
obvious from the tree and that have already cost someone time.

## Build and verify

The sketch is `Code/Firmware/CubeSolver/CubeSolver.ino`. `Code/libraries/` is an
Arduino *sketchbook libraries* folder; Arduino's sketchbook location must point
at `Code/`.

**There is no Teensy toolchain in most sessions, so the usual loop is:**

```sh
cmake --build Code/sim/build -j && ./Code/sim/build/cubesim --scale 3
```

The desktop simulator (`Code/sim/`) compiles the **real firmware** against SDL
shims — the same `CubeDisplay`, `CubeMenu`, `RotaryEncoder` and the same
`lv_conf.h`, including its LVGL memory pool. Layout, fonts, navigation and
memory pressure are all faithful. It is the fastest way to see a UI change, and
the only way to catch an LVGL pool problem before the bench.

It replaces exactly one file: `CubeSystem.cpp` → `Code/sim/CubeSystemSim.cpp`.
**So edits to `CubeSystem.cpp` are compiled by nothing here.** Syntax-check them:

```sh
g++ -fsyntax-only -std=c++17 -DLV_CONF_INCLUDE_SIMPLE -ICode/sim/shim \
  -ICode/libraries/CubeSolver -ICode/libraries -ICode/libraries/kociemba \
  -ICode/sim/build/_deps/lvgl-src -ICode/sim/build/_deps/lvgl-src/src \
  Code/libraries/CubeSolver/CubeSystem.cpp
```

Menu navigation has host-side tests needing neither hardware nor LVGL — see the
header comment in `Code/tests/test_menu.cpp`. Run them after touching `CubeMenu`.

## Before changing anything the panel shows

**Read `docs/theme/UI_DESIGN.md` first.** The UI is a baked theme generated from
`docs/theme/*.html` by `Code/tools/bake_theme.py`, not drawing code, and it has
several failure modes that are completely silent:

- `LV_USE_LOG` is 0, so LVGL never reports anything.
- An exhausted LVGL pool **hangs the firmware** in a `while(1)` — no message, no
  crash, blank panel. Check the pool line `CubeDisplay::begin()` prints, or
  press `M` in the simulator.
- Alpha-only (A8) images are copied into RAM and will silently fail to appear;
  recolourable art is baked RGB565A8.
- Generated assets must stay in `Code/libraries/CubeSolver/utility/` — the
  Arduino 1.0 library format compiles no other subdirectory.

That document also has the palette, the type scale, the size limits, and recipes
for adding a menu item, a screen, or an operation screen.

## Conventions worth keeping

- **Tables, not code.** A menu screen is a `MenuScreen` + `MenuItem[]` in the
  `.ino`. `CubeMenu` knows nothing about what items do, and depends only on
  `<stdint.h>` so it stays host-testable.
- **Widgets are created once** in `CubeDisplay::buildUi()` and shown/hidden
  after — never created and deleted per screen.
- **Comments explain why, not what.** The existing code documents the reasoning
  behind non-obvious choices, including ones that look wrong until you know the
  constraint. Match that; do not strip it.
- **Long operations must keep the UI alive.** They call `pumpDelay()` /
  `pumpOnce()` so the panel refreshes and the abort chord is noticed mid-scan.
- Generated files (`utility/`, `docs/sim-main-menu.png`) are committed. Re-run
  the generator rather than hand-editing them.

## Known hardware fault, not a bug to fix

Colour sensor board 2, sensor 2 has a **dead green channel** — it reads 0 for
every sticker colour in every archived calibration run. The firmware detects it
(`ColorSensor::checkSensorHealth`). Low separation on board 2 in Calibration
Status is expected and real.
