# Panel UI — design notes

How to add to the 320x240 menu without it looking bolted on, and the traps that
will cost you an afternoon if you meet them cold. Read this before writing any
`lv_*` call.

The short version: **the UI is a baked theme, not drawing code.** Almost nothing
is drawn with LVGL primitives. Shapes come from images generated out of the
design files; code positions them and swaps text. If you find yourself reaching
for `lv_obj_set_style_*` to draw a new *shape*, you are probably about to make
something that does not match — bake it instead.

---

## 1. Where everything lives

| What | Where |
|---|---|
| Design source of truth | `docs/theme/melee-menu-lvgl-preview.html` (native 320x240) |
| Full-resolution master | `docs/theme/melee-menu-mockup.html` (1200x900) |
| Asset baker | `Code/tools/bake_theme.py` |
| Generated assets + fonts | `Code/libraries/CubeSolver/utility/` |
| Generated placement constants | `Code/libraries/CubeSolver/utility/CubeThemeAssets.h` |
| Renderer | `Code/libraries/CubeSolver/CubeDisplay.{h,cpp}` |
| Menu engine (no LVGL) | `Code/libraries/CubeSolver/CubeMenu.{h,cpp}` |
| Menu tables + screens | `Code/Main Code/CubeSolver/CubeSolver.ino` |
| Operation sequence shape | `Code/libraries/CubeSolver/CubeOpShape.cpp` |
| Desktop simulator | `Code/sim/` — see its README |

---

## 2. The visual language

**The frame band is the identity.** One continuous band around the content,
recolored as you move. In a menu it takes the color of the **selected item** —
turning the wheel recolors the whole frame. It is the one piece of state
readable from across the room, so it is worth keeping honest.

**The color says WHERE YOU ARE, not what the machine is doing.** A branch of
the menu tree is one color from the row that opens it, down through its
submenus, and out through the operation screens that branch leads to. Yellow
means "somewhere under Settings" whether you are reading About or watching a
motor calibration sweep. This is the Melee menu the design is modelled on,
where the whole Options branch is one color and the whole Trophies branch is
another.

| Theme | Fill | Edge | Branch |
|---|---|---|---|
| Blue | `#1D2680` | `#6D7CF0` | main menu, Load & Scan, Solve — the main line of work. Also Demo Mode |
| Red | `#6E1A10` | `#F0603A` | Modes, and Scramble Solve. Also every Error screen, always |
| Green | `#14522A` | `#4ADE70` | Eject Cube. Also Patterns |
| Yellow | `#6E5A10` | `#E8CF3A` | Settings and its entire subtree. Also Idle Mode |
| Purple | `#33176E` | `#8A62E8` | Stats. Also Step Solve |
| Violet | `#4A1A72` | `#AE6AF0` | no branch — kept for the Idle Mode cycle and for a future one |

Modes is the branch that fans out: the screen is red, but each of the five
modes owns a color and keeps it through its own operation screens. A running
mode is somewhere you can be for minutes, and the five are five different
things that happen to be filed together.

**This replaced an older rule**, which was that the frame reported machine
state — blue scanning, violet calibrating, green solving. The two schemes
fought over one band: the menu already recolored by selection, so a solve
launched from a blue row turned green for no reason the operator could see.
Wayfinding won. `CubeDisplay::OpKind` still maps `Scan`→Blue, `Solve`/`Done`→
Green, `Calibrate`→Violet, `Info`→Yellow, but that mapping is now only the
fallback for a caller that has not named a branch.

**Two exceptions, and only two.**

- **`Error` is red, always**, overriding the branch. A fault has to be
  unmistakable, red already means "stopped" everywhere in this machine, and a
  blue solve that failed into a blue screen would say nothing went wrong.
  Enforced in `CubeDisplay::setOpTheme()`, which refuses to repaint an Error
  screen, rather than left to callers to remember.
- **Idle Mode cycles all six colors**, one per move, on purpose: it is the
  screen whose whole job is looking alive from across a room. Idle Mode's
  *other* screens — the clamp, the solve it hands off to — wear its yellow.

**How a screen gets its color.** `CubeSolver.ino` keeps `s_opTheme`, set in
one place: `loop()` copies `CubeMenu::themeOf()` off the row under the cursor
just before `Menu.handle()` runs it. Every operation screen goes through the
sketch's `opScreen()` helper, which calls `showOperation()` and then
`setOpTheme(s_opTheme)`. So a new screen inherits its branch's color by
existing, and nothing in the sketch calls `showOperation()` directly.

**Typography** — five baked fonts, each with one job. Do not introduce a sixth
without a reason; each costs ~20-40 KB of flash.

| Font | Face | Used for |
|---|---|---|
| `lv_font_title_13` | Anton 13 | screen title, top-left at (52, 21) |
| `lv_font_head_16` | Fira Sans Cond. Bold 16 | operation headline |
| `lv_font_bar_12` | Fira Sans Cond. Bold 12 | menu bar labels, status rows |
| `lv_font_desc_11` | Anton 11 | the description/hint box |
| `lv_font_prev_9` | Fira Sans Cond. Bold 9 | preview pane, chip row labels |

**The baked fonts carry ASCII only** — `0x20-0x7F`, which is what the baker asks
`lv_font_conv` for. Arrows, angle quotes and box-drawing characters are not in
them, and a missing glyph draws as a blank box with no complaint (see 4.5). Use
`<` and `>`, not `‹` and `›`, or widen the range in `bake_theme.py` and accept
the flash.

**The description box is always the bottom line.** 182x21 at (69, 199). In a
menu it carries the selected item's caption; on an operation screen it carries
the hint ("Press SELECT", "SELECT+LEFT to abort"). Same box, same place, always
— that consistency is most of why operations do not feel like a different
program.

**The cursor means "this is the one".** Orb + inward sonar rings + spinning
comma. It marks the selected menu item. Do not use it decoratively.

**Bar art means "you can select this".** The glowing bar with the socket is the
menu's vocabulary for a *choice*. It was briefly reused to show scan progress
and the result read as a screen full of buttons that did nothing — the shape
promised an interaction that was not there. Status belongs to chips, the
progress bar, the status table or plain text. When something genuinely is a list
of choices, bars are right and should be used.

`docs/theme/SCREEN_PLAN.md` records how every screen was designed — Patterns,
Idle, Step Solve, Cube State, Parameters and the rest — and which shared pieces
exist. Read it before designing a new screen or changing one; the piece you
want may already be built.

---

### The dial vs the progress bar

Both are round-ish things that fill up, and they say opposite things.

| | Progress bar | Dial |
|---|---|---|
| Means | how far through a job | where a SETTING sits in its range |
| Driven by | the machine | the wheel |
| While it moves | you wait | you are turning it |

A screen showing both is claiming two different kinds of "how far along", so
`setOpDial()` and `setOpProgress()` should not appear together — the one place
that could have happened, Idle Mode's solve, hides the dial explicitly.

The dial follows the frame color, so it belongs to the screen rather than
sitting on it. Its caption goes UNDER the arc: the inner diameter is about 60 px
and any caption worth writing is wider, so one placed in the middle draws across
the stroke on both sides.

## 3. Hard limits to design within

These are not style preferences; exceeding them clips, ellipsises or collides.

| Limit | Value | Why |
|---|---|---|
| Menu items per screen | **5** | `kVisibleRows`. The zigzag layout has five slots. A sixth wants a submenu, not a scrollbar. |
| Menu caption | **~34 chars** | 182 px at Anton 11, else ellipsised. |
| Preview entries | **5 max, ~13 chars** | `PREV_W` is 54 px; the pane is a perspective trapezoid whose right border sits near x=282, *not* at the asset's bounding box. |
| Operation body rows | **7** | `kOpLines`, 216 px wide from x=52. Seven, because the machine has seven motor encoders. |
| Calibration chips | **6 colors x 2 boards** | fixed by the hardware. |
| Panel | **320x240, RGB565** | 16-bit color quantises; flat fills and clearly-separated colors hold up, near-neighbour gradients band. |

Row geometry changes with item count: 5 items start at y=70 with a 27 px pitch,
fewer start at y=76 with 29. `CubeDisplay::barBoxPos()` owns this.

---

## 4. Traps that cost real time

### 4.1 Running out of LVGL memory HALTS the machine, silently

`LV_USE_ASSERT_MALLOC` is 1 and LVGL's default assert handler is `while(1);`.
`LV_USE_LOG` is 0. So an exhausted pool is not a degraded UI — it is a hang,
with no message, no crash, nothing on the panel and nothing on Serial.

This has already happened once: adding the operation screens took the peak over
the then-32 KB pool and the firmware hung at boot producing no output at all.

**Rule:** after adding widgets, check the pool. `CubeDisplay::begin()` prints it
over Serial once every widget exists; `M` in the simulator prints it live.
The pool is 64 KB (`LV_MEM_SIZE`); the boot line says how much the widget set
takes — ~35 KB at the last raise. Leave at least 8 KB free — a draw layer needs
it.

### 4.2 A8 images are copied into RAM; RGB565A8 is read from flash

LVGL's bin decoder hands an uncompressed variable-source image straight to the
draw unit *unless* it is alpha-only. `decode_alpha_only()` calls
`lv_draw_buf_create()` and copies the whole image into a fresh `w*h` RAM buffer.

A 255x194 A8 band is 49 KB. It did not fit, so it failed to allocate, so the
frame band simply **never appeared** — silently.

**Rule:** anything recolorable at runtime is baked **RGB565A8** (white pixels +
alpha), never A8. Costs 3 bytes/px instead of 1; that is the whole reason the
asset total is ~565 KB. Flash this project has; RAM it does not.

### 4.3 Assets must live in `utility/`

`Code/libraries/CubeSolver` has no `library.properties`, so the Arduino IDE
treats it as a 1.0-format library and compiles **only the library root and
`utility/`**. Sources anywhere else are silently ignored — the sketch builds and
then fails to link. Do not "tidy" the assets into a prettier subdirectory.

### 4.4 You cannot transform a container

Rotating or scaling an `lv_obj` renders it through a layer, and a *transformed*
layer is allocated whole up front — 150 KB for a full-screen group, ~19 KB for a
single bar box. Neither fits.

Transformed **images** are fine: they stream through a small bounded buffer.
That is why the wheel transition moves each bar along its arc (`placeBarsAt()`)
instead of rotating the group, and why the sonar rings and comma are `lv_image`
widgets rather than styled objects.

### 4.5 Everything fails silently

`LV_USE_LOG` is 0. A missing glyph renders as a blank box, an unallocatable
image renders as nothing, a full pool hangs. **There is no error path that talks
to you.** So: change one thing, look at it in the simulator, then continue.
Never batch five visual changes and build once.

### 4.6 An `lv_image_dsc_t` must be complete BEFORE `lv_image_set_src()`

`lv_image_set_src()` reads the header there and then, to size the widget. Point
it at a descriptor you have not filled in yet and the widget is 0x0 and draws
nothing — silently, of course. This bit the preview-pane cube net: the image was
created up with the menu widgets, and its descriptor was populated later,
alongside the operation-screen ones.

Build the descriptor, then create the image. If a runtime-drawn image is blank,
check that order first.

### 4.7 No C99 compound literals in the sketches

`(char[2]){ c, 0 }` is C99. In C++ it is a GNU extension with different lifetime
rules, and GCC accepts it without a word — the string simply came out empty and
the whole row vanished from the screen. Build the value into a named buffer, or
use a table of `const char*`.

### 4.8 (bonus) `bake_theme.py --no-images` leaves the header stale

`CubeThemeAssets.h` carries both image placement constants and font
declarations, and only the rasterizer can write the image half. If you add a
font, run a **full** bake. The script now says so, but it will not stop you.

---

## 5. What LVGL cannot do here, and what to do instead

| Wanted | LVGL | Do this |
|---|---|---|
| Skewed / italic title | no text transform | bake a title image, or accept upright |
| Per-corner radius | single `radius` | bake the shape (the preview pane is baked for exactly this) |
| Text outline / stroke | none | pick colors with enough contrast, or bake |
| Gaussian glow | none per-frame | bake it into the sprite |
| Vertical text | none | bake it rotated (`theme_next_label`) |
| Perspective | none | bake pre-skewed |

**Deviations already made deliberately — do not "fix" them back:**

- Title and description use **Anton**, a libre stand-in for Impact. The design's
  `skewX(-8deg) scaleY(.84)` on the title is dropped.
- Preview pane text is **upright inside a tilted frame**. Baking the text would
  freeze menu labels into an asset.
- The wheel transition **does not tilt the bars** (see 4.4). The arc paths are
  exact; only per-bar rotation is missing.
- **No leaf-confirm blink.** Every leaf action in this firmware navigates away
  from the menu immediately, so the blink would be drawn and instantly
  overwritten. Add it only if a leaf ever stays put.
- Preview entries are **abbreviated** ("Motor Positions" → "Motors") to fit 54 px.

---

## 6. Recipes

### Add a menu item

Edit the table in `CubeSolver.ino`. Nothing else needs touching.

```c
{ "Label", &kScreenSub, nullptr, "Caption under 34 chars.",
  kPrevSomething, 3 },
```

`submenu` **or** `action`, never both. `preview` non-null spells out what the
submenu contains; leave it null for an item that starts an operation and the
pane draws the placeholder plates instead. Three-field
`{ label, submenu, action }` still compiles — the rest default.

**Omit `theme` unless the item leaves its branch.** Inheriting the screen's is
what keeps a branch one color by construction instead of by every row agreeing,
and the color is now wayfinding (see §2). The tree names a theme in exactly
three places: the top-level rows, the five Modes rows, and the Patterns rows —
and the last of those are a preview device rather than a place, which is why
`actPattern()` overrides `s_opTheme` back to the screen's green.

If a `preview` list is edited, fix the item's `previewCount` and the screen's
`count` in the same breath. They are parallel numbers, not derived ones, and a
stale one either mislabels the pane or walks off the end of the table.

### Add a menu screen

A screen is a table plus a `MenuScreen`. Five items maximum. Give it a theme;
give each item a caption. Add an `extern const MenuScreen` near the top if the
tables reference each other.

### Add an operation screen

```c
showOp(Op::Calibrate, "Servo Calibration", "Setting endpoints", "Do not touch");
```

`showOp()` is `opScreen()` plus the flush; use `opScreen()` if you are about to
decorate and want one repaint rather than two. Either way the frame comes out
the color of the branch you are in — `kind` only names what sort of screen this
is, and picks the fallback color if no branch has been set. Use `Op::Error`
when, and only when, something has gone wrong: it pins the frame red.

Then optionally decorate — these are separate calls because most screens want
only one:

- `cubeDisplay.setOpLines(rows, n)` — up to 7 rows (`kOpLines`). `"Label\tValue"` renders as
  an aligned pair; no tab means a full-width line.
- `cubeDisplay.setOpSteps(labels, n, active, done)` — named steps drawn with the
  menu's own bar art. **Takes over from the headline**, which it hides.
- `cubeDisplay.setOpChips(bits, boards)` — the calibration color grid.
- `cubeDisplay.setOpProgress(done, total)` — a filled bar.

Steps and chips both want the middle of the screen; do not use both at once.

### Report progress from a long operation

The operation lives in `CubeSystem`, which knows how far along it is. Call
`displaySetStatus()` / `displaySteps()` / `displayChips()` / `displayProgress()`
from inside it, then `displayUpdate()`. `setMessage`/`setStatus` update a live
operation screen in place rather than rebuilding it, so they are cheap to call
in a loop.

**Update before the slow part, not after.** A scan pass is ~5.4 s of color
integration; a progress display that updates once it finishes is not a progress
display.

If the sequence has a shape (passes, stages, rotations), put the labels in
`CubeOpShape.cpp`. The simulator replaces `CubeSystem.cpp` but still compiles
that file, which is what stops the simulator drifting from the machine.

### Change the artwork

Edit `docs/theme/melee-menu-lvgl-preview.html`, re-run
`python3 Code/tools/bake_theme.py`, rebuild. Placement constants are measured by
the rasterizer and regenerated, so widgets follow the art without hand-editing
coordinates.

---

## 7. Verifying

The simulator runs the **real firmware** — the same `CubeDisplay`, the same
`lv_conf.h`, the same 64 KB pool. Use it; do not design against the HTML.

```sh
cmake --build Code/sim/build -j && ./Code/sim/build/cubesim --scale 3
```

`P` screenshots to `sim-shot-NN.bmp` (native 320x240, gitignored). `M` prints
the LVGL pool. `C` toggles the cube state to reach both main menus. `F` arms a
fault to reach the error screens.

Driving it from a script has three traps, all documented in `Code/sim/README.md`:
SDL creates more than one X window (try each and keep the one that takes
focus), buttons are level-sampled every 25 ms so a press must be **held**, not
tapped, and focus drifts mid-run. Kill leftover instances with `pkill -x cubesim`
— `pkill -f cubesim` matches your own shell.

For a visual check, render the preview at 320x240 with headless Chrome and
compare side by side. That is how the original port was validated to near
pixel-accuracy.

The menu engine has host-side tests that need no hardware and no LVGL:

```sh
cd Code/tests && g++ -std=c++14 -Wall -Wextra -I../libraries/CubeSolver \
  test_menu.cpp ../libraries/CubeSolver/CubeMenu.cpp -o /tmp/test_menu && /tmp/test_menu
```

**The Teensy build is not verifiable from the simulator.** `CubeSystem.cpp` is
not compiled there at all. A useful stopgap for edits to it:

```sh
g++ -fsyntax-only -std=c++17 -DLV_CONF_INCLUDE_SIMPLE -ICode/sim/shim \
  -ICode/libraries/CubeSolver -ICode/libraries -ICode/libraries/kociemba \
  -ICode/sim/build/_deps/lvgl-src -ICode/sim/build/_deps/lvgl-src/src \
  Code/libraries/CubeSolver/CubeSystem.cpp
```

---

## 8. Budgets

| Resource | Used | Of |
|---|---|---|
| Theme assets (flash) | ~565 KB | ~3.3 MB free after the solver's 4.14 MiB of tables |
| Generated source | 3.7 MB of `.c` | committed, like the kociemba tables |
| LVGL pool | ~35 KB after UI build (see the boot line) | 64 KB (`LV_MEM_SIZE`) |
| Framebuffers (RAM) | ~195 KB | 1 MB on a Teensy 4.1 |

Flash is plentiful, the LVGL pool is not. When adding widgets, prefer reusing
existing ones: the operation screens reuse the menu's bar art for step rows and
`lbl_msg`/`lbl_status`/`box_desc` for the headline, sub-line and hint bar, which
is why they cost almost nothing.

Widgets are created **once** in `buildUi()` and shown/hidden thereafter — never
created and deleted per screen. Churning LVGL objects at menu speed fragments
the pool, and the failure mode of that is 4.1.
