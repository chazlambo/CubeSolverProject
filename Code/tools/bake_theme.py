#!/usr/bin/env python3
# =============================================================================
#  bake_theme.py — turn the Melee menu design into LVGL assets
# =============================================================================
#
#  The design ships as two HTML files (docs/theme/melee-menu-mockup.html and
#  melee-menu-lvgl-preview.html). Rather than transcribe their shapes into
#  drawing code by hand, this script rasterizes the *same* SVG paths and CSS
#  through headless Chrome at the panel's native 320x240 and packs the result
#  into LVGL C arrays. When the design changes, re-run this; nothing downstream
#  is hand-maintained.
#
#  Why baked assets at all: the design leans on Gaussian glows, sub-pixel
#  strokes and a perspective-tilted pane. LVGL can draw none of those per-frame
#  on a Teensy at menu speed, but it can blit an image, and the ILI9341_T4 diff
#  engine means an unchanged image costs no SPI traffic at all.
#
#  Colour strategy: the frame band recolors per selected item across six
#  themes. Baking six coloured copies would cost megabytes, so the band ships
#  as white masks that LVGL recolors at draw time — one asset, six colours.
#
#  Those masks are RGB565A8, NOT A8, and that is a hard constraint rather than
#  a preference. LVGL's bin decoder hands an uncompressed variable-source image
#  straight to the draw unit when it can (lv_bin_decoder.c, `use_directly`), so
#  RGB565/RGB565A8 are read in place from flash and cost no RAM. Alpha-only
#  formats take a different branch: decode_alpha_only() calls lv_draw_buf_create
#  and copies the whole image into a fresh w*h RAM buffer. A 255x194 A8 band is
#  49 KB against this project's 32 KB LV_MEM pool, so it fails to allocate — and
#  with LV_USE_LOG at 0, it fails *silently* and simply never appears.
#
#  RGB565A8 costs 3 bytes per pixel instead of 1, which is the whole reason the
#  asset total is ~570 KB rather than ~350 KB. On a Teensy 4.1 that is flash
#  this project has (the solver's own tables are 4.14 MiB of 7.9 MB) traded for
#  RAM it does not.
#
#  Usage:
#      python3 Code/tools/bake_theme.py            # images + fonts
#      python3 Code/tools/bake_theme.py --no-fonts # images only (no network)
#
#  Needs: google-chrome, python3-pil, and for fonts npx + network access.
# =============================================================================

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
# Generated files land in the library's utility/ folder, not a theme/assets/
# tree, and that is not a stylistic choice. Code/libraries/CubeSolver has no
# library.properties, so the Arduino IDE treats it as a 1.0-format library and
# compiles exactly two places: the library root and utility/. Sources in any
# other subdirectory are silently ignored — the sketch would build, then fail
# to link against every asset here.
OUT = os.path.join(REPO, "Code", "libraries", "CubeSolver", "utility")
ASSETS = OUT

# ---------------------------------------------------------------------------
#  Geometry, mirrored from the LVGL preview's <script>.
#
#  The preview is authoritative on sizes (the handoff says so explicitly: its
#  numbers were readability-tuned for this panel). Keep these in step with it.
# ---------------------------------------------------------------------------
LX, LY = 320, 240
MASTER_W, MASTER_H = 1200, 900      # the master mockup's coordinate space
S = 3.75                            # master -> native scale factor

BAR_W, BAR_H = 141, 22
ZIGZAG = [198, 171, 91, 154, 113]   # master-space x offsets, reused per row

# Derived bar path values (barSVG() in the preview)
TIP_Y = BAR_H * 0.72                # 15.84 — left point sits below centre
INSET = BAR_H * 0.34                # 7.48  — short steep chamfer
R_CAP = BAR_H / 2 - 0.7             # 10.3  — right semicircular cap
CAP_CX = BAR_W - BAR_H / 2          # 130   — cap centre; socket shares it
R_ARC = R_CAP * 0.72                # half-target arc
R_SOCK = R_CAP * 0.46               # socket ring

BAR_SVG_W, BAR_SVG_H = BAR_W + 26, BAR_H + 26   # 167 x 48, glow included
BAR_VB_X, BAR_VB_Y = -6, -13                    # viewBox origin

BODY_PATH = (f"M 0 {TIP_Y} L {INSET} 0.7 H {CAP_CX} "
             f"A {R_CAP} {R_CAP} 0 0 1 {CAP_CX} {BAR_H - 0.7} H {INSET} Z")

# The band: one continuous frame plus three diagonal bars against the step.
BAND_PATHS = [
    ("M 233 813 L 167 813 Q 127 813 127 773 L 127 180 "
     "Q 127 139 168 139 L 515 139 L 570 86 L 1040 86 Q 1078 86 1078 124 "
     "L 1078 775 Q 1078 813 1038 813 L 968 813 "
     "L 968 762 L 1008 762 Q 1064 762 1064 706 L 1064 132 Q 1064 94 1030 94 "
     "L 578 94 L 523 147 L 168 147 Q 141 147 141 174 L 141 706 "
     "Q 141 762 197 762 L 233 762 Z"),
    "M 496 139 L 551 86 L 570 86 L 515 139 Z",
    "M 480 139 L 535 86 L 554 86 L 499 139 Z",
    "M 464 139 L 519 86 L 538 86 L 483 139 Z",
]

# NEXT SCREEN outline: grey polygon with a trapezoidal indent on its left edge.
NEXT_FRAME_PATH = ("M 730 208 L 1042 196 L 1042 714 L 730 731 "
                   "L 730 624 L 764 600 L 764 330 L 730 306 Z")

# The sonar ring is baked at its LARGEST animated scale and zoomed *down* by
# LVGL. CSS transform:scale() scales stroke width too, so baking at 1.0 and
# zooming up would thin the stroke and alias it; baking at the top of the range
# reproduces the CSS exactly.
SONAR_MAX = 1.7

FONTS = [
    # (output name, ttf, px size, what it draws)
    ("lv_font_bar_12",   "FiraSansCondensed-Bold.ttf", 12, "bar labels, info rows"),
    ("lv_font_head_16",  "FiraSansCondensed-Bold.ttf", 16, "operation headlines"),
    ("lv_font_prev_9",   "FiraSansCondensed-Bold.ttf", 9,  "preview pane list"),
    ("lv_font_title_13", "Anton-Regular.ttf",          13, "screen title"),
    ("lv_font_desc_11",  "Anton-Regular.ttf",          11, "description bar"),
]

FONT_URLS = {
    "FiraSansCondensed-Bold.ttf":
        "https://github.com/google/fonts/raw/main/ofl/firasanscondensed/FiraSansCondensed-Bold.ttf",
    # Anton is the standard libre stand-in for Impact: same condensed heavy
    # grotesque proportions, and unlike Impact it is redistributable.
    "Anton-Regular.ttf":
        "https://github.com/google/fonts/raw/main/ofl/anton/Anton-Regular.ttf",
}


# ---------------------------------------------------------------------------
#  Chrome rasterization
# ---------------------------------------------------------------------------
def chrome_bin():
    for c in ("google-chrome", "chromium", "chromium-browser", "google-chrome-stable"):
        p = shutil.which(c)
        if p:
            return p
    sys.exit("error: no chrome/chromium on PATH; needed to rasterize the design")


def render(html, w, h, tmp):
    """Render an HTML fragment at exactly w*h CSS px and return an RGBA image."""
    src = os.path.join(tmp, "layer.html")
    png = os.path.join(tmp, "layer.png")
    if os.path.exists(png):
        os.remove(png)
    with open(src, "w") as f:
        f.write("<!DOCTYPE html><html><head><meta charset='utf-8'><style>"
                "html,body{margin:0;padding:0;background:transparent;overflow:hidden;}"
                "svg{display:block;}</style></head><body>" + html + "</body></html>")
    subprocess.run(
        [chrome_bin(), "--headless", "--disable-gpu", "--no-sandbox",
         f"--screenshot={png}", f"--window-size={w},{h}",
         "--force-device-scale-factor=1", "--default-background-color=00000000",
         "--hide-scrollbars", "--disable-lcd-text", src],
        check=True, capture_output=True, timeout=120)
    if not os.path.exists(png):
        sys.exit("error: chrome produced no screenshot")
    return Image.open(png).convert("RGBA")


def svg_native(body, defs=""):
    """An SVG in master (1200x900) coordinates, rasterized to the panel size."""
    return (f"<svg width='{LX}' height='{LY}' viewBox='0 0 {MASTER_W} {MASTER_H}' "
            f"fill='none' xmlns='http://www.w3.org/2000/svg'>"
            f"<defs>{defs}</defs>{body}</svg>")


# ---------------------------------------------------------------------------
#  C emission
# ---------------------------------------------------------------------------
HDR = """// Generated by Code/tools/bake_theme.py — do not edit by hand.
#include "theme_progmem.h"

"""


def _dsc(name, w, h, stride, cf, arr):
    return (f"\nconst lv_image_dsc_t {name} = {{\n"
            f"    .header.magic  = LV_IMAGE_HEADER_MAGIC,\n"
            f"    .header.cf     = {cf},\n"
            f"    .header.flags  = 0,\n"
            f"    .header.w      = {w},\n"
            f"    .header.h      = {h},\n"
            f"    .header.stride = {stride},\n"
            f"    .data_size     = sizeof({arr}),\n"
            f"    .data          = {arr},\n"
            f"}};\n")


def _bytes_c(data, per_line=16):
    out = []
    for i in range(0, len(data), per_line):
        out.append("    " + " ".join(f"0x{b:02x}," for b in data[i:i + per_line]))
    return "\n".join(out)


def emit_mask(name, img):
    """A recolorable mask: white pixels carrying the shape's alpha.

    Emitted as RGB565A8 rather than A8 on purpose — see the note at the top of
    this file. A8 would be a third of the size and would never render.
    """
    w, h = img.size
    white = Image.new("RGBA", (w, h), (255, 255, 255, 255))
    white.putalpha(img.getchannel("A"))
    return emit_rgb565a8(name, white)


def emit_rgb565(name, img):
    """Opaque colour, panel-native depth."""
    rgb = img.convert("RGB")
    w, h = rgb.size
    px = rgb.load()
    data = bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            data += bytes((v & 0xFF, v >> 8))       # little-endian, as LVGL reads it
    arr = f"{name}_map"
    body = (HDR + f"static const uint8_t {arr}[] PROGMEM = {{\n"
            + _bytes_c(data) + "\n};\n"
            + _dsc(name, w, h, w * 2, "LV_COLOR_FORMAT_RGB565", arr))
    _write(name, body)
    return w, h, len(data)


def emit_rgb565a8(name, img):
    """Colour + alpha: the RGB565 plane followed by the A8 plane."""
    w, h = img.size
    px = img.load()
    colour = bytearray()
    alpha = bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            colour += bytes((v & 0xFF, v >> 8))
            alpha.append(a)
    data = bytes(colour + alpha)
    arr = f"{name}_map"
    body = (HDR + f"static const uint8_t {arr}[] PROGMEM = {{\n"
            + _bytes_c(data) + "\n};\n"
            + _dsc(name, w, h, w * 2, "LV_COLOR_FORMAT_RGB565A8", arr))
    _write(name, body)
    return w, h, len(data)


def _write(name, body):
    os.makedirs(ASSETS, exist_ok=True)
    with open(os.path.join(ASSETS, f"{name}.c"), "w") as f:
        f.write(body)


def crop(img):
    """Trim fully-transparent margins; return (image, x_offset, y_offset)."""
    bb = img.getchannel("A").getbbox()
    if bb is None:
        return img, 0, 0
    return img.crop(bb), bb[0], bb[1]


# ---------------------------------------------------------------------------
#  Layers
# ---------------------------------------------------------------------------
def bake_images(manifest, tmp):
    # ---- background: one static full-screen image -------------------------
    # Grid warp, regional colour variation, vignette and scanlines all baked.
    # This is the only opaque layer; everything else composites over it.
    bg = ("<div style=\"position:absolute;inset:0;width:320px;height:240px;"
          "overflow:hidden;background:"
          "repeating-linear-gradient(0deg, rgba(0,0,0,.06) 0 1px, transparent 1px 3px),"
          "radial-gradient(42% 30% at 78% 22%, rgba(58,52,150,.30), transparent 70%),"
          "radial-gradient(34% 26% at 16% 62%, rgba(38,58,138,.20), transparent 72%),"
          "radial-gradient(46% 36% at 60% 88%, rgba(46,26,96,.18), transparent 75%),"
          "radial-gradient(115% 90% at 58% 36%, #06051a 0%, #040311 48%, #02020b 75%, #010107 100%);\">"
          "<div style=\"position:absolute;inset:-10%;background:"
          "repeating-linear-gradient(0deg, transparent 0 10px, rgba(66,78,205,.22) 10px 11px),"
          "repeating-linear-gradient(90deg, transparent 0 10px, rgba(66,78,205,.22) 10px 11px);"
          "transform:perspective(300px) rotateX(17deg) rotateY(-7deg) scale(1.28);"
          "transform-origin:50% 42%;opacity:.7;\"></div>"
          "<div style=\"position:absolute;inset:0;background:"
          "radial-gradient(115% 100% at 50% 45%, transparent 40%, rgba(0,0,5,.88) 100%);\"></div>"
          "</div>")
    img = render(bg, LX, LY, tmp)
    # Composite onto the panel's own black so the result is fully opaque.
    flat = Image.new("RGBA", (LX, LY), (2, 2, 8, 255))
    flat.alpha_composite(img)
    manifest.append(("theme_bg", 0, 0) + emit_rgb565("theme_bg", flat))

    # ---- band fill + edge -------------------------------------------------
    # Two masks rather than one: the design's fill (#14522a at 55%) and edge
    # (#4ade70) are different hues, not two alphas of one hue, so a single
    # recolored mask cannot produce both. The 0.55 fill opacity is baked into
    # the fill mask's alpha, leaving the runtime recolor at full opacity.
    fill_body = "".join(
        f"<path d='{p}' fill='#fff' fill-opacity='.55' stroke='none'/>" for p in BAND_PATHS)
    edge_body = "".join(
        f"<path d='{p}' fill='none' stroke='#fff' stroke-width='2.2' "
        f"stroke-linejoin='miter'/>" for p in BAND_PATHS)
    for nm, body in (("theme_band_fill", fill_body), ("theme_band_edge", edge_body)):
        c, x, y = crop(render(svg_native(body), LX, LY, tmp))
        manifest.append((nm, x, y) + emit_mask(nm, c))

    # ---- NEXT SCREEN outline ---------------------------------------------
    nf = (f"<path d='{NEXT_FRAME_PATH}' fill='none' stroke='#fff' "
          f"stroke-width='4' stroke-linejoin='miter'/>")
    c, x, y = crop(render(svg_native(nf), LX, LY, tmp))
    manifest.append(("theme_next_frame", x, y) + emit_mask("theme_next_frame", c))

    # ---- the two bar states ----------------------------------------------
    # Labels are NOT baked: they are LVGL text drawn on top, so the same two
    # images serve every menu entry. Widths are uniform, so two assets is all.
    defs = ("<filter id='gW' x='-50%' y='-120%' width='200%' height='340%'>"
            "<feGaussianBlur stdDeviation='3.4'/></filter>"
            "<filter id='gT' x='-30%' y='-60%' width='160%' height='220%'>"
            "<feGaussianBlur stdDeviation='0.8'/></filter>")

    def bar_svg(inner):
        return (f"<svg width='{BAR_SVG_W}' height='{BAR_SVG_H}' "
                f"viewBox='{BAR_VB_X} {BAR_VB_Y} {BAR_SVG_W} {BAR_SVG_H}' fill='none' "
                f"xmlns='http://www.w3.org/2000/svg'><defs>{defs}</defs>{inner}</svg>")

    def glows(wide_col, wide_opa, tight_col, tight_opa):
        return (f"<path d='{BODY_PATH}' fill='none' stroke='{wide_col}' "
                f"opacity='{wide_opa}' stroke-width='7.5' filter='url(#gW)'/>"
                f"<path d='{BODY_PATH}' fill='none' stroke='{tight_col}' "
                f"opacity='{tight_opa}' stroke-width='2.4' filter='url(#gT)'/>")

    comma_r = R_SOCK * 0.55
    socket = (f"<g transform='translate({CAP_CX},{BAR_H / 2})'>"
              f"<path d='M 0 {-R_ARC} A {R_ARC} {R_ARC} 0 0 1 0 {R_ARC}' fill='none' "
              f"stroke='#d09018' stroke-width='0.9'/>"
              f"<circle r='{R_SOCK}' stroke='#d09018' stroke-width='1' "
              f"fill='rgba(3,2,12,.55)'/>"
              f"<path d='M 0 {-comma_r} A {comma_r} {comma_r} 0 1 1 {-comma_r} 0 L 0 0 Z' "
              f"fill='#d09018'/></g>")

    unsel = bar_svg(glows("#f5aa14", ".9", "#ffc030", ".95")
                    + f"<path d='{BODY_PATH}' fill='rgba(2,2,12,.86)' stroke='none'/>"
                    + socket)
    # Selected is FLAT #fbff47 — deliberately no gradient — and drops the
    # socket furniture entirely; the cursor orb takes that spot.
    sel = bar_svg(glows("#f8ff5c", "1", "#fdffa8", ".95")
                  + f"<path d='{BODY_PATH}' fill='#fbff47' stroke='none'/>")

    for nm, svg in (("theme_bar_unsel", unsel), ("theme_bar_sel", sel)):
        img = render(svg, BAR_SVG_W, BAR_SVG_H, tmp)
        manifest.append((nm, 0, 0) + emit_rgb565a8(nm, img))

    # ---- cursor orb (static parts) ---------------------------------------
    # Kept on a centred square canvas: its centre is the socket centre the
    # renderer positions it by, and cropping would move that.
    ORB = 48
    orb = (f"<svg width='{ORB}' height='{ORB}' viewBox='{-ORB/2} {-ORB/2} {ORB} {ORB}' "
           f"fill='none' xmlns='http://www.w3.org/2000/svg'><defs>"
           f"<filter id='oG' x='-120%' y='-120%' width='340%' height='340%'>"
           f"<feGaussianBlur stdDeviation='2.4'/></filter>"
           f"<radialGradient id='orbCore'><stop offset='0' stop-color='#feffe0'/>"
           f"<stop offset='.55' stop-color='#fbff5e'/>"
           f"<stop offset='1' stop-color='#eef23c'/></radialGradient></defs>"
           f"<circle r='{BAR_H*0.62}' fill='#f8ff4e' opacity='.7' filter='url(#oG)'/>"
           f"<circle r='{BAR_H*0.44}' fill='none' stroke='#fcffd8' stroke-width='1.4'/>"
           f"<circle r='{BAR_H*0.34}' fill='url(#orbCore)'/></svg>")
    manifest.append(("theme_orb", 0, 0) + emit_rgb565a8("theme_orb", render(orb, ORB, ORB, tmp)))

    # ---- spinning comma glyph --------------------------------------------
    # Centred on its own canvas so LVGL's rotation pivot is the image centre.
    CM = 16
    cr = R_SOCK * 0.6
    comma = (f"<svg width='{CM}' height='{CM}' viewBox='{-CM/2} {-CM/2} {CM} {CM}' "
             f"fill='none' xmlns='http://www.w3.org/2000/svg'>"
             f"<path d='M 0 {-cr} A {cr} {cr} 0 1 1 {-cr} 0 L 0 0 Z' fill='#8a6a00'/></svg>")
    manifest.append(("theme_comma", 0, 0) + emit_rgb565a8("theme_comma", render(comma, CM, CM, tmp)))

    # ---- sonar ring -------------------------------------------------------
    SR = 56
    sr_r = BAR_H * 0.52 * SONAR_MAX
    sonar = (f"<svg width='{SR}' height='{SR}' viewBox='{-SR/2} {-SR/2} {SR} {SR}' "
             f"fill='none' xmlns='http://www.w3.org/2000/svg'><defs>"
             f"<filter id='pS' x='-80%' y='-80%' width='260%' height='260%'>"
             f"<feGaussianBlur stdDeviation='{0.65*SONAR_MAX}'/></filter></defs>"
             f"<circle r='{sr_r}' fill='none' stroke='#f2fa78' "
             f"stroke-width='{1.7*SONAR_MAX}' filter='url(#pS)'/></svg>")
    manifest.append(("theme_sonar", 0, 0) + emit_rgb565a8("theme_sonar", render(sonar, SR, SR, tmp)))

    # ---- teal preview pane ------------------------------------------------
    # Baked pre-skewed: LVGL has neither perspective nor per-corner radius, and
    # this pane needs both (radius on top-right and bottom-left only).
    pane = ("<div style=\"position:absolute;left:20px;top:20px;width:66px;height:96px;"
            "transform:perspective(240px) rotateY(-26deg);transform-origin:left center;"
            "border-radius:0 5px 0 5px;background:rgba(70,175,185,.16);"
            "border:1px solid rgba(175,235,240,.75);\"></div>")
    img = render(pane, 160, 160, tmp)
    c, x, y = crop(img)
    # Offsets are relative to the 20,20 the fragment was drawn at.
    manifest.append(("theme_pane", x - 20, y - 20) + emit_rgb565a8("theme_pane", c))

    # ---- NEXT SCREEN vertical label --------------------------------------
    # Vertical writing-mode with wide tracking; LVGL has no vertical text.
    nl = ("<div style=\"position:absolute;left:20px;top:20px;height:70px;"
          "writing-mode:vertical-rl;transform:rotate(180deg);"
          "font-family:Impact,'Anton','Arial Narrow',sans-serif;font-size:5px;"
          "letter-spacing:2.5px;color:#fff;text-align:center;\">NEXT SCREEN</div>")
    c, x, y = crop(render(nl, 160, 160, tmp))
    manifest.append(("theme_next_label", x - 20, y - 20) + emit_mask("theme_next_label", c))


# ---------------------------------------------------------------------------
#  Fonts
# ---------------------------------------------------------------------------
def bake_fonts(tmp):
    fdir = os.path.join(tmp, "fonts")
    os.makedirs(fdir, exist_ok=True)
    for ttf, url in FONT_URLS.items():
        dst = os.path.join(fdir, ttf)
        cached = os.path.join(HERE, "fonts", ttf)
        if os.path.exists(cached):
            shutil.copy(cached, dst)
            continue
        print(f"  fetching {ttf}")
        subprocess.run(["curl", "-sL", url, "-o", dst], check=True, timeout=180)

    for name, ttf, size, what in FONTS:
        out = os.path.join(ASSETS, f"{name}.c")
        print(f"  {name}: {ttf} @ {size}px  ({what})")
        subprocess.run(
            ["npx", "-y", "lv_font_conv@1.5.3",
             "--font", os.path.join(fdir, ttf), "--size", str(size),
             "--bpp", "4", "--format", "lvgl", "--lv-include", "lvgl.h",
             "--no-compress", "-r", "0x20-0x7F", "-o", out],
            check=True, capture_output=True, timeout=600)


# ---------------------------------------------------------------------------
#  Generated header
# ---------------------------------------------------------------------------
def write_header(manifest):
    lines = [
        "// Generated by Code/tools/bake_theme.py — do not edit by hand.",
        "//",
        "// Placement constants come from the rasterizer, not from hand-measuring",
        "// the design: each cropped asset records where its top-left corner sits",
        "// on the 320x240 panel, so re-baking a changed design moves the widgets",
        "// automatically.",
        "#ifndef CubeThemeAssets_h",
        "#define CubeThemeAssets_h",
        "",
        "#include <lvgl.h>",
        "",
        "#ifdef __cplusplus",
        "extern \"C\" {",
        "#endif",
        "",
    ]
    total = 0
    for name, x, y, w, h, nbytes in manifest:
        total += nbytes
        lines.append(f"// {w}x{h}, {nbytes} bytes")
        lines.append(f"extern const lv_image_dsc_t {name};")
        lines.append(f"#define {name.upper()}_X {x}")
        lines.append(f"#define {name.upper()}_Y {y}")
        lines.append(f"#define {name.upper()}_W {w}")
        lines.append(f"#define {name.upper()}_H {h}")
        lines.append("")
    for name, _ttf, size, what in FONTS:
        lines.append(f"extern const lv_font_t {name};   // {size}px, {what}")
    lines += [
        "",
        f"// Total image flash: {total} bytes ({total / 1024:.1f} KB)",
        f"#define THEME_IMAGE_BYTES {total}",
        "",
        "#ifdef __cplusplus",
        "}",
        "#endif",
        "",
        "#endif // CubeThemeAssets_h",
    ]
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "CubeThemeAssets.h"), "w") as f:
        f.write("\n".join(lines) + "\n")
    return total


def write_progmem():
    with open(os.path.join(OUT, "theme_progmem.h"), "w") as f:
        f.write("""// Generated by Code/tools/bake_theme.py — do not edit by hand.
//
// Baked assets are large and read-only, so they belong in flash. On Teensy 4.x
// PROGMEM keeps them there (flash is memory-mapped, so LVGL can read them in
// place); on the desktop simulator there is no such attribute and the define
// collapses to nothing. Same idiom the kociemba cache tables use.
#ifndef CubeThemeProgmem_h
#define CubeThemeProgmem_h

#include <lvgl.h>

#if defined(__has_include)
#  if __has_include(<avr/pgmspace.h>)
#    include <avr/pgmspace.h>
#  endif
#endif

#ifndef PROGMEM
#  define PROGMEM
#endif

#endif // CubeThemeProgmem_h
""")


def main():
    ap = argparse.ArgumentParser(description="Bake the Melee menu theme into LVGL assets")
    ap.add_argument("--no-fonts", action="store_true", help="skip font conversion")
    ap.add_argument("--no-images", action="store_true", help="skip image rasterization")
    args = ap.parse_args()

    os.makedirs(ASSETS, exist_ok=True)
    write_progmem()

    manifest = []
    with tempfile.TemporaryDirectory() as tmp:
        if not args.no_images:
            print("rasterizing layers with headless Chrome...")
            bake_images(manifest, tmp)
        if not args.no_fonts:
            print("converting fonts...")
            bake_fonts(tmp)

    if args.no_images:
        # The header carries both the image placement constants and the font
        # declarations, and the image half can only be written by the
        # rasterizer that measured it. Rather than emit a header that silently
        # drops every image, say so and leave the existing one alone — a
        # fonts-only run that added a font must be followed by a full bake.
        print("\nNOTE: --no-images leaves CubeThemeAssets.h untouched.")
        print("      If you added or removed a font, re-run without --no-images.")
        return

    if manifest:
        total = write_header(manifest)
        print(f"\n{'asset':<22}{'size':>12}{'pos':>12}{'bytes':>10}")
        for name, x, y, w, h, nbytes in manifest:
            print(f"{name:<22}{f'{w}x{h}':>12}{f'{x},{y}':>12}{nbytes:>10}")
        print(f"{'':<22}{'':>12}{'total':>12}{total:>10}  ({total/1024:.1f} KB)")


if __name__ == "__main__":
    main()
