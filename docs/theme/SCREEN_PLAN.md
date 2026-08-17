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

Built so far, and where each lives:

| Piece | Call | Built for |
|---|---|---|
| Cube net | `setOpCubeNet()` | Cube State, scan review, Patterns |
| Chip row | `setOpChipRow()` / `setOpFaces()` | scan faces, calibration, the jog strip |
| Move ribbon | `setOpRibbon()` | Step Solve |
| Row status marks | `setOpLines(..., marks)` | the jog cursor; Sensor Test wants it too |
| Frame recolour | `setOpKind()` | phase changes, and "you are editing" |

Still missing:

1. **Scrolling status list** — Fault Log. More than six rows with a position
   readout in the hint bar rather than a scrollbar the theme has no art for.
2. **Value editor** — Parameters and Servo Positions. Mostly solved already:
   the Actuators page's enter-pick-send on a gripper IS this interaction, and it
   proved out. What is left is a NUMBER rather than three named positions, and
   deciding the step size.

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

### Demo Mode — Modes — **BUILT**

Scramble, solve, repeat, unattended. Both halves already existed — the phase
colours from Scramble Solve, the ribbon from Step Solve — so it is a loop around
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

Screens done, machine side outstanding:

- **Patterns** — the four states and the preview pane are built; running the
  move sequence is not.
- **Scramble Solve** — red while scrambling, green the moment it solves, one bar
  throughout. Needs a scramble generated and run.
- **Step Solve** — the ribbon works; needs the moves actually executed.
- **Demo Mode** — scramble and solve on a loop with a run counter, showing the
  moves rather than only a count. Needs the two operations behind it.
- **Hardware Test** — the Actuators page drives real hardware already. What
  remains is porting the page into the firmware's Diagnostics menu.

Still to build:

1. **Sensor Test** — no new primitive. Status rows refreshed live, with a chip
   row doubling as a per-sensor colour readout. **Throttle it**: every refresh
   is I²C traffic on the bus the encoder is also using, and `Test_Menu`'s Input
   Report already shows the pattern.
2. **Fault Log** — needs the scrolling status list.
3. **Parameters** and **Servo Positions** — need the value editor, which the
   Actuators page has already proved the interaction for.
4. **Stats**, then **Idle Mode** — the screens are drawn; the work is an EEPROM
   block to put real numbers behind them.

Low-confidence marking on the Cube State net can slot in whenever; it needs no
new primitive, only `scanConf` plumbed through.

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
