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

1. **Cube net** — 54 stickers, unfolded. Serves Cube State, Patterns previews,
   and a scan-review screen. By far the highest-value missing piece; three
   screens are waiting on it.
2. **Move ribbon** — a sequence with a cursor on the current entry. Step Solve
   needs it; Solve and Demo get better with it.
3. **Scrolling status list** — Fault Log. More than six rows with a position
   readout.
4. **Value editor** — Parameters and Servo Positions. The wheel changes a
   number instead of moving a cursor. This is a new *interaction*, not just new
   art, and is the one worth thinking hardest about.
5. **Row status marks** — a status row whose value is green/red rather than
   grey. Hardware Test wants it; small change to `setOpLines()`.

### On the cube net

54 stickers cannot be 54 LVGL objects — that is most of the pool for one
screen. Use `lv_canvas` with a **static buffer, not one from `LV_MEM`**: a
12x9-sticker net at 8 px/sticker is 96x72, which is 13.8 KB of RGB565. That is
nothing in Teensy RAM and three times the whole LVGL pool, so it must be a plain
static array handed to `lv_canvas_set_buffer()`.

Draw it once per change, not per frame. The net is static between moves.

```
        ┌──┬──┬──┐
        │  U     │            standard unfolded cross:
        ├──┼──┼──┼──┬──┬──┐   4 faces wide, 3 tall
        │  L  │  F  │  R  │  B     at 8 px/sticker -> 96 x 72
        ├──┼──┼──┼──┴──┴──┘   fits the content area with room to spare
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

Running one is then just the solve screen with the headline naming the pattern.
Blocked on the cube net.

### Cube State — Diagnostics

The full net, as large as fits, plus whether the stored state is valid.

```
        ┌────────────────────┐
        │      cube net      │            8 px/sticker
        └────────────────────┘
        State      valid                   <- or "impossible"
```

The most direct use of the net. Also the natural home for a **scan review**: the
same screen, reachable after a failed scan, with the ambiguous stickers marked —
`scanColor`/`scanAlt`/`scanConf` already record everything needed for that, and
the TODO in `CubeSolver.ino` says as much.

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

Exercise each actuator in turn and report. A checklist, not a menu.

```
        Top servo      OK                  <- value in green
        Bottom servo   OK
        Ring           testing…            <- value in cursor yellow
        Face motors    –
        ▓▓▓▓▓▓▓▓░░░░░░░░░░░░
```

Needs the small `setOpLines()` addition for per-row value colour. Everything
else exists.

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

1. **Scramble Solve** — no new primitives, proves the phase-colour idea.
2. **Row status marks** then **Hardware Test** — smallest new primitive, and
   Hardware Test is the most useful diagnostic to have on the bench.
3. **Cube net** — then **Cube State**, then **Patterns**, then scan review.
   Three screens unblock at once.
4. **Move ribbon** then **Step Solve**; Demo Mode falls out nearly free.
5. **Scrolling list** then **Fault Log**.
6. **Value editor** then **Parameters** and **Servo Positions** — last because
   it is a new interaction, and worth having the rest settled before adding a
   second thing the wheel can mean.

Idle Mode can slot in any time after Stats has real numbers.

Every one of these can be built and judged in the simulator with no machine
attached — and `Test Code/Test_Menu` is the right place to prototype a new
primitive before wiring it to hardware.
