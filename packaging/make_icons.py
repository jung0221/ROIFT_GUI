#!/usr/bin/env python3
"""Generate ROIFT_GUI app icons: SVG (authoring source), 256px PNG, multi-size ICO.

Design: an axial CT slice on the app's bezel — thorax wall and ribs in ink, both
lungs dark, the left one segmented in the accent blue behind its boundary
contour, with an object seed inside it and a background seed on the spine.

Colours are the Theme.h tokens; geometry lives in a 512px canvas. Re-run after
editing anything here:  python3 packaging/make_icons.py
"""
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICON_DIR = os.path.join(REPO, "resources", "icons")
WIN_DIR = os.path.join(REPO, "packaging", "windows")

# ── Palette ─────────────────────────────────────────────────────────────────
# Theme.h tokens, plus two slice-only greys chosen so soft tissue reads against
# the bezel and lung air reads against soft tissue.
BEZEL = (20, 19, 18)          # #141312 Theme::kBezel — canvas
TISSUE = (78, 76, 70)         # soft tissue
AIR = (27, 26, 24)            # lung parenchyma
BONE = (147, 147, 139)        # #93938B Theme::kInk3 — ribs, spine, sternum
INK = (243, 243, 239)         # #F3F3EF Theme::kInk — object seed
ACCENT_FILL = (69, 104, 255)  # #4568FF Theme::kAccentFill — segmented region
ACCENT_DEEP = (38, 60, 168)   # shaded edge of the region, so the contour reads
ACCENT = (147, 176, 255)      # #93B0FF Theme::kAccent — boundary contour
FLAG = (240, 121, 107)        # #F0796B Theme::kFlag — background seed

# ── Geometry (512 canvas) ───────────────────────────────────────────────────
CANVAS = 512
BEZEL_INSET = 24
BEZEL_R = 116

BODY_C, BODY_R = (256, 260), (196, 168)   # thorax ellipse: centre, radii
SKIN_W = 10

# Each lung is a lobe ellipse with the mediastinum bitten out of its medial side.
MEDIASTINUM = dict(c=(256, 300), r=(54, 116))
LUNG_SEG = dict(c=(150, 250), r=(62, 110), rot=-6)    # segmented (left in image)
LUNG_RAW = dict(c=(362, 250), r=(62, 110), rot=6)

SPINE_C, SPINE_R = (256, 388), 28
STERNUM_C, STERNUM_R = (256, 116), 16
RIB_SCALE, RIB_W = 0.91, 11

SEED_OBJ, SEED_BG = (150, 228), (256, 220)
SEED_R, SEED_RING = 19, 7

CONTOUR_W = 10


# ── Shape helpers ───────────────────────────────────────────────────────────
def _ellipse_pts(c, r, rot_deg=0.0, n=240):
    """Polygon approximation of a rotated ellipse."""
    a = math.radians(rot_deg)
    ca, sa = math.cos(a), math.sin(a)
    out = []
    for i in range(n):
        t = 2 * math.pi * i / n
        x, y = r[0] * math.cos(t), r[1] * math.sin(t)
        out.append((c[0] + x * ca - y * sa, c[1] + x * sa + y * ca))
    return out


def _norm2(p, c, r):
    """Squared normalized radius of p in the ellipse (c, r); < 1 means inside."""
    return ((p[0] - c[0]) / r[0]) ** 2 + ((p[1] - c[1]) / r[1]) ** 2


def _arc_pts(c, r, a0, a1, n=48):
    return [(c[0] + r[0] * math.cos(a0 + (a1 - a0) * i / n),
             c[1] + r[1] * math.sin(a0 + (a1 - a0) * i / n)) for i in range(n + 1)]


def _lung_path(spec):
    """Lobe ellipse minus the mediastinum, as one closed simple polygon."""
    lobe = _ellipse_pts(spec["c"], spec["r"], spec["rot"])
    bc, br = MEDIASTINUM["c"], MEDIASTINUM["r"]
    keep = [_norm2(p, bc, br) >= 1.0 for p in lobe]
    if all(keep):
        return lobe

    n = len(lobe)
    # The kept points form one contiguous run; rotate so it starts at index 0.
    start = next(i for i in range(n) if keep[i] and not keep[i - 1])
    kept = [lobe[(start + k) % n] for k in range(n) if keep[(start + k) % n]]

    # Close the cut by walking the mediastinum's own boundary from the exit
    # point back to the entry point — whichever of the two arcs runs through
    # the lobe's interior is the medial border we want.
    ang = lambda p: math.atan2((p[1] - bc[1]) / br[1], (p[0] - bc[0]) / br[0])
    a0, a1 = ang(kept[-1]), ang(kept[0])
    forward = a1 + 2 * math.pi if a1 < a0 else a1
    for end in (forward, forward - 2 * math.pi):
        mid = _arc_pts(bc, br, a0, end, n=2)[1]
        if _norm2(mid, spec["c"], spec["r"]) < 1.0:   # rotation ignored: the
            return kept + _arc_pts(bc, br, a0, end)   # test only needs a side
    return kept


def _ribs():
    """Short arcs hugging the thorax wall, mirrored across the midline."""
    out = []
    for k in range(4):
        a = -54 + 36 * k
        out.append((a - 11, a + 11))
        out.append((169 - a, 191 - a))
    return out


def _rib_pts(a0, a1):
    return _arc_pts(BODY_C, (BODY_R[0] * RIB_SCALE, BODY_R[1] * RIB_SCALE),
                    math.radians(a0), math.radians(a1), n=24)


def rgb(c):
    return "rgb(%d,%d,%d)" % c


# ── SVG ─────────────────────────────────────────────────────────────────────
def write_svg(path):
    poly = lambda pts: " ".join(f"{x:.1f},{y:.1f}" for x, y in pts)
    seg, raw = _lung_path(LUNG_SEG), _lung_path(LUNG_RAW)

    s = ['<?xml version="1.0" encoding="UTF-8"?>',
         f'<svg xmlns="http://www.w3.org/2000/svg" width="{CANVAS}" height="{CANVAS}" '
         f'viewBox="0 0 {CANVAS} {CANVAS}">',
         '  <defs>',
         '    <radialGradient id="seg" cx="36%" cy="28%" r="82%">',
         f'      <stop offset="0%" stop-color="{rgb(ACCENT_FILL)}"/>',
         f'      <stop offset="100%" stop-color="{rgb(ACCENT_DEEP)}"/>',
         '    </radialGradient>',
         f'    <clipPath id="body"><ellipse cx="{BODY_C[0]}" cy="{BODY_C[1]}" '
         f'rx="{BODY_R[0]}" ry="{BODY_R[1]}"/></clipPath>',
         '  </defs>',
         f'  <rect x="{BEZEL_INSET}" y="{BEZEL_INSET}" '
         f'width="{CANVAS - 2 * BEZEL_INSET}" height="{CANVAS - 2 * BEZEL_INSET}" '
         f'rx="{BEZEL_R}" fill="{rgb(BEZEL)}"/>',
         '  <g clip-path="url(#body)">',
         f'    <ellipse cx="{BODY_C[0]}" cy="{BODY_C[1]}" rx="{BODY_R[0]}" '
         f'ry="{BODY_R[1]}" fill="{rgb(TISSUE)}"/>',
         f'    <polygon points="{poly(raw)}" fill="{rgb(AIR)}"/>',
         f'    <polygon points="{poly(seg)}" fill="{rgb(AIR)}"/>']
    for a0, a1 in _ribs():
        s.append(f'    <polyline points="{poly(_rib_pts(a0, a1))}" fill="none" '
                 f'stroke="{rgb(BONE)}" stroke-width="{RIB_W}" stroke-linecap="round"/>')
    s += [f'    <circle cx="{SPINE_C[0]}" cy="{SPINE_C[1]}" r="{SPINE_R}" fill="{rgb(BONE)}"/>',
          f'    <circle cx="{STERNUM_C[0]}" cy="{STERNUM_C[1]}" r="{STERNUM_R}" fill="{rgb(BONE)}"/>',
          f'    <polygon points="{poly(seg)}" fill="url(#seg)"/>',
          f'    <polygon points="{poly(seg)}" fill="none" stroke="{rgb(ACCENT)}" '
          f'stroke-width="{CONTOUR_W}" stroke-linejoin="round"/>',
          '  </g>',
          f'  <ellipse cx="{BODY_C[0]}" cy="{BODY_C[1]}" rx="{BODY_R[0]}" ry="{BODY_R[1]}" '
          f'fill="none" stroke="{rgb(BONE)}" stroke-width="{SKIN_W}"/>']
    for (cx, cy), fill in ((SEED_OBJ, INK), (SEED_BG, FLAG)):
        s.append(f'  <circle cx="{cx}" cy="{cy}" r="{SEED_R}" fill="{rgb(fill)}" '
                 f'stroke="{rgb(BEZEL)}" stroke-width="{SEED_RING}"/>')
    s.append('</svg>')
    with open(path, "w") as f:
        f.write("\n".join(s) + "\n")


# ── Raster (same design, supersampled) ──────────────────────────────────────
def render(scale):
    """RGBA image of the icon at CANVAS*scale px."""
    size = CANVAS * scale
    S = lambda v: int(round(v * scale))
    pts = lambda ps: [(x * scale, y * scale) for x, y in ps]
    ebox = lambda c, r: [S(c[0] - r[0]), S(c[1] - r[1]), S(c[0] + r[0]), S(c[1] + r[1])]
    cbox = lambda c, r: ebox(c, (r, r))

    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(img).rounded_rectangle(
        [S(BEZEL_INSET), S(BEZEL_INSET), size - S(BEZEL_INSET), size - S(BEZEL_INSET)],
        radius=S(BEZEL_R), fill=BEZEL + (255,))

    # Slice interior on its own layer, pasted through the thorax ellipse mask.
    body = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    bd = ImageDraw.Draw(body)
    bd.ellipse(ebox(BODY_C, BODY_R), fill=TISSUE + (255,))
    seg_poly, raw_poly = _lung_path(LUNG_SEG), _lung_path(LUNG_RAW)
    bd.polygon(pts(raw_poly), fill=AIR + (255,))
    bd.polygon(pts(seg_poly), fill=AIR + (255,))
    for a0, a1 in _ribs():
        bd.line(pts(_rib_pts(a0, a1)), fill=BONE + (255,), width=S(RIB_W), joint="curve")
    bd.ellipse(cbox(SPINE_C, SPINE_R), fill=BONE + (255,))
    bd.ellipse(cbox(STERNUM_C, STERNUM_R), fill=BONE + (255,))
    body.alpha_composite(_gradient_polygon(size, scale, seg_poly))
    ring = pts(seg_poly)
    ImageDraw.Draw(body).line(ring + [ring[0]], fill=ACCENT + (255,),
                              width=S(CONTOUR_W), joint="curve")

    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).ellipse(ebox(BODY_C, BODY_R), fill=255)
    img.paste(body, (0, 0), mask)

    d = ImageDraw.Draw(img)
    d.ellipse(ebox(BODY_C, BODY_R), outline=BONE + (255,), width=S(SKIN_W))
    for centre, fill in ((SEED_OBJ, INK), (SEED_BG, FLAG)):
        d.ellipse(cbox(centre, SEED_R + SEED_RING / 2), fill=BEZEL + (255,))
        d.ellipse(cbox(centre, SEED_R), fill=fill + (255,))
    return img


def _gradient_polygon(size, scale, poly):
    """Polygon filled with the ACCENT_FILL→ACCENT_DEEP radial gradient of the SVG."""
    yy, xx = np.mgrid[0:size, 0:size]
    cx, cy, r = 0.36 * size, 0.28 * size, 0.82 * size
    t = np.clip(np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2) / r, 0, 1)[..., None]
    grad = ((1 - t) * np.array(ACCENT_FILL) + t * np.array(ACCENT_DEEP)).astype(np.uint8)
    shape = Image.new("L", (size, size), 0)
    ImageDraw.Draw(shape).polygon([(x * scale, y * scale) for x, y in poly], fill=255)
    return Image.fromarray(np.dstack([grad, np.array(shape)]), "RGBA")


def main():
    os.makedirs(ICON_DIR, exist_ok=True)
    os.makedirs(WIN_DIR, exist_ok=True)
    svg_path = os.path.join(ICON_DIR, "roift_gui.svg")
    png_path = os.path.join(ICON_DIR, "roift_gui-256.png")
    ico_path = os.path.join(WIN_DIR, "roift_gui.ico")

    write_svg(svg_path)
    master = render(4).resize((256, 256), Image.LANCZOS)
    master.save(png_path)
    # 16/24px lose the ribs entirely; a light sharpen keeps the contour readable.
    master.filter(ImageFilter.UnsharpMask(radius=2, percent=90, threshold=2)).save(
        ico_path, sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)])

    print("wrote:", svg_path, png_path, ico_path, sep="\n  ")


if __name__ == "__main__":
    main()
