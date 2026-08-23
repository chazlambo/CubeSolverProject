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

Second rule, and it has been rewritten since most of this file was: **the
frame color is where you are.** A branch of the menu tree is one color from the
row that opens it all the way out through the operation screens it leads to —
blue for the main line of work, red for Modes, green for Eject, yellow for the
whole Settings subtree, purple for Stats. A new screen does not pick a color;
it inherits its branch's.

It used to mean machine state — blue scanning, green solving, violet
calibrating — and several sections below still describe screens in those terms,
noted where they do. The two schemes fought over one band, because a menu
already recolors by selection, and wayfinding won. Only `Error` kept its old
meaning: **red, always, whatever branch it happened in.** `UI_DESIGN.md` §2 has
the full rule and the one other exception (Idle Mode's decorative cycle).

### The Settings subtree

Restructured with the color change, since one branch of one color also wants to
be one branch of one shape:

```
   Settings
     Diagnostics     read the machine    Hardware Test, Sensor Test,
                                         Cube State, Fault Log
     Calibration     teach the machine   Status, Color Sensors,
                                         Motor Positions, Servo Positions
     Parameters      change the machine  the six tuning sections
     About
```

Diagnostics above Calibration because looking comes before altering, and
Parameters promoted out of Diagnostics to sit beside Calibration because it
WRITES — filing it under a menu named for reading was always a lie about it.
Every screen under Settings is yellow. That does cost the local distinction
Settings used to draw between Diagnostics (purple) and Calibration (violet);
the title line still says which, and one branch of one color is the trade.

---

## Vocabulary we have

| Piece | Call | Says |
|---|---|---|
| Frame color | `opScreen(kind, …)` then `setOpTheme()` | which branch you are in |
| Title | `showOperation(…, title, …)` | where you are |
| Headline / sub-line | `showOperation(…, headline)` / `setStatus()` | what it is doing now |
| Status table | `setOpLines()`, `"Label\tValue"` | facts, up to 7 rows (`kOpLines`) |
| Face row | `setOpFaces()` | the six faces, filled with the color actually read |
| Chip rows | `setOpChips()` | a set being collected, two rows |
| Progress bar | `setOpProgress()` | how far through a countable job |
| Dial | `setOpDial()` | a setting you are steering, and where it sits in its range |
| Hint box | `showOperation(…, hint)` | what button to press |
| Preview pane | menu only | what is behind this item |

## Vocabulary added while building

Where each of the later pieces lives:

| Piece | Call | Built for |
|---|---|---|
| Cube net | `setOpCubeNet()` | Cube State, scan review, Patterns, the two Solve thinking screens |
| Chip row | `setOpChipRow()` / `setOpFaces()` | scan faces, calibration, the jog strip |
| Move ribbon | `setOpRibbon()` | Step Solve |
| Row status marks | `setOpLines(..., marks)` | the jog cursor; Sensor Test uses it too |
| Frame recolor | `setOpTheme()` | the branch color, and Idle Mode's cycle |

Two more pieces came with the machine side:

1. **Scrolling status list**, for Fault Log. More than a page of rows
   (`kOpLines`, seven) with a position readout in the hint bar rather than a
   scrollbar the theme has no art for.
2. **Value editor**, for Parameters and Servo Positions. The Actuators
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

### Solve, the thinking half — top level — **BUILT**

The two screens between pressing Solve and the machine moving. Both are the
same picture: the net of the cube *as it stands*, drawn by `setOpCubeNet()`
from `VirtualCube::getColorArray()`. One function draws both — `drawSolveNet()`
in the sketch — because they ask the operator the same question, "is this the
cube you loaded", and only the line under the net changes.

```
        [ the unfolded net, y=60..150 ]
           Finding solution...                <- sub-line, under the net
                                              <- no hint: nothing is polled
```
```
        [ the same net ]
         Solution found in 21 moves           <- Cube.solutionLength, not a recount
      SELECT to solve, LEFT to cancel         <- hint box
```

**No headline on either**, and that is structural rather than taste: the net
occupies y=60..150 and `showOperation()` puts the headline at 58, so a screen
carrying both draws one through the other. `setOpCubeNet()` moves the sub-line
below the net for exactly this reason.

The first screen has **no hint** because `solveVirtual()` blocks *without*
pumping — nothing is polled while it is up, so an abort line would advertise a
gesture the machine cannot hear.

The first screen is painted by the Solve action but *flushed* by the `Solving`
state, which pumps one refresh period (`pumpDelay(40)`) before the search
blocks. A single `displayUpdate()` only marks widgets dirty; LVGL repaints when
its 33 ms timer comes due, and without the pump the panel stayed on the menu
for the whole compute — Solve read as a press that had not registered.

The second is `AppState::SolveConfirm`, and its whole value is that nothing has
started yet: no gripper driven, no solve timer, no stats touched. LEFT is
therefore free — it returns to the menu leaving the cube exactly as it stood
(clamped, in the usual case, which is where the scan and the previous solve
leave it) and leaving no half-started solve behind. SELECT falls through to
`Loading`.

**Only say the machine is clamping when it is.** `Loading` clamps *only* if
`cubeIsClamped()` says it is not already, and the "Clamping the cube" screen now
lives inside that branch. It used to be painted unconditionally on the way in
from `Solving`, which claimed a clamp on the common path where nothing moved.

The whole run — both of these, the clamp if there is one, the moves, the display
spin — wears the Solve row's **blue**, by the ordinary `s_opTheme` mechanism
(§ "The rule everything here follows"). Nothing on the path names a colour; the
`Op::Done` on the display spin is a label only, since `opScreen()` repaints the
branch colour over the kind's fallback. A failure leaves through `fail()` and is
red, as everywhere.

### Solve, the display spin — top level — **BUILT**

How a plain Solve ends. Not a straight release any more: the machine lets go of
the ring and the top servo, keeps the cube up on the bottom gripper, and turns
it slowly on the spot — one revolution about every eight seconds — while the
result stands on the screen. With nothing else engaged the D motor turns the
WHOLE cube rather than a face, which is why the model is never told about it.

```
                Solved!                     <- headline
           21 moves in 4.62 s               <- sub-line, the shared formatter
      SELECT clamps the cube and finishes   <- hint box
```

Reuses everything; needed nothing new. The interaction is the one novel part:
SELECT (or LEFT) finishes the revolution so the cube is square, clamps it again
bottom -> ring -> top, and returns to the menu — which leaves the machine
holding the cube, so the next Solve skips its own clamp.

**Modes do not end here.** They still finish through `Unloading`: Demo loops and
would stall on a screen that waits for a press, and Step Solve is already
human-paced. Making it universal is a one-line change, recorded in the sketch
above `drawSolveDisplay()`.

Rehearsed in `Test_Menu` as `Screens > Operations > Solve Result` — the
"Solved!" screen the Scramble Solve demo already draws, minus the ribbon and
bar, plus a hint. The spin itself is the part no bench sketch can show, so the
demo is the still frame only.

### Eject — top level — **BUILT**

Was a screen that asked for a press: "take the cube out, then press SELECT". It
watches now. The machine releases the ring and the top servo, lifts the cube on
the bottom gripper — `unloadCubeKeepBottom()` then `botServoEject()`, straight
there, no dip into the bay first — and then leaves both color boards illuminated
and sweeps all eighteen sensors five times a second.

```
             Take the cube out              <- headline
     SELECT if the machine does not notice  <- hint box
```

The hint is the whole design of the screen. The cube can just be taken, so the
headline says only that; the button is named as a backstop, not as a step,
because promising a press the operator does not need is how the old screen was
wrong.

Detection is **tare, pick witnesses, watch for the fall**. Once the LEDs have
warmed, three `presenceSweep()`s of every sensor on both boards are averaged
into a per-sensor baseline — taken while the cube is definitely still there.
The sensors reading at least 50% of the brightest baseline are the *witnesses*:
at the eject height the cube's lowest row sits a couple of millimetres from the
top row of each board and bounces the LED straight back, while everything else
looks at the bay, so brightness alone sorts out which sensors can see the cube.
The cube is gone when more than half the witnesses have fallen to 40% of their
own baselines for four consecutive sweeps. No absolute brightness anywhere —
ambient light, LED output and sticker color all vary, and each baseline is
measured against the very sticker that is about to leave. A negative read is
an I2C fault and is never removal. A *fall* rather than "any change", because
a hand reaching in is also a change — it reflects the LED into the bay-facing
sensors a second before the cube leaves — but it cannot get between a sticker
and the sensor it sits 2 mm from. The debounce is there because a cube loosely
held can clear its witnesses while still resting on the platform.

Every ending — detected, SELECT, the 120 s give-up timeout, the abort chord —
goes through one `ejectFinish()`: LEDs off, bottom servo down, back to the
menu, where the root list reverting to its pre-scan form is the confirmation.
No "Cube ejected" acknowledgement screen, because that was the press being
removed.

**No sensor is chosen by hand.** The first version watched one sensor — first
the centre, then the bottom-middle — and stood through its timeout, because
which of the nine is physically uppermost is not knowable from this tree: the
`{UL, UM, UR, ...}` names in `CubeHardwareConfig.cpp` predate the 90° scanner
rotation the README describes. Sweeping all eighteen and letting the tare say
which ones see the cube removes the guess. The tare and the verdict go to
Serial (`Eject tare ...`, `Eject: cube gone ...`), which is where the 50%/40%
thresholds get confirmed on the bench; the timeout exists so a machine whose
sensors cannot see the cube at this height costs two minutes rather than forever.

Rehearsed in `Test_Menu` as `Screens > Operations > Eject Prompt` — the
prompt only. `Test_Menu`'s own root `Eject Cube` is an actuator exercise, not a
screen demo, and the watch itself needs the real color boards, so neither
rehearses the detection.

### Scramble Solve — Modes — **BUILT**

Two phases — scrambling, then solving — and the headline, sub-line and bar all
say which. **They no longer recolor the frame.** The frame is red throughout,
because red is Scramble Solve's place in the tree, and the frame answers "where
am I" rather than "which half is this". The as-built plan below was written
under the old rule and the color half of it is superseded.

```
              Scrambling                    <- headline, per phase
             Move 7/25   R'                 <- sub-line
        ▓▓▓▓▓▓▓▓▓░░░░░░░░░░░░░              <- progress bar
```

Reuses everything. Needed nothing new. Built first — it was the cheapest of
the Modes, and the phase-color idea it proved is the one that later lost to
wayfinding.

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
is the one sanctioned exception to frame-color-as-wayfinding: everywhere else
the band says which branch you are standing in, and here it cycles all six on
purpose. Tying it to the moves is what keeps it honest — a color change means
something happened, so the machine reads as alive from further away than the
move counter can be read. Idle Mode's other screens (the clamp, and the solve
SELECT hands off to) wear the mode's own yellow.

Idling **disorders the cube** — Step Solve must not then scramble a cube that
is already scrambled. The firmware derives that from the cube model itself
(`cubeIsSolved()`) rather than a flag, so the two cannot disagree.

### Demo Mode — Modes — **BUILT**

Scramble, solve, repeat, unattended. Both halves already existed — the phase
handling from Scramble Solve, the ribbon from Step Solve — so it is a loop
around them plus a run counter.

Four phases, because the machine has four. The colors this table used to name
are gone: Demo Mode is **blue** end to end, its row's color in the Modes menu.
A run scrambles and solves and scrambles again every few seconds, and
recoloring the halves would have said the machine kept changing job.

```
   Scrambling                     30 moves, ribbon + bar
   Computing the                  the handover: no ribbon, no bar
     solution
   Solving                        21 moves, ribbon + bar
   Solved!                        the result, briefly
```

The pause to compute is not padding. The real machine has to work out a solution
before it can run one, and a demo that jumped from the last scramble move
straight to the first solve move would be showing something the machine never
does. The handover happens THERE rather than at the first solve move, because
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

Three phases: scrambling with the scramble ribbon running, computing, then the
step-through. **Purple** throughout — Step Solve's color in the Modes menu — and
not the red/yellow/green progression this originally specified; see the second
rule above. The compute phase must still CLEAR the ribbon and the progress bar:
left up, a finished ribbon and a full bar sit under the word "Solving" and read
as a solve that finished before it started. Both hide on a null/zero argument.



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

**And it turns.** With a model built, the wheel points at a face and UP/DOWN
turn it, with the net redrawing after each move. The turn is applied to the
**model only** — no motor, no servo — which is the opposite half of the trade
`jogTurn()` makes on the Hardware Test page: that page moves the machine and
therefore cannot keep the model, and this one keeps the model and therefore
must not move the machine. Same desync, opposite ends.

The screen says so in capitals under the net, next to the `< U >` selector,
because the whole risk of the page is an operator believing the machine just
moved.

A model left turned would desync just as badly the other way — Solve would run
against a state the machine is not holding — so the page does not leave it
turned. Every move goes on a 60-entry trail (a turn that undoes the previous
one pops instead of pushing, so forward-and-back costs no depth) and **every
exit replays it backwards inverted**. A scan survives a visit here.

The raw-scan form stays read-only: `VirtualCube::executeMove()` refuses a cube
that is not ready, and the per-face rotation is unresolved anyway, so a turn
would shuffle stickers inside a frame that does not mean anything yet.

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

Seven rows at a time — the whole of `kOpLines` — wheel scrolls, and the
position goes in the hint box — "8-14 of 24" — rather than inventing a
scrollbar the theme has no art for.

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
Entering something used to turn the frame yellow. The frame is Settings' yellow
throughout now — it says where you are, not what is armed — so the hint line and
the row mark carry that instead. Both already changed with the state.

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

### Tuning — Calibration > Servo Positions and Settings > Parameters — **BUILT**

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
compiled default — all six sections at once. One section at a time is done
from inside the section: UP or DOWN on its Apply row stages that page's
defaults as pending values, and Apply writes them.

#### Interaction

- Scroll to a row, SELECT to enter, wheel to change, SELECT to keep, LEFT to
  restore. The frame no longer changes on entry — it is Settings' yellow the
  whole time — so the hint line becomes the value's range instead.
- **Nothing is applied until the Apply row is pressed.** A parameter was one
  press and one detent away from being changed for good, and a machine that
  quietly kept the accident offered no route back to the number that worked.
  What is gated is the *commit*, never the preview: a servo endpoint is set by
  eye, so `previewRaw()` still runs on every detent exactly as before, and it
  is `set()` that waits. While a section is open the owners and EEPROM hold
  `parBase[]` and the wheel moves `parVal[]`; `set()` and `tuneSaveAll()` are
  reached from the Apply row and nowhere else.
- Changed rows carry a **leading star**, and the Apply row's value is the count
  ("3 changed"). The star is in the label column rather than being a `RowMark`
  because the cursor already owns the mark.
- A value that is **not the compiled default** is drawn in amber
  (`RowMark::Tuned`), so a page read cold says which numbers have been tuned
  without anything having to be pressed. Amber, not yellow: the cursor is
  already yellow. It follows the value shown, so a row wheeled back to its
  default stops being amber as it gets there; the star still says it is
  pending. On the cursor row the cursor wins, as it does over every mark.
- **UP or DOWN on the Apply row loads this page's defaults** — as pending
  values, through the same Apply gate, with no preview of the live rows (the
  parts move when Apply stores the values and the machine next uses them,
  exactly as after Reset Defaults). There is no row for it because Ring and
  Color plus Apply already fill all seven lines. The hint on the Apply row
  offers it whenever it would do something.
- LEFT with changes pending **asks, in red** — the same shape as Reset
  Defaults' confirm, and SELECT is the destructive answer there as it is
  everywhere else in this sketch. Discarding drives every previewed part back
  to its base value first: a live row has already moved the horn, so forgetting
  the number is only half of putting it back.
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
one arms a full-travel slam on the next power-up. That write records where the
horn is *standing*, not what its endpoints are, which is why it is right on the
way out of an Apply and equally right on the way out of a discard.

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

### Motor Calibration — Calibration > Motor Positions — **BUILT**

Was one blocking call behind a "Finding home positions" screen. It is a flow
now, because of what `calibrateMotorRotations()` actually does: it turns every
face through four quarter turns and files the readings **relative to wherever
the motors were standing when it started**. A face left out of square produces
four marks out of square by the same amount, and nothing downstream can tell.

So: square the faces by hand FIRST, then sweep. Five steps.

```
   Is the machine empty?              <- confirm; the grippers are about to
   The grippers will close on an         close on an empty centre
   empty centre and turn every face.
   Take the cube OUT before starting.

   U        squared 2013  m0 +3      <- the list. Six motors and a Save row.
   R                1502  m1 -118       Wheel scrolls, SELECT enters a row.
   F                 877  m2 +9         After the raw angle: the nearest
   D            err -1                  STORED mark and the signed error to
   L                3011  m3 -2         it, in counts — what the aligner homes
   B                1244  m0 +0         to. An encoder fault is a fault, not
   Save calibration  4 of 6 squared     a plausible number.

              +3 steps   m0 +3       <- the dial, for one motor. Wheel steps
                  ( 2013 )              it; SELECT accepts, LEFT does not.
                   Motor U              Headline: steps jogged, nearest mark,
         marks 2013 3037 4061 989       error. Sub-line: the four stored marks
                                        the sweep will replace.
```

The mark readouts exist so a face squared by eye can be checked against what
the machine will actually home to, and so "what does home currently mean for
this motor" is answerable from the panel. "m1 -118" is a face 118 counts
(~10 degrees) off its mark; both values come from `CubeSystem::encError()`
over `MotorEncoder::getCalibration()`, the aligner's own test, so the screen
and the aligner cannot disagree. Blank until a calibration exists.

**Rows, not bars, and this is the exception that proves the rule.** Every row of
the list IS a choice, which is what bar art is for — but bars have five slots
and this list has seven, so they cannot be used at all. The cursor is a marked
row instead, exactly as the jog page next door does it. Seven rows is the whole
of `kOpLines` and they only fit with no headline and no sub-line, which is the
Motor Sensors page's shape.

**The clamp is the same three lines as Loading and ModeClamp** — bottom, ring,
top — and honours the abort latch the same way. Every exit from the flow, LEFT
or chord or fault, RELEASES: the flow shut the grippers and the menu has no
idea it happened.

**What SELECT means on the dial** is worth writing down because it is a real
fork. It accepts the motor's PHYSICAL alignment and writes nothing; the Save
row's sweep is what produces all twenty-four calibration values. The other
reading — SELECT writes the current reading through
`MotorEncoder::setCalibration()` — would be overwritten by the sweep moments
later, which makes the dial ceremonial. The physical alignment survives to the
sweep because `resetMotorPos()` only zeroes the step counters; it commands no
travel.

One detent is one step: at 100 steps per quarter turn and 4096 encoder counts
per revolution that is about 10 counts, half the alignment tolerance the
machine works to. It was two steps — one tolerance — until the bench found
that too coarse to centre a face inside the band rather than at one edge of
it, and the sweep's four marks are only as square as the dial left them.
45 degrees out is 50 detents, which nobody squares a face from.

The dial page owns its input like the jog page and the tuning editor, and for
the same reason — the wheel steps a motor there, and `pollEvent()` collapses
the wheel and the UP/DOWN buttons into one meaning.

### Stats — top level — **BUILT**

Six rows — solves, best, average, last, run time, faults — which is exactly
enough, so resist a seventh. The numbers are real now: an EEPROM block of
counters (`CubeStats`), a block of ITS OWN rather than a corner of the tuning
one: solve counts change every run and tuning changes almost never, so sharing
would rewrite the tuning bytes on every solve for nothing.

---

## Build order, as it happened

Roughly in this order:

- **Patterns** — the four states and the preview pane came first; the move
  sequences now run, gated on a solved cube.
- **Scramble Solve** — the two phases with one bar throughout. (It was built
  to recolor between them; it is red end to end since the frame became
  wayfinding.)
- **Step Solve** — the ribbon works and the moves execute, one per SELECT.
- **Demo Mode** — scramble and solve on a loop with a run counter, showing the
  moves rather than only a count.
- **Color Sensors** and **Motor Sensors** — real sensor reads, **throttled** and
  round-robin: every refresh is I²C traffic on the bus the wheel is also using.
- **Hardware Test** — the Actuators page, ported into the firmware's
  Diagnostics menu.
- **Tuning** — six sections editing the real values, with defaults in the
  source and overrides in EEPROM, persistence and reset included.
- **Color as wayfinding** — one color per branch, carried out of the menu and
  through the operation screens, plus the Settings reshuffle above.

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
