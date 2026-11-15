#!/usr/bin/env python3
"""
The README's picture, docs/img/hero.png: the assembled board and the exploded case, side by side, from the
same viewpoint (front right, ~32 deg up), on a transparent background so it sits on GitHub's light or dark
page alike.

    python3 case/render_hero.py        (render.sh runs it; needs kicad-cli, OpenSCAD, numpy, Pillow,
                                        and the makers' 3D models: hardware/lib/3d/fetch.sh)

- Board: kicad-cli, transparent, basic quality (high quality adds a soft shadow that shows as a grey
  smudge on a light page). The Alps encoder model is coloured magenta/orange/blue by its maker; a copy is
  recoloured as the real part (metal shaft and frame, black base) for the render only, so the fetched
  file stays byte-identical (fetch.sh checks it).
- Both orthographic, as kicad-cli renders. Case: per-part meshes, coloured as printed (picod64-case.scad's preview colours) and previewed twice,
  over a light and a dark background; the difference between the two gives each pixel's coverage, so the
  edges are antialiased onto transparency exactly.
"""
import os, re, shutil, subprocess, sys, tempfile
import numpy as np
from PIL import Image

CASE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.dirname(CASE)
HW = os.path.join(TOP, "hardware")
OUT = os.path.join(TOP, "docs", "img", "hero.png")
CAMERA = "--camera=150,-120,140,53,45,20"     # as render.sh's exploded view
BOARD_ROT = "-58,0,30"                          # the same viewpoint for the board (front right, ~32 deg up)
ALPS = "Alps_EC11E-05SW.step"
LUMBERG = "Lumberg_0105_06"
DIN_BLACK = "0.07 0.07 0.07"                    # black PBT
LED_RED = "diffuseColor 0.69999999 0.1 0.050000001"   # the lens in KiCad's LED_D3.0mm, as exported
LED_GREEN = "diffuseColor 0.15 0.62 0.12"
RECOLOUR = {   # the maker's flat colours -> the real part's (a photo, 2026-11-01: bright metal throughout
               # - shaft, threaded bushing, nut, frame - on a green resin base)
    (1.0, 0.0, 1.0): (0.88, 0.88, 0.90),        # shaft: magenta -> bright steel
    (1.0, 0.50196, 0.0): (0.84, 0.84, 0.86),    # bushing and frame: orange -> bright plated steel
    (0.0, 0.0, 1.0): (0.16, 0.66, 0.55),        # base: blue -> the green resin
    (1.0, 1.0, 1.0): (0.80, 0.80, 0.82),        # terminals: white -> tinned
}


def run(*a):
    subprocess.run(a, check=True, capture_output=True)


# The model is one fused solid with one material, so the contacts are found by where they are (model
# coordinates, mm; the footprint turns it over): the legs through the board, a column round each pin, and
# everything wholly inside the open back where the contacts come out of the body and bend down (checked by
# eye from behind and from the side, 2026-11-01: contacts silver, body black, nothing else caught).
DIN_PINS = [(-7.5, -11.2), (7.5, -11.2), (0, -11.2), (-5, -13.7), (5, -13.7), (0, -16.2), (-5, -1.2), (5, -1.2)]
DIN_BACK = ((-6, 6), (-16.8, -10.5), (5.5, 16))
DIN_TIN = "0.66 0.66 0.68"                     # tin-plated CuZn (datasheet), duller than the white the lighting makes of 0.8


def din_colours(w):
    head, *shapes = re.split(r"(?=DEF SHAPE_\d+ Shape)", w)
    used, out = set(), [head]
    for sh in shapes:
        pts = re.search(r"point \[(.*?)\]", sh, re.S).group(1)
        P = np.array([[float(v) * 2.54 for v in q.split()] for q in pts.replace("\n", " ").split(",") if q.strip()])
        mn, mx = P.min(0), P.max(0)
        pin = any(mn[0] >= x - 0.9 and mx[0] <= x + 0.9 and mn[1] >= y - 0.9 and mx[1] <= y + 0.9 for x, y in DIN_PINS)
        back = all(lo <= mn[k] and mx[k] <= hi for k, (lo, hi) in enumerate(DIN_BACK))
        name, col = ("APP_TIN", DIN_TIN) if pin or back else ("APP_PBT", DIN_BLACK)
        app = ("appearance USE %s" % name if name in used else
               "appearance DEF %s Appearance { material Material { diffuseColor %s specularColor 0.1 0.1 0.1 } }"
               % (name, col))
        used.add(name)
        new, n = re.subn(r"appearance (?:DEF \w+ Appearance \{.*?\} \}|USE \w+)", lambda m: app, sh, count=1, flags=re.S)
        assert n == 1, "a DIN socket face without an appearance"
        out.append(new)
    assert used == {"APP_TIN", "APP_PBT"}, used
    return "".join(out)


def board_copy(T):
    """A copy of the board for rendering, its parts in their real colours (encoder, DIN sockets, D1 green)."""
    p = os.path.join(T, "pcb")
    os.makedirs(os.path.join(p, "lib"))
    for f in ("picod64.kicad_pcb", "picod64.kicad_pro"):
        shutil.copy(os.path.join(HW, f), p)
    shutil.copytree(os.path.join(HW, "lib", "3d"), os.path.join(p, "lib", "3d"))
    step = os.path.join(p, "lib", "3d", ALPS)
    if not os.path.exists(step):
        sys.exit("hardware/lib/3d/%s missing: run bash hardware/lib/3d/fetch.sh" % ALPS)
    s = open(step, errors="surrogateescape").read()

    def rc(m):
        v = tuple(round(float(x), 5) for x in m.group(2).split(","))
        for k, new in RECOLOUR.items():
            if all(abs(a - b) < 1e-4 for a, b in zip(v, k)):
                return m.group(1) + ", ".join("%.4f" % c for c in new) + " )"
        return m.group(0)
    s = re.sub(r"(COLOUR_RGB \( '',)([^)]*)\)", rc, s)
    open(step, "w", errors="surrogateescape").write(s)
    # Lumberg 0105 06: black PBT body, tin-plated contacts. KiCad ignores IGES colours (and Lumberg's file
    # assigns none anyway), so the model goes through KiCad's own VRML export (one .wrl per model, in
    # KiCad's 0.1 inch units, as a .wrl footprint model expects), gets its two colours there, and the render
    # copy's footprints point at that instead
    run("kicad-cli", "pcb", "export", "vrml", "--units", "mm", "--models-dir", "wrl", "--models-relative", "-f",
        "-o", os.path.join(p, "board.wrl"), os.path.join(p, "picod64.kicad_pcb"))
    wrl = os.path.join(p, "wrl", LUMBERG + ".wrl")
    w = din_colours(open(wrl).read())          # (read before opening to write: "w" empties it)
    open(wrl, "w").write(w)
    pcb = os.path.join(p, "picod64.kicad_pcb")
    s = open(pcb).read()
    old = '(model "${KIPRJMOD}/lib/3d/%s.igs"' % LUMBERG
    assert s.count(old) == 2, "expected the two DIN sockets' models"
    s = s.replace(old, '(model "${KIPRJMOD}/wrl/%s.wrl"' % LUMBERG)
    # LEDs: KiCad's 3 mm LED model is red; D1 (PWR) is green, Kingbright L-934GD (BOM), D2 (ACT) red L-934ID
    led = open(os.path.join(p, "wrl", "LED_D3.0mm.wrl")).read()
    assert LED_RED in led, "the LED model's red lens material has changed"
    open(os.path.join(p, "wrl", "LED_D3.0mm_green.wrl"), "w").write(led.replace(LED_RED, LED_GREEN))
    i = s.index('(property "Reference" "D1"')
    j, k = s.rfind("\n\t(footprint ", 0, i), s.find("\n\t(footprint ", i)
    fp = s[j:k]
    m = re.search(r'\(model "[^"]*LED_D3\.0mm\.step"', fp)
    assert m, "D1's LED model"
    s = s[:j] + fp.replace(m.group(0), '(model "${KIPRJMOD}/wrl/LED_D3.0mm_green.wrl"') + s[k:]
    open(pcb, "w").write(s)
    return pcb


def board(T):
    pcb = board_copy(T)
    out = os.path.join(T, "board.png")
    run("kicad-cli", "pcb", "render", "--rotate", BOARD_ROT, "--zoom", "0.9", "-w", "1600", "-h", "1300",
        "--quality", "basic", "--background", "transparent", "-o", out, pcb)
    im = Image.open(out).convert("RGBA")
    return im.crop(im.getbbox())


def case(T):
    for part in ("tray", "lid_front_raw", "lid_rear_raw", "knob_raw",
                 "badges_front_fit", "letters_front_fit", "badges_rear_fit", "letters_rear_fit"):
        run("openscad", "-q", os.path.join(CASE, "picod64-case.scad"), "-D", 'part="%s"' % part,
            "-o", os.path.join(T, "m_%s.stl" % part))
    scad = os.path.join(T, "exploded.scad")
    open(scad, "w").write('include <%s/picod64-case.scad>\npart = "none";\n'
                          'color(TRAY_COLOUR) import("%s/m_tray.stl");\n'
                          'translate([0, -12, 25]) color(LID_COLOUR) import("%s/m_lid_front_raw.stl");\n'
                          'translate([0, 12, 25]) color(LID_REAR_COLOUR) import("%s/m_lid_rear_raw.stl");\n'
                          '// the knob, lifted clear of the front lid above its shaft hole\n'
                          'translate([cx(SHAFT_X), cy(SHAFT_Y) - 12, 25 + FRONT_TOP + KNOB_CLR + 4]) '
                          'color(KNOB_COLOUR) import("%s/m_knob_raw.stl");\n'
                          '// the badges (STYLE c64), in their recesses: light plates, dark letters\n'
                          'translate([0, -12, 25]) { color(BADGE_COLOUR) import("%s/m_badges_front_fit.stl"); '
                          'color(BADGE_INK) import("%s/m_letters_front_fit.stl"); }\n'
                          'translate([0, 12, 25]) { color(BADGE_COLOUR) import("%s/m_badges_rear_fit.stl"); '
                          'color(BADGE_INK) import("%s/m_letters_rear_fit.stl"); }\n'
                          % (CASE, T, T, T, T, T, T, T, T))
    ims = []
    for cs in ("Tomorrow", "Tomorrow Night"):          # light and dark background, same model colours
        o = os.path.join(T, "c_%s.png" % cs.replace(" ", "_"))
        run("openscad", "-q", "--colorscheme=" + cs, scad, CAMERA, "--projection=o", "--viewall", "--autocenter",
            "--imgsize=1400,1200", "-o", o)
        ims.append(np.asarray(Image.open(o).convert("RGB")).astype(float))
    c1, c2 = ims
    b1, b2 = c1[0, 0], c2[0, 0]
    a = np.clip(1 - np.abs(c1 - c2).mean(axis=2) / np.abs(b1 - b2).mean(), 0, 1)
    solid = a > 0.999
    assert np.abs(c1 - c2)[solid].max() == 0, "the two backgrounds changed the model's own colours"
    fg = np.where(a[..., None] > 0.01, (c1 - (1 - a[..., None]) * b1) / np.maximum(a[..., None], 1e-3), 0)
    im = Image.fromarray(np.dstack([np.clip(fg, 0, 255), a * 255]).astype(np.uint8), "RGBA")
    return im.crop(im.getbbox())


def board_views():
    """hardware/render/board_top.png and board_bottom.png, in the same real colours (render.sh runs this)."""
    T = tempfile.mkdtemp()
    pcb = board_copy(T)
    for side in ("top", "bottom"):
        run("kicad-cli", "pcb", "render", "--side", side, "--width", "1800", "--height", "1560", "--quality", "high",
            "-o", os.path.join(HW, "render", "board_%s.png" % side), pcb)
    shutil.rmtree(T)


def main():
    if sys.argv[1:] == ["--board-views"]:
        return board_views()
    T = tempfile.mkdtemp()
    try:
        b, c = board(T), case(T)
    finally:
        pass
    H = 560                                            # one height for both, side by side
    b = b.resize((round(b.width * H / b.height), H), Image.LANCZOS)
    c = c.resize((round(c.width * H / c.height), H), Image.LANCZOS)
    gap = 60
    hero = Image.new("RGBA", (b.width + gap + c.width, H), (0, 0, 0, 0))
    hero.alpha_composite(b, (0, 0))
    hero.alpha_composite(c, (b.width + gap, 0))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    hero.save(OUT, optimize=True)
    shutil.rmtree(T)
    print("docs/img/hero.png", hero.size)


if __name__ == "__main__":
    main()
