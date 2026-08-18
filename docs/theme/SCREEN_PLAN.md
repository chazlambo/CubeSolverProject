# Screen plan — what the rest of the machine needs to look like

Every menu item that once landed on "Not implemented yet", designed before it
got written. As of firmware 1.0.0 every screen here is **BUILT**, machine side
included — the per-screen statuses say so. The point was never to lock anything
down; it is so that the tenth screen looks like it belongs with the first, and
so the shared pieces get built once instead of five slightly different times.

Read `UI_DESIGN.md` first — it covers the palette, the limits and the traps.
This file was about screens that did not exist yet; it stays as the record of
why each one looks the way it does.

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

Second rule, from the same place: **the frame color is the state.** Blue
scanning, green solving/done, violet calibrating, yellow info/settings, red
stopped. A new screen picks the one that already means what it means.

---

## Vocabulary we have

| Piece | Call | Says |
|---|---|---|
| Frame color | `showOperation(kind, …)` | what kind of thing is happening |
| Title | `showOperation(…, title, …)` | where you are |
| Headline / sub-line | `showOperation(…, headline)` / `setStatus()` | what it is doing now |
| Status table | `setOpLines()`, `"Label\tValue"` | facts, up to 6 rows |
| Face row | `setOpFaces()` | the six faces, filled with the color actually read |
| Chip rows | `setOpChips()` | a set being collected, two rows |
| Progress bar | `setOpProgress()` | how far through a countable job |
| Dial | `setOpDial()` | a setting you are steering, and where it sits in its range |
| Hint box | `showOperation(…, hint)` | what button to press |
| Preview pane | menu only | what is behind this item |

## Vocabulary we still need, ranked

Built so far, and where each lives:

| Piece | Call | Built for |
|---|---|---|
| Cube net | `setOpCubeNet()` | Cube State, scan review, Patterns |
| Chip row | `setOpChipRow()` / `setOpFaces()` | scan faces, calibration, the jog strip |
| Move ribbon | `setOpRibbon()` | Step Solve |
| Row status marks | `setOpLines(..., marks)` | the jog cursor; Sensor Test uses it too |
| Frame recolor | `setOpKind()` | phase changes, and "you are editing" |

Nothing is still missing — the last two landed with the machine side:

1. **Scrolling status list** — BUILT, for Fault Log. More than six rows with a
   position readout in the hint bar rather than a scrollbar the theme has no
   art for.
2. **Value editor** — BUILT, for Parameters and Servo Positions. The Actuators
   page's enter-pick-send on a gripper proved the interaction; the editor adds
   a NUMBER rather than three named positions, with the step size a column of
   the shared parameter table.

### On the cube net — built, and how

`setOpCubeNet(facelets)` takes 54 color letters in the standard order and draws
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
than on a grey slab. A facelet that is not a known color is drawn as a hollow
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

### Scramble Solve — Modes — **BUILT**

Two phases, and the frame color should carry that: **red** while scrambling,
**green** the moment it starts solving. Nothing else on the screen has to
change for the operator to know which half they are watching.

```
              Scrambling                    <- headline, per phase
             Move 7/25   R'                 <- sub-line
        ▓▓▓▓▓▓▓▓▓░░░░░░░░░░░░░              <- progress bar
```

Reuses everything. Needed nothing new. Built first — it was the cheapest of
the Modes and it proved the phase-color idea.

### Idle Mode — Modes — **BUILT**

The machine turning to look alive between visitors: a random quarter turn every
so often, waiting for someone to walk past.

```
              R'                     <- the last move, large
        Moves      12
        Every      10 s
              wheel sets the gap - SELECT solves
```

Three controls share one screen and none needs a mode of its own, because
there is no cursor here to move and nothing to enter:

| | |
|---|---|
| wheel | how long between moves, 1-60 s |
| SELECT | stop idling and solve it |
| LEFT | back, as everywhere |

The gap **clamps** rather than wrapping. A cursor may wrap because every item is
equivalent; rolling a one-second gap round to a minute because the wheel went
one detent too far is a different kind of surprise. Changing it also re-times
the pending move from now — shortening the gap and then waiting out the old one
reads as the setting not working.

The frame color advances with each MOVE rather than on a timer of its own. This
is the one place in this UI where frame color is decorative rather than
semantic, and tying it to the moves at least makes it honest: a color change
means something happened, so the machine reads as alive from further away than
the move counter can be read.

Idling **disorders the cube** — Step Solve must not then scramble a cube that
is already scrambled. The firmware derives that from the cube model itself
(`cubeIsSolved()`) rather than a flag, so the two cannot disagree.

### Demo Mode — Modes — **BUILT**

Scramble, solve, repeat, unattended. Both halves already existed — the phase
colors from Scramble Solve, the ribbon from Step Solve — so it is a loop around
them plus a run counter.

Four phases, because the machine has four:

```
   Scrambling            red      30 moves, ribbon + bar
   Computing the         green    the handover: no ribbon, no bar
     solution
   Solving               green    21 moves, ribbon + bar
   Solved!               green    the result, briefly
```

The pause to compute is not padding. The real machine has to work out a solution
before it can run one, and a demo that jumped from the last scramble move
straight to the first solve move would be showing something the machine never
does. The frame turns green THERE rather than at the first solve move, because
that is the moment it stops scrambling — and the ribbon and bar go, because
neither has anything true to say about a search that has not finished.

It shows the MOVES because the whole point of leaving it running is that it
should be worth watching. The scramble is its own list — thirty moves, no two in
a row on the same face, which is what a real scramble looks like and the reason
it reads differently from a solution.

### Step Solve — Modes — **BUILT**

Scrambles thirty moves first, then steps — but only if the cube is not already
scrambled. Idle Mode and a previous run both leave it disordered, and thirty
moves spent re-scrambling a scrambled cube would be a lie about what the machine
does.

Three phases: red while scrambling with the scramble ribbon running, yellow
while computing, green for the step-through. The compute phase must CLEAR the
ribbon and the progress bar — left up, a finished ribbon and a full bar sit
under the word "Solving" and read as a solve that finished before it started.
Both hide on a null/zero argument.



One move per press. This is what the **move ribbon** is for.

```
             Move 7 of 21

     R   U2   F'   ⟨ L ⟩   D   B2   R'      <- current in cursor yellow,
                                               past dimmed, future grey
```

Deliberately **plain text tokens, no boxes** — boxed tokens would drift back
toward looking like buttons. Color alone carries the cursor.

`setOpRibbon(moves, count, current)` centres the window on the current move and
shows about seven. Hint: "SELECT for next move".

### Patterns — Modes — **BUILT**

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
cube and checked for nine of every color — a preview that is not a real cube
would hide exactly the bugs this screen is for.

The machine side — running the moves — is built now too. The fold refuses a
cube that is not solved, because the preview is a promise about the result.

### Cube State — Diagnostics — **BUILT**

The net plus a color count. Nine of each is the cheapest check that the stored
state is a cube at all, and unlike "invalid" it says *which* color was misread.

It doubles as the **scan review**: with nothing built but a scan recorded, it
shows the raw readings instead of an empty screen — which is the case where
somebody most wants to look. See the caveat above.

Still worth adding: mark the low-confidence stickers. `scanAlt` and `scanConf`
already record the runner-up color and the confidence for every sticker, so the
data is there — a hollow or outlined sticker for "the classifier was unsure
about this one" would turn this from *what it read* into *what to distrust*.

### Fault Log — Diagnostics — **BUILT**

A list nobody selects from, so status rows rather than bars.

```
        12:04   Solve stopped      122
        11:58   Scan failed         60
        11:51   Aborted             25
        ...
```

Six rows at a time, wheel scrolls, and the position goes in the hint box —
"7-12 of 24" — rather than inventing a scrollbar the theme has no art for.

### Hardware Test — Diagnostics — **BUILT**

**Not a self-test.** It is manual control: drive each actuator to each position,
by hand, one at a time. The panel version of `Test Code/Actuator_Test`, which is
a numbered list over Serial. It is where you go to answer "does that servo
actually reach retract", and where you drive one part at a time to show the
machine off.

It started as a menu of every action, and that was wrong: nine entries across
three screens for the grippers alone, twelve more for the faces, all clicked
through one at a time — when what you want is to pick a thing and nudge it while
you watch it move.

So the two busy pages are **direct manipulation**, not menus. They can be,
because the wheel and the UP/DOWN BUTTONS are separate inputs on this encoder.
A menu collapses them into one meaning; a jog page gives them two:

```
    wheel        choose which part
    UP / DOWN    move that part
    LEFT         back, exactly as everywhere else
```

**One page, no submenu at all.** Twelve things on one wheel — three grippers,
the cube button, six face motors, two whole-cube rotations — and it wraps.

```
   Top servo                  < Extend >     <- entered: the candidate
   Bottom servo                       ?
   Ring                          Middle
   Eject cube                                <- named for what it will DO
   [U] [R] [F] [D] [L] [B] [RotX] [RotZ]
```

Load and eject are ONE row, because the cube is either in the machine or it is
not. It is named for what pressing it will do, so there is nothing to read twice.

Keeping them together is not only tidiness. **A face motor cannot turn until the
grippers are clear**, so seeing where the grippers are WHILE jogging a face is
the difference between a considered press and a jam.

**Two levels everywhere: scroll to a thing, SELECT to enter it, LEFT to leave.**
The frame goes yellow while you are inside something, so "I am about to move
this" reads without a word.

What the wheel then does depends on what you entered, and that difference is the
whole design:

- A **gripper is a position**. The wheel picks one, SELECT sends it, and nothing
  moves until you say so.
- A **motor is a thing you turn**. Once you have taken the wheel, every detent
  IS a turn — which is what a jog wheel should feel like, and it means watching
  a motor through several turns costs no button presses at all.

The cursor is a **marked row**, not a bar:
they are a readout you are steering, and bar art would promise a selection that
is not what is happening. Position reads "?" until it is known — the servos
remember across a reset but nothing exposes it, and guessing is worse than
admitting it. Load and eject both end at a known state, so those rows can say so
rather than falling back to "?".

Everything that turns is one **chip strip**, faces and whole-cube rotations
together, because they are the same gesture. Eight of anything does not fit five
bars; as chips it fits with room. The strip sizes its chips to the count so it
always clears the frame band, and six still comes out at exactly the width the
scan screen has always used.

The hint bar carries what the buttons do and changes with what is selected —
there is no one sentence true of both a servo and a face motor.

Built in `Test_Menu` under Actuators first, and now ported into the firmware's
Diagnostics menu as **Hardware Test**; the bench page stays as the rehearsal —
change both.

### Color Sensors — Diagnostics — **BUILT**

Two health rows and eighteen live chips, nine per board. The chips ARE the
readout: eighteen color names would take longer to read than the cube takes to
scan, and the thing you are hunting — one sensor disagreeing with its
neighbours — shows up instantly as a chip of the wrong color.

```
   Board 1              9/9 healthy, sep 165     <- green
   Board 2              8/9 healthy, sep 3       <- red
   [][][][][][][][][]                            <- board 1, live
    1  2  3  4  5  6  7  8  9
   [][][][][][][][][]                            <- board 2
```

The wheel moves a cursor across all eighteen and **SELECT drills into one**:

```
   Board 1  Sensor 4
   Red                              902
   Green                            252
   Blue                             222
   White                           1376
   Reads as                       Orange     <- green, or red for "unusable"
```

That second screen is for "why did it call that sticker orange". The
classification is a judgement made from four numbers, and until you can see them
the answer is a guess.

The demo draws board 2 sensor 2 as unusable, because on this machine it is (a
dead green channel; see the README). A diagnostic that only ever shows healthy
hardware is not a diagnostic.

On hardware the values come from `colorSensorN.currentRGBW` right after a
`scanSingle()` — `getScanValRow()` holds the last full-face scan, which this
screen would show as stale data. Wants a 3x3 grid per board eventually, to
match the physical face.

### Motor Sensors — Diagnostics — **BUILT**

Seven encoders, seven numbers — six faces and the ring, raw 12-bit angles.

Nothing here is a picture, because an angle is not one. What you are checking is
whether a value moves when you turn a face, and whether any encoder is reporting
an I2C error instead of an angle. `MotorEncoder::scan()` returns the angle or a
negative error code, and showing the error rather than a plausible number is the
point of the screen.

### Tuning — Calibration > Servo Positions and Diagnostics > Parameters — **BUILT**

The one that needed a genuinely new interaction: the wheel changes a *value*,
not a cursor. Built as a settings list kept separate from `CubeMenu` rather than
bolted onto it — `CubeMenu` is navigation-only and host-testable, and value
editing is a different job.

Six sections, each a window onto one flat table:

| Section | Rows |
|---|---|
| Servos > Top Servo | extend, retract, sweep delay |
| Servos > Bottom Servo | extend, retract, partial, eject, sweep delay |
| Servos > Ring | retract, partial, middle, extend, speed, accel |
| Face Motors | step speed, step delay, rotate delay, servo delay |
| Alignment | tolerance, align timeout, home timeout, debug log |
| Color | scans averaged, integration, color tol, margin frac, distance frac, min separation |

Plus **Reset Defaults**, which clears the EEPROM stamp and re-applies every
compiled default.

#### Interaction

- Scroll to a row, SELECT to enter, wheel to change, SELECT to keep, LEFT to
  restore. Frame goes **yellow** while a row is entered.
- A **toggle** flips in place. Entering an edit mode to choose between two
  values would be three presses to change one bit.
- A **gated** row shows a red confirm FIRST. That ordering is the whole point:
  gated rows preview as the wheel turns, so by the time you could confirm a
  value the horn has already been there. What is confirmed is "I am about to
  move this part", not "I accept this number".
- The **description** of the selected row lives in the sub-line, above the rows.
  It has to be set BEFORE `setOpLines()`, which reads it to decide where rows
  start — set afterwards, it lands on top of row one.

#### Traps this screen is built around

**Accessors, never the config globals.** `CubeServo` copies `topExtPos` at
construction and never reads it again, and the `CubeMotors` tunables were
private. An editor written the obvious way shows numbers changing and moves
nothing. Same trap, twice more: `ColorSensor::begin()` is the only caller of
`setConfiguration()`, so integration time needed
`ColorSensor::applyIntegrationTime()` to mean anything at runtime; and
`waitTime` is documented as "integration time * 2.5" but has always been
1.875x, so it is derived from the ratio the machine actually runs.

**One detent is one step, however fast the wheel spins.** A live row writes the
servo on every change and `previewRaw()` jumps rather than sweeps, so honouring
a burst of detents at once turns a nudge into a slam.

**Persist on the way out, not per detent.** `previewRaw()` deliberately skips
EEPROM. But skipping it entirely is worse than either: `CubeServo::begin()`
trusts the stored position to decide how far its first sweep travels, so a stale
one arms a full-travel slam on the next power-up.

**Angle brackets are ASCII.** The baked fonts carry 0x20-0x7F and a missing
glyph draws as an empty box in silence.

#### What is deliberately NOT here

- **Turn step** — steps per quarter turn is gearing, not preference. A wrong
  value does not make solves worse, it makes them impossible. `CubeMotors` has
  a getter and no setter, on purpose.
- **Wait time** — derived from integration time. Exposing both invites an
  inconsistent pair that misreads every sticker.
- **Saturation threshold, step size, stable required** — properties of the
  VEML6040 and of homing internals, not of this machine.
- **Abort hold time** — the SELECT+LEFT duration. It is a safety gesture, not a
  preference: too short and a solve aborts by accident, too long and the abort
  reads as broken. Nothing about it varies per machine.

#### Persistence

Defaults live in the table, overrides in EEPROM, via `CubeTuning` — a version
stamp plus a flat array of int32. Values are stored BY POSITION, so **reordering
or inserting a parameter must bump `CubeTuning::kVersion`**; without it the next
boot hands an alignment tolerance to a servo. The block is appended at the END
of `initializeEEPROMLayout()` and has to stay there: addresses are handed out
sequentially, so a block inserted higher shifts every one below it and silently
reinterprets an already-calibrated machine's motor and color data.

One behaviour change worth knowing: the bottom servo's partial and eject
positions are now **pinned** rather than derived from the extend position, since
boot applies every value whether it came from EEPROM or from the defaults.

### Stats — top level — **BUILT**

Six rows — solves, best, average, last, run time, faults — which is exactly
enough, so resist a seventh. The numbers are real now: an EEPROM block of
counters (`CubeStats`), a block of ITS OWN rather than a corner of the tuning
one: solve counts change every run and tuning changes almost never, so sharing
would rewrite the tuning bytes on every solve for nothing.

---

## Suggested order

All of it landed, roughly in this order:

- **Patterns** — the four states and the preview pane came first; the move
  sequences now run, gated on a solved cube.
- **Scramble Solve** — red while scrambling, green the moment it solves, one bar
  throughout. Done.
- **Step Solve** — the ribbon works and the moves execute, one per SELECT. Done.
- **Demo Mode** — scramble and solve on a loop with a run counter, showing the
  moves rather than only a count. Done.
- **Color Sensors** and **Motor Sensors** — real sensor reads, **throttled** and
  round-robin: every refresh is I²C traffic on the bus the wheel is also using.
  Done.
- **Hardware Test** — the Actuators page, ported into the firmware's
  Diagnostics menu. Done.
- **Tuning** — six sections editing the real values, with defaults in the
  source and overrides in EEPROM. Done, including persistence and reset.

**Every screen is now drawn AND wired.** `Test_Menu` covers the whole tree, and
its `Screens > Modes` submenu is deliberately the same five items in the same
order as the firmware's `Modes` menu — a rehearsal that groups things
differently from the machine stops being a rehearsal.

The machine side landed too, in the two kinds this section predicted:

1. **Two EEPROM stores**, both the shape `CubeTuning` already is — the
   `CubeFaultLog` ring buffer (replacing the one-int `CubeSystem::lastFault`)
   and the `CubeStats` counters.
2. **Behaviour behind the five Modes**, plus the finished Diagnostics screens
   ported into the firmware sketch. No menu item says "Not implemented yet"
   any more.

Low-confidence marking on the Cube State net can still slot in whenever; it
needs no new primitive, only `scanConf` plumbed through.

## Where to build them

**In `Code/Test Code/Test_Menu`, first.** Not in the firmware.

That sketch needs no cube, no machine and no solver, its `Screens` submenu draws
every operation screen from canned data, and it runs in the simulator:

```sh
cmake -S Code/sim -B Code/sim/build-test -DSIM_SKETCH=Test_Menu
cmake --build Code/sim/build-test -j && ./Code/sim/build-test/cubesim
```

A screen prototyped there can be judged against every frame color and item
count in seconds, and it keeps half-finished UI out of the sketch that drives
real motors. Move it into the firmware once it looks right and there is
something behind it to show.

The corollary matters too: when a screen changes in the firmware, the demo in
`Test_Menu` should follow. The bench sketch being *behind* the firmware is how
it stops being useful. Anything shared — the required calibration orientation,
the scan pass labels — should be referenced from `CubeSystem`'s constants rather
than copied, so the demo cannot drift from the machine.
