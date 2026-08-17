# Screen plan — what the rest of the machine needs to look like

Every menu item that currently lands on "Not implemented yet", designed before
it gets written. The point is not to lock anything down; it is so that the tenth
screen looks like it belongs with the first, and so the shared pieces get built
once instead of five slightly different times.

Read `UI_DESIGN.md` first — it covers the palette, the limits and the traps.
This file is only about screens that do not exist yet.

---

## The rule everything here follows

**Bar art means "you can select this."**

The glowing bar with the socket and the cursor orb is the menu's vocabulary for
a choice. It was briefly reused to show scan progress, and the result read as a
screen full of buttons that did nothing — the shape promised an interaction that
was not there.

So: a bar is a thing you can pick. Status is chips, a progress bar, a table, or
text. Never a bar. When something is genuinely a list of choices — Patterns,
Parameters — bars are exactly right and should be used.

Second rule, from the same place: **the frame colour is the state.** Blue
scanning, green solving/done, violet calibrating, yellow info/settings, red
stopped. A new screen picks the one that already means what it means.

---

## Vocabulary we have

| Piece | Call | Says |
|---|---|---|
| Frame colour | `showOperation(kind, …)` | what kind of thing is happening |
| Title | `showOperation(…, title, …)` | where you are |
| Headline / sub-line | `showOperation(…, headline)` / `setStatus()` | what it is doing now |
| Status table | `setOpLines()`, `"Label\tValue"` | facts, up to 6 rows |
| Face row | `setOpFaces()` | the six faces, filled with the colour actually read |
| Chip rows | `setOpChips()` | a set being collected, two rows |
| Progress bar | `setOpProgress()` | how far through a countable job |
| Hint box | `showOperation(…, hint)` | what button to press |
| Preview pane | menu only | what is behind this item |

## Vocabulary we still need, ranked

~~1. **Cube net**~~ — **BUILT.** `setOpCubeNet()`, and Cube State uses it. See
   "On the cube net" below for how, and the one thing still missing from it.

1. **Move ribbon** — a sequence with a cursor on the current entry. Step Solve
   needs it; Solve and Demo get better with it.
2. **Scrolling status list** — Fault Log. More than six rows with a position
   readout.
3. **Value editor** — Parameters and Servo Positions. The wheel changes a
   number instead of moving a cursor. This is a new *interaction*, not just new
   art, and is the one worth thinking hardest about.
4. **Row status marks** — a status row whose value is green/red rather than
   grey. Hardware Test wants it; small change to `setOpLines()`.

### On the cube net — built, and how

`setOpCubeNet(facelets)` takes 54 colour letters in the standard order and draws
the unfolded cross. `VirtualCube::getColorArray()` returns exactly that.

It is **not** LVGL primitives and **not** `lv_canvas`. It writes RGB565A8 into a
plain static buffer and hands that to an `lv_image`. Three reasons, all of which
apply to the next thing like it:

- 54 stickers as objects would be most of the pool for one screen.
- The buffer (39 KB at 10 px stickers) is several times the whole LVGL pool, so
  it must be a static array, not an `LV_MEM` allocation. Teensy RAM has room;
  the pool does not.
- LVGL reads an uncompressed variable-source image **straight out of the buffer
  without caching it** (`use_directly` in `lv_bin_decoder`). So redrawing is
  "rewrite the bytes, call `lv_obj_invalidate()`" — no reallocation, no cache
  invalidation dance.

Gaps between stickers are alpha 0, so the net sits on the themed backdrop rather
than on a grey slab. A facelet that is not a known colour is drawn as a hollow
outline, so a bad sticker reads as wrong rather than as missing.

**Still missing:** per-face rotation for a *raw* scan. Cube State falls back to
showing the last scan when nothing was built, but each face is laid out as its
sensor saw it — `setOrientation()` resolves that only when the cube is built. A
stray sticker is visible; where it sits within its face may be turned. The
screen says "last scan, not built" rather than pretending otherwise.

```
        ┌──┬──┬──┐
        │  U     │            standard unfolded cross:
        ├──┼──┼──┼──┬──┬──┐   4 faces wide, 3 tall
        │  L  │  F  │  R  │  B     at 10 px/sticker -> 132 x 99
        ├──┼──┼──┼──┴──┴──┘   centred, with the sub-line below it
        │  D     │
        └──┴──┴──┘
```

---

## Screen by screen

Sketches are the ~240x150 content area inside the frame. The title sits top-left
and the hint box along the bottom on every one of them.

### Scramble Solve — Modes

Two phases, and the frame colour should carry that: **red** while scrambling,
**green** the moment it starts solving. Nothing else on the screen has to
change for the operator to know which half they are watching.

```
              Scrambling                    <- headline, per phase
             Move 7/25   R'                 <- sub-line
        ▓▓▓▓▓▓▓▓▓░░░░░░░░░░░░░              <- progress bar
```

Reuses everything. Needs nothing new. Build this first — it is the cheapest of
the Modes and it proves the phase-colour idea.

### Idle Mode — Modes

The machine turning slowly to look alive. The screen's job is to be worth
glancing at from across the room and to say the machine is awake and idle, not
broken.

```
             Ready                          <- large, calm
        Best      12.4 s                    <- status table, last/best
        Solves    128
                                            <- frame slowly cycles the six
                                               theme colours, ~8 s each
```

Giving it the stats table means it earns its place instead of being a
screensaver. The slow frame cycle reuses `applyTheme()` on a timer — the one
place a colour change is decorative rather than semantic, which is fine because
nothing else is happening. Hint: "SELECT to wake".

### Demo Mode — Modes

Scramble, solve, repeat, unattended. Same as Scramble Solve plus a run counter,
and the numbers people actually want to see when it is showing off.

```
              Solving                       <- phase
             Move 14/21   L2
        ▓▓▓▓▓▓▓▓▓▓▓▓▓░░░░░░░░░
        Run 7      Best 11.8 s              <- one compact status row
```

### Step Solve — Modes

One move per press. This is what the **move ribbon** is for.

```
             Move 7 of 21

     R   U2   F'   ⟨ L ⟩   D   B2   R'      <- current in cursor yellow,
                                               past dimmed, future grey
```

Deliberately **plain text tokens, no boxes** — boxed tokens would drift back
toward looking like buttons. Colour alone carries the cursor.

`setOpRibbon(moves, count, current)` centres the window on the current move and
shows about seven. Hint: "SELECT for next move".

### Patterns — Modes

A *menu*, so bars are correct here — and the preview pane is the obvious home
for a small cube net of what each pattern produces. That is the pane doing
exactly what it was designed for.

```
   ╭─ Checkerboard ─╮        ┌──────┐
   ╰────────────────╯        │ net  │   <- preview pane, ~66x96,
   ╭─ Cube in Cube ─╮        │      │      net at 5 px/sticker
   ╰────────────────╯        └──────┘
   ...
```

**Prototyped** in `Test_Menu` (Screens > Patterns). `MenuItem::previewNet` holds
54 facelets and `setPreview()` draws them in the pane at 3 px stickers, sharing
one `drawNet()` with the full-size version so the two cannot disagree about what
a cube looks like.

The four states are computed by applying each well-known sequence to a solved
cube and checked for nine of every colour — a preview that is not a real cube
would hide exactly the bugs this screen is for.

What remains is the machine side: running the moves. The screen is done.

### Cube State — Diagnostics — **BUILT**

The net plus a colour count. Nine of each is the cheapest check that the stored
state is a cube at all, and unlike "invalid" it says *which* colour was misread.

It doubles as the **scan review**: with nothing built but a scan recorded, it
shows the raw readings instead of an empty screen — which is the case where
somebody most wants to look. See the caveat above.

Still worth adding: mark the low-confidence stickers. `scanAlt` and `scanConf`
already record the runner-up colour and the confidence for every sticker, so the
data is there — a hollow or outlined sticker for "the classifier was unsure
about this one" would turn this from *what it read* into *what to distrust*.

### Fault Log — Diagnostics

A list nobody selects from, so status rows rather than bars.

```
        12:04   Solve stopped      122
        11:58   Scan failed         60
        11:51   Aborted             25
        ...
```

Six rows at a time, wheel scrolls, and the position goes in the hint box —
"7-12 of 24" — rather than inventing a scrollbar the theme has no art for.

### Hardware Test — Diagnostics

**Not a self-test.** It is manual control: drive each actuator to each position,
by hand, one at a time. The panel version of `Test Code/Actuator_Test`, which is
a numbered list over Serial. It is where you go to answer "does that servo
actually reach retract", and where you drive one part at a time to show the
machine off.

So it is a MENU, and bars are correct throughout. Grouped by part, because five
items is the screen limit and there are six face motors — opposite faces share a
screen, which is a grouping that means something rather than an arbitrary split:

```
   Actuators
   ├─ Top Servo      -> Extend / Partial / Retract
   ├─ Bottom Servo   -> Extend / Partial / Retract
   ├─ Ring           -> Extend / Middle / Retract
   ├─ Face Motors    -> Up/Down -> U 90, U -90, D 90, D -90
   │                    Left/Right, Front/Back likewise
   └─ Cube Rotate    -> Rotate X / Rotate Z
```

A move that works goes **straight back to the menu**, cursor still on the item
you fired. This is a jog tool: you press it repeatedly to watch a motor, and a
result screen demanding SELECT between presses would make that miserable. Only a
FAILURE stops and says so, with the move and the code.

While a part is moving the panel says so — the servo sweeps take seconds, and
the cooperative pump refreshes the display throughout, so without it the panel
would sit on the old menu looking frozen.

**Built** in `Test_Menu` under Actuators. What remains is porting the tree into
the firmware's Diagnostics menu; the actions already call the real CubeSystem
methods.

### Sensor Test — Diagnostics

Live numbers, refreshed. **Throttle it** — every refresh is I²C traffic on the
bus the encoder is also using; 20 Hz is plenty, and `Test_Menu`'s Input Report
already demonstrates the pattern.

```
        Board 1        9/9 healthy
        Board 2        8/9 healthy
        Centre 1       R  (0.82)
        Centre 2       G  (0.79)
        ██ ██ ██ ██ ██ ██                  <- live detected colour per sensor
```

The chip row doubling as a live colour readout is free and makes a wall of
numbers legible at a glance.

### Parameters / Servo Positions — Diagnostics, Calibration

The one that needs a genuinely new interaction: the wheel has to change a
*value*, not move a cursor.

Proposal — a settings list, kept separate from `CubeMenu` rather than bolted
onto it. `CubeMenu` is deliberately navigation-only and host-testable, and
value editing is a different job.

- Rows are bars (they *are* selectable — this is legitimate bar use).
- SELECT enters edit on the highlighted row; the frame goes **yellow** to say
  "you are changing something", and the row shows `‹ 1450 ›`.
- Wheel adjusts, SELECT commits, LEFT cancels and restores.
- The hint box carries the units and the range: "µs · 900–2100".

```
   ╭─ Top servo extend    ‹ 1450 › ─╮      <- editing, frame yellow
   ╰────────────────────────────────╯
   ╭─ Top servo retract      1900  ─╮
   ╰────────────────────────────────╯
```

Servo Positions additionally has to *move the servo* as the value changes, which
is the whole point of setting it by eye. Rate-limit that, and never leave edit
mode without either committing or restoring the previous position.

### Stats — top level

Already drawn; it just needs real data behind it. The EEPROM block is the work,
not the screen. When it exists: solves, best, average, last, plus total run
time. Six rows is exactly enough, so resist adding a seventh.

---

## Suggested order

~~Cube net, Cube State, scan review~~ — done.

~~Patterns~~ — screen done; only running the moves remains.

1. **Scramble Solve** — screen **prototyped** in `Test_Menu` (Screens >
   Operations > Scramble Solve): red frame while scrambling, green the moment
   it starts solving, one progress bar throughout. `setOpKind()` recolours the
   frame without tearing the screen down, which is what makes the phase change
   free. What remains is the machine side — generating a scramble and running
   it — not the screen.
~~Hardware Test~~ — built in `Test_Menu` as the Actuators tree. `setOpLines()`
also gained an optional `RowMark` per row along the way, which tints the value
half green/red/amber; nothing uses it yet, but **Sensor Test** wants exactly
that.

2. **Move ribbon** then **Step Solve**; Demo Mode falls out nearly free.
4. **Scrolling list** then **Fault Log**.
5. **Value editor** then **Parameters** and **Servo Positions** — last because
   it is a new interaction, and worth having the rest settled before adding a
   second thing the wheel can mean.

Low-confidence marking on the Cube State net can slot in whenever; it needs no
new primitive, only `scanConf` plumbed through.

Idle Mode can slot in any time after Stats has real numbers.

## Where to build them

**In `Code/Test Code/Test_Menu`, first.** Not in the firmware.

That sketch needs no cube, no machine and no solver, its `Screens` submenu draws
every operation screen from canned data, and it runs in the simulator:

```sh
cmake -S Code/sim -B Code/sim/build-test -DSIM_SKETCH=Test_Menu
cmake --build Code/sim/build-test -j && ./Code/sim/build-test/cubesim
```

A screen prototyped there can be judged against every frame colour and item
count in seconds, and it keeps half-finished UI out of the sketch that drives
real motors. Move it into the firmware once it looks right and there is
something behind it to show.

The corollary matters too: when a screen changes in the firmware, the demo in
`Test_Menu` should follow. The bench sketch being *behind* the firmware is how
it stops being useful. Anything shared — the required calibration orientation,
the scan pass labels — should be referenced from `CubeSystem`'s constants rather
than copied, so the demo cannot drift from the machine.
