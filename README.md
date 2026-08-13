# CubeSolver

A self-contained Rubik's Cube solving robot: six stepper-driven faces, two servo
grippers on a moving ring, eighteen colour sensors, an on-board Kociemba
two-phase solver, and a 320×240 display. Scan, solve, execute — no PC involved.

Everything here is one person's design: mechanical (SolidWorks), electrical
(KiCad), and firmware (Teensy 4.1).

---

## Status

**Working:** full autonomous scan → solve → execute cycle.

**Recently added:** menu-driven state machine, cooperative UI (the display stays
live during scan and solve), abort gesture, encoder fault handling, piece-level
cube validation with automatic scan repair, per-sensor colour confidence.

**Known unfinished:**
- `VirtualCube::rebuildFromCubeArray()` — marked `UNFINISHED NEEDS DEBUGGING`.
  The commented-out body is believed correct but has no rollback on failure.
- Colour sensor board 2, sensor 2 has a **dead green channel** — it reads exactly
  `0` for all six sticker colours in every archived calibration run. The firmware
  now detects this (`ColorSensor::checkSensorHealth`) but it is a hardware fault.
- Face steppers run without an acceleration ramp (`MultiStepper` is constant
  speed by design). Left as-is deliberately — a ramp would slow the solve.

---

## Which sketch do I flash?

**`Code/Firmware/CubeSolver/CubeSolver.ino`** — that's the program.

Everything under `Code/Test Code/` is bring-up and calibration tooling. The ones
still worth keeping:

| Sketch | Purpose |
|---|---|
| `Test Code/Actuator_Test` | Serial menu for manual moves and jam-recovery testing |
| `Test Code/Test_Motor_Calibrate` | Motor encoder calibration (also on the main menu) |
| `Test Code/Test_Color_Calibrate` | Colour calibration (also on the main menu) |
| `Test Code/I2C_Search` | Bus scanner — genuinely useful on a five-mux rig |

`Test Code/_Archive/` does not compile against the current tree (it includes a
`Cube.h` that no longer exists). Git history is the archive.

---

## Build

**Board:** Teensy 4.1. Not a 4.0 — the solver's lookup tables alone are 4.14 MiB,
which is 214% of a 4.0's flash, and pins 40/41 don't exist on a 4.0.

**Toolchain:** Arduino IDE + Teensyduino.

> **TODO:** record the exact Arduino IDE and Teensyduino versions you build with,
> plus CPU speed and USB type from the Tools menu.

**Sketchbook layout — this is the non-obvious step.** `Code/libraries/` is an
Arduino *sketchbook libraries* folder. Set Arduino's sketchbook location to the
`Code/` directory (File → Preferences → Sketchbook location). Then
`#include <CubeSystem.h>` and `#include <kociemba.h>` resolve, and `lv_conf.h`
is found automatically.

Do **not** copy the libraries into a different sketchbook — you'll edit the repo
and build a stale copy.

### Dependencies

None of these are vendored except Kociemba. Install via Library Manager unless
noted.

| Library | Source | Notes |
|---|---|---|
| AccelStepper | Library Manager / airspayce.com | GPL-2.0 |
| MultiStepper | ships inside AccelStepper | constant-speed by design |
| PWMServo | bundled with Teensyduino | Teensy-only; **not** the stock `Servo` |
| TCA9548 | RobTillaart/TCA9548 | I²C multiplexer |
| veml6040 | ThingPulse/VEML6040 | colour sensors |
| RunningMedian | RobTillaart/RunningMedian | scan filtering |
| Adafruit_seesaw | Adafruit | ANO scroll-wheel breakout |
| ILI9341_T4 | vindar/ILI9341_T4 (GitHub) | Teensy-4-specific DMA driver |
| LVGL | lvgl/lvgl | **v9.x** — the code uses v9-only APIs |
| EEPROM / Wire | Teensyduino core | two I²C buses used |
| SPI | Teensyduino core | pulled in transitively by ILI9341_T4, not included directly |
| kociemba | **vendored** at `Code/libraries/kociemba/` | from vindar/kociemba |

> **TODO:** pin the versions you actually build against, especially LVGL — the
> code uses `lv_display_create` / `lv_tick_set_cb`, which are v9-only.

### Menu theme assets

The menu's look is generated, not hand-drawn. `docs/theme/` holds the design as
two HTML files, and `Code/tools/bake_theme.py` rasterizes *those same files*
through headless Chrome at the panel's native 320x240, then packs the result
into LVGL C arrays under `Code/libraries/CubeSolver/utility/`:

```sh
python3 Code/tools/bake_theme.py              # images + fonts (needs network)
python3 Code/tools/bake_theme.py --no-fonts   # images only
```

The generated files **are committed**, so a normal build needs none of this —
run it only after changing the design. It needs `google-chrome`, `python3-pil`,
and `npx` for the font conversion.

Two things about those assets are load-bearing and easy to undo by accident:

- **They live in `utility/`.** `Code/libraries/CubeSolver` has no
  `library.properties`, so the Arduino IDE treats it as a 1.0-format library and
  compiles only the library root and `utility/`. Assets moved to a prettier
  subdirectory are silently not compiled, and the sketch fails to link.
- **The recolourable masks are RGB565A8, not A8.** LVGL reads an uncompressed
  RGB565A8 straight out of flash, but copies every alpha-only image into RAM
  first. The frame band is 49 KB against a 32 KB `LV_MEM_SIZE`, so as A8 it
  fails to allocate — and with `LV_USE_LOG` at 0 it fails *silently*, drawing
  nothing at all. This costs about 220 KB of flash and is worth it.

Total asset cost: **~565 KB of flash**, against the ~3.3 MB left after the
solver's own 4.14 MiB of lookup tables. RAM is unchanged.

### lv_conf.h

`Code/libraries/lv_conf.h` is **required** and its settings are not defaults.
`LV_COLOR_DEPTH 16` must match what ILI9341_T4 expects; a mismatch does not fail
to build, it renders wrong colours and looks like a hardware fault. See the
comments in that file for the load-bearing settings and for several defines that
use LVGL v8 spellings and are silently ignored by v9.

**`LV_MEM_SIZE` is 48 KB, and running out of it hangs the machine.**
`LV_USE_ASSERT_MALLOC` is 1 and LVGL's default assert handler is `while(1);`, so
an exhausted pool is not a degraded UI — it is a halt, with no message, because
`LV_USE_LOG` is 0. It was 32 KB, which the themed menu alone came within ~1 KB
of; adding the operation screens took the peak to ~27 KB and the firmware hung
at boot with no output at all. 48 KB was set against a measurement, not a guess.

Two things watch this now: `CubeDisplay::begin()` prints the pool usage over
Serial once every widget exists, and warns below 8 KB free; and `M` in the
desktop simulator prints it live. Check the boot line before assuming a new
screen is free.

---

## Wiring

Verified against the `TEENSY_4.1` footprint's pad-to-net mapping in
`Circuitry/KiCad/Cube Solver/Motherboard/Motherboard.kicad_pcb`.
`Code/libraries/CubeSolver/CubeHardwareConfig.cpp` is the single source of truth
in firmware.

| Pin | Signal | Pin | Signal |
|---:|---|---:|---|
| 0 / 1 | U step / dir | 18 / 19 | **I²C0** SDA / SCL |
| 2 / 3 | R step / dir | 22 / 23 | Bottom / Top servo |
| 4 / 5 | F step / dir | 24 / 25 | B step / dir |
| 6 / 7 | D step / dir | 26 | Stepper enable (all 7) |
| 8 / 9 | L step / dir | 28 / 29 | Ring step / dir |
| 10 | Display DC | 30 | Encoder mux /RESET |
| 11 / 12 | Display MOSI / MISO | 32 | Power sense |
| 13 | Display SCK (+ onboard LED) | 40 / 41 | Colour LED 2 / 1 |
| 14 / 15 | Display CS / RESET | 16 / 17 | **I²C1** SCL / SDA — see note |

**I²C addresses**

| Addr | Device | Bus |
|---|---|---|
| `0x70` | TCA9548 — encoder mux (7 AS5600 behind it) | Wire |
| `0x74`–`0x75` | TCA9548 ×2 — colour board 1 | Wire |
| `0x76`–`0x77` | TCA9548 ×2 — colour board 2 | Wire |
| `0x36` | AS5600 magnetic encoder ×7 | Wire (behind mux) |
| `0x10` | VEML6040 ×18 | Wire (behind muxes) |
| `0x49` | Adafruit seesaw (ANO wheel) | **Wire1** |

No pin or address conflicts.

### ⚠ Known board/firmware divergence

The firmware puts the scroll wheel on **Wire1 (pins 16/17)**, but Motherboard
Rev A routes connector J17 to **Wire0 (pins 18/19)**, and Teensy pads 16/17 are
marked unconnected. Since the machine works, there must be a bodge wire.

> **TODO:** confirm the bodge and either roll it into a Rev B or note it on the
> board. Right now this exists only in the hardware and in your head.

Two other things worth knowing:
- **Pin 10 must be the display DC.** ILI9341_T4 drives DC through the SPI
  peripheral's chip-select logic during DMA, so it has to be a hardware CS pin —
  on Teensy 4.1 SPI0 that's 10, 36 or 37. A plain GPIO fails confusingly.
- Pins 24/25 are Teensy's `Wire2`, consumed by the B stepper. There is **no third
  I²C bus available** without moving that motor.

---

## First run

Order matters — the solver needs both calibrations, and colour calibration needs
motors that already home correctly.

### 1. Motor encoder calibration

Menu → **Calibrate Motors**, or flash `Test Code/Test_Motor_Calibrate`.

Records four encoder positions per motor, one per quarter turn, into EEPROM.

> **TODO:** describe the physical setup — how the fingers should be positioned
> relative to the cube before starting, what a good result looks like, and when
> this needs redoing after a mechanical change.

### 2. Colour calibration

Menu → **Calibrate Colours**, or flash `Test Code/Test_Color_Calibrate`.

Needs a **solved** cube. Steps through orientations recording a reference RGBW
per colour per sensor, plus an "empty chamber" reference.

The firmware now derives a per-sensor decision threshold from this data
(`ColorSensor::computeSeparations`) rather than using one global tolerance, so a
bad calibration degrades classification measurably. Check the health report.

> **TODO:** describe ambient lighting conditions, whether the lid should be on,
> and how to tell a good calibration from a marginal one.

### 3. Servo endpoints

`topExtPos` / `botExtPos` in `CubeHardwareConfig.cpp`.

> **TODO:** record how these were determined and how to retune them after
> changing a servo horn or linkage.

---

## Operating

1. Power on. The machine homes its motors if calibrated.
2. Turn the wheel to select **Solve Cube**, press SELECT.
3. Insert a scrambled cube, press SELECT.
4. It scans (~25–40 s), solves, and reports the move count.
5. Press SELECT to execute.

**Abort:** hold SELECT for one second during a scan or solve. The machine stops
at the next mechanically safe point, releases the cube, and returns to the menu.

**On any error that can leave the machine holding the cube, it is released
automatically** — the solve paths and the abort paths all route through
`safeStop()`. Scan-time colour and build errors do not, because at those points
the ring and both servos are already retracted.

### Error codes

| Range | Meaning |
|---|---|
| Scan 1–3 | A sensor could not identify a face, or two faces read the same |
| Scan 1X/2X | Sensor 1/2's face was rejected when set on the virtual cube |
| Scan 3X | Could not determine orientation |
| Scan 41 | Fewer than six distinct faces set — a duplicate-centre misread |
| Scan 42-47 | Wrong number of some colour (not nine of each) |
| Scan 5X | Could not build the cube array |
| Scan 60 | Physically impossible cube, auto-repair failed → rescan |
| Scan 70 | Aborted by user |
| Scan 80 / 81 | Reorientation move ROTX / ROTZ failed (encoder fault) |
| Scan 90 | A colour sensor board failed to initialise at boot |
| Cal 82 | A calibration reorientation move failed |
| Solve 11 | Cube not scanned yet |
| Solve 12 | No solution — illegal cube or solver timeout |
| Solve 13 | Centres not canonical → **orientation** was misread |
| Solve 14 | Impossible piece → a **colour** was misread → rescan |
| Solve 15 | Solution longer than the move buffer |
| Exec 105 / 125 | Aborted by the user (between moves / inside a move) |
| Exec 1XX | A move failed; cube released and virtual state invalidated |
| Cal 8 | Colour calibration failed to save — machine is **not** calibrated |
| Cal 9 | Colour calibration aborted; EEPROM left untouched |

Code 14 and 60 both mean "the cube I think I have cannot exist" — the fix is a
rescan, not a retry.

**Code 13 is effectively unreachable, by design.** `buildCubeArray()` relabels
colours through the reported orientation, so the centres come out canonical
whatever that orientation was. A *misread* orientation therefore produces a
different but internally consistent cube that is legal, piece-valid and
solvable — the machine would execute ~20 moves in the wrong frame with no error
at any layer. The centre check is kept as cheap insurance against a corrupted
`cubeArray`; detecting an orientation misread needs a cross-check against
something outside the cube model and is **not currently implemented**.

---

## Repo layout

```
3D Models/      SolidWorks parts and assemblies, plus print-ready STLs
Circuitry/      KiCad projects: Motherboard, Motor Breakout, Color Sensor
Code/
  Firmware/CubeSolver/    <- THE PROGRAM
  Test Code/              bring-up and calibration sketches
  libraries/
    lv_conf.h             required LVGL config (see above)
    CubeSolver/           the machine's own code
    kociemba/             vendored solver (from vindar/kociemba)
```

`.gitignore` excludes regenerable KiCad output (auto-backup zips, footprint
caches, per-user project state) and SolidWorks lock files. `.gitattributes`
marks binary CAD/fab formats `-text` so git's heuristic can't CRLF-mangle them.

---

## Notes for future-you

- **The alignment step sign lives in one place.** `CubeSystem::kAlignStepSign`.
  Both closed-loop routines use `encError()`; if alignment ever drives away from
  target, flip that constant and nothing else.
- **Encoder reads can fail.** `MotorEncoder::scanChecked()` returns negative on
  I²C failure and every control path treats that as a fault, not a position.
  Never go back to raw `scan()` in a control loop.
- **`pumpDelay()` instead of `delay()`** anywhere that waits more than a few ms,
  so the display and abort keep working.
- Solve times: Kociemba runs from flash. `set_memory()` would be ~4× faster but
  the 479 KiB buffer doesn't fit alongside the display's ~200 KB of RAM2.
