# Melee-Style Menu Theme — LVGL Port Handoff

Context for the implementing agent: this document hands off a finished visual
design to be applied to the existing menu/submenu structure in this repo
(CubeSolverProject, Teensy 4.x + ILI9341 320x240 via ILI9341_T4, LVGL v9
already integrated in `Code/libraries/CubeSolver/CubeDisplay.*`, rotary
encoder + button input already working).

Two reference files accompany this doc and are the source of truth:

- `melee-menu-mockup.html` — the full-resolution (1200x900) master design.
  Open it in a browser to see intended look/motion. All geometry below derives
  from it at scale factor 3.75.
- `melee-menu-lvgl-preview.html` — the same design rebuilt natively at 320x240
  using ONLY LVGL-feasible techniques, with hardware-tuned sizes. **When this
  doc and the master disagree on a size, the LVGL preview wins** — its values
  were readability-tuned for the panel. Its `<script>` contains the exact
  final numbers.

## Design tokens

Theme palette (border recolors per SELECTED item, not per screen):

| key    | band fill | band edge (rim/highlight) |
|--------|-----------|---------------------------|
| green  | #14522a   | #4ade70 |
| blue   | #1d2680   | #6d7cf0 |
| red    | #6e1a10   | #f0603a |
| violet | #4a1a72   | #ae6af0 |
| yellow | #6e5a10   | #e8cf3a |
| purple | #33176e   | #8a62e8 |

Other colors:
- Unselected bar: fill rgba(2,2,12,0.86) (near-black, slightly translucent),
  NO crisp outline; defined by glow only. Glow: wide layer #f5aa14 @ 0.9,
  tight layer #ffc030 @ 0.95. Label gold #f0a018 with dark outline #080512.
- Selected bar: FLAT #fbff47 (no gradient), black-ish label #151000,
  glow layers #f8ff5c / #fdffa8. Cursor orb core radial #feffe0 -> #fbff5e ->
  #eef23c; rings #fcffd8, sonar rings #f2fa78.
- Title / NEXT SCREEN / description text: Impact-style condensed heavy face,
  grey rgba(172,175,186,.82) title, rgba(205,210,222,.8) NEXT SCREEN,
  rgba(248,250,255,.95) description. No text shadows or outlines on these.
- Bar label font: Fira Sans Condensed Bold (or nearest converted equivalent).
- Teal pane: flat rgba(70,175,185,0.16) fill (no gradient), border
  rgba(175,235,240,0.75), corner radius ONLY top-right and bottom-left.
- Background: near-black indigo with regional variation (see baked-asset note).

## Geometry (native 320x240 px, from the LVGL preview)

- Bars: height 22, width 141. Left tip is a point at 72% of bar height
  (below center) with a short chamfer (inset = 0.34 * h). Right end is a
  semicircular cap; concentric with it: a half-target arc at r*0.72, socket
  ring at r*0.46, rotating "comma" glyph inside.
- Row x-offsets zigzag (not a staircase): [52.8, 45.6, 24.3, 41.1, 30.1]
  (master 1200-space [198,171,91,154,113] / 3.75). 5-item screens: first row
  center-y 70, step 27. Fewer items: start 76, step 29.
- Selected state does NOT move or scale the bar — highlight change only.
- Border: one continuous band, drawn in the master 1200x900 coordinate space
  and rasterized to 320x240 (the SVG in both HTML files carries the exact
  path). Thin horizontal runs (8 units), thicker perfectly-vertical sides
  (14 units), wide bottom ribbon (~51 units) produced purely by oversized
  inner fillets at the bottom corners. Sharp-angled notch on the top edge
  seats the title; three band-styled diagonal bars (19 units wide,
  overlapping by 3) nestle against the LEFT face of the step. The bottom
  ribbon stops short of the description box (gap in path at master-x 233 and
  968). The low line does NOT continue right of the step.
- Description box: 182x21 at (69,199), Impact ~11px text, border 2px
  top/bottom + 1px sides, radius 4, embedded in the ribbon split.
- NEXT SCREEN: grey outline polygon with a trapezoidal indent on its left
  edge; vertical label (~5px, wide tracking) centered in the indent. Pane:
  66x96 at (220,69), tilted (rotateY -26deg in the mockup; in LVGL either a
  pre-skewed baked image or a plain rect if perspective is dropped), preview
  text ~9px bold, top-left aligned, swaps per selection. Pane draws BEHIND
  the border.

## Animations (all LVGL-native)

- Sonar (selected socket): rings appear at outer radius and travel INWARD
  (scale 1.7 -> 0.5 with fade). Two rings per cycle 160 ms apart, converge
  over ~52% of a 2.1 s cycle, then rest. Rings are soft (baked blur).
  LVGL: `lv_anim` on zoom + opacity of a small ring image; two anims with a
  start-delay offset. Comma glyph spins continuously (2.6 s/rev).
- Wheel transition (screen change): the whole bar group rotates about a
  pivot OFF-SCREEN RIGHT at (389,123) native. Old bars: rotate to -24 deg
  over 160 ms ease-in with late fade; new bars: build pre-rotated +28 deg,
  rotate to 0 over 200 ms with slight overshoot
  (cubic-bezier(.18,.85,.32,1.07)). Back navigation mirrors the signs.
  Title, border color, caption, and preview all swap at transition START.
  LVGL: `transform_rotation` + `transform_pivot` on a container, or rotate
  bars individually if container rotation is too slow.
- Selection change: border recolor is INSTANT; caption/preview do a ~110 ms
  fade-out/in swap.
- Leaf confirm (no submenu): two quick blinks of the selected bar (~90 ms
  steps), nothing else. No white flash anywhere.

## Asset strategy (the key to performance)

1. Background = ONE static full-screen image (grid with perspective warp,
   swirls, stars, green dashes, vignette, faint scanlines all baked). Render
   it by screenshotting the LVGL preview's background layer at 1x, convert to
   RGB565 with the LVGL image converter, store in flash.
2. Border = white/alpha mask image (render the band paths in white),
   recolored at runtime with `lv_obj_set_style_img_recolor` per theme. This
   gives all six theme colors from one asset. Note fill is ~55% opaque with a
   brighter rim — bake that alpha/luminance structure into the mask.
3. Bars = baked images with glow included: one unselected + one selected per
   width (widths are uniform, so 2 assets + label text drawn on top with a
   converted font), plus the socket/orb elements as small images.
4. Fonts: convert Fira Sans Condensed Bold at ~12px (bar labels) and an
   Impact-like face at ~11px (description) and ~13px (title) via the LVGL
   font converter. Preview pane text ~9px.

## Integration notes for this repo

- Menu data model: mirror the `SCREENS` / `THEMES` objects at the top of the
  LVGL preview's script — the repo's existing menu/submenu structure should
  map onto items with {label, caption, preview, goto, theme}.
- Input: wire LVGL's encoder input device (`LV_INDEV_TYPE_ENCODER`) to the
  existing rotary encoder + button; Back = long-press or a dedicated input
  per the hardware.
- The agent cannot flash or see the physical panel: after each visual
  change, ask the user to compile/flash and report back (photos of the panel
  can be shared into the session for comparison against the HTML preview).
- Verify LVGL version in the repo before using v9-only APIs
  (`transform_pivot`, image recolor behavior).
