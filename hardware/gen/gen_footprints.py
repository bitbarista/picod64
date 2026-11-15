#!/usr/bin/env python3
"""
PicoD64 project footprints (KiCad 9). Re-run:  python3 hardware/gen/gen_footprints.py

IEC_DIN6_DualEarth -- the IEC bus socket, HARDWARE.md "IEC socket" (2026-10-25).
One footprint that takes every common 6-pin 240-degree DIN right-angle socket:

  Signal pins, the same on every source checked:
    5 . 3 . 1  one row, x = -7.5 / 0 / +7.5            (pin numbers per the
    4 . 2      2.5 mm behind, x = -5 / +5               Same Sky SDF-60J drawing,
    6          5.0 mm behind, x = 0                     top view [D])
  Earth/shield tabs, 10 mm IN FRONT of the 5-3-1 row:
    generic sockets (Pi1541 builds)   x = +/-5    (alangarf/tebl C64_IEC [C])
    Lumberg 0105 06, Same Sky SDF-60J x = +/-2.5  (both makers' drawings [D];
                                                   Lumberg 9.9 mm in front)
  so each side is ONE PLATED SLOT spanning x = 2.5..5 (tebl's Pi1541-III used four
  separate holes for the same reason).
  SDF-60J's two plastic posts: NPTH Ø2.4 at x = +/-7.5, 5 mm in front of the row [D].

Holes Ø1.5 (Lumberg's layout; SDF-60J asks Ø1.3 +0.1). Pads 2.4 mm round for
hand soldering. Front of the socket (the board edge) is +y.
⚠ Check against a real sample of each socket before ordering boards.
"""
import os
import re
import uuid

HW = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(HW, "lib", "picod64.pretty")


# Deterministic UUIDs (were uuid4: every run rewrote the library; AUDIT R22/R24)
_NS = uuid.UUID("7a1e2b3c-4d5e-4f60-8172-93a4b5c6d7e8")   # fixed, arbitrary: PicoD64's footprint namespace
_seq = [0]


def U():
    _seq[0] += 1
    return str(uuid.uuid5(_NS, f"seq {_seq[0]}"))


# 3D models, placed BY HAND in KiCad's footprint editor against the real parts and kept here, so a
# re-run cannot drop them (it did: check_sch.sh re-ran this and lost all three; AUDIT R24).
# name -> (file in lib/3d, offset xyz, rotate xyz)
MODELS = {
    "IEC_DIN6_DualEarth": ("Lumberg_0105_06.igs", "0 -11.21 19.455", "180 0 0"),
    "microSD_GCT_MEM2075": ("GCT_MEM2075-00-140-01-A.step", "-0.48 -3.27 0", "-90 0 0"),
    "EC11E_Switch_Alps": ("Alps_EC11E-05SW.step", "7.51 -2.28 -11.84", "0 0 90"),
}


def write_fp(name, text):
    """Write a footprint with its MODELS entry as its only 3D model (any other, e.g. KiCad's stock one, removed)."""
    text = re.sub(r'\n\t\(model .*?\n\t\)', '', text, flags=re.S).rstrip()
    assert text.endswith(")"), name
    f, off, rot = MODELS[name]
    text = (text[:-1].rstrip("\n") + f'\n\t(model "${{KIPRJMOD}}/lib/3d/{f}"\n\t\t(offset\n\t\t\t(xyz {off})\n\t\t)'
            '\n\t\t(scale\n\t\t\t(xyz 1 1 1)\n\t\t)' f'\n\t\t(rotate\n\t\t\t(xyz {rot})\n\t\t)\n\t)\n)\n')
    open(os.path.join(OUT, name + ".kicad_mod"), "w").write(text)


def pad_round(num, x, y, size=2.4, drill=1.5):
    return (f'\t(pad "{num}" thru_hole circle\n\t\t(at {x} {y})\n\t\t(size {size} {size})'
            f'\n\t\t(drill {drill})\n\t\t(layers "*.Cu" "*.Mask")\n\t\t(remove_unused_layers no)'
            f'\n\t\t(uuid "{U()}")\n\t)')


def pad_slot(num, x, y, w, h, dw, dh):
    return (f'\t(pad "{num}" thru_hole oval\n\t\t(at {x} {y})\n\t\t(size {w} {h})'
            f'\n\t\t(drill oval {dw} {dh})\n\t\t(layers "*.Cu" "*.Mask")\n\t\t(remove_unused_layers no)'
            f'\n\t\t(uuid "{U()}")\n\t)')


def npth(x, y, d):
    return (f'\t(pad "" np_thru_hole circle\n\t\t(at {x} {y})\n\t\t(size {d} {d})'
            f'\n\t\t(drill {d})\n\t\t(layers "*.Cu" "*.Mask")\n\t\t(uuid "{U()}")\n\t)')


def line(x1, y1, x2, y2, layer, w):
    return (f'\t(fp_line\n\t\t(start {x1} {y1})\n\t\t(end {x2} {y2})\n\t\t(stroke\n\t\t\t(width {w})'
            f'\n\t\t\t(type solid)\n\t\t)\n\t\t(layer "{layer}")\n\t\t(uuid "{U()}")\n\t)')


def rect(x1, y1, x2, y2, layer, w):
    return "\n".join([line(x1, y1, x2, y1, layer, w), line(x2, y1, x2, y2, layer, w),
                      line(x2, y2, x1, y2, layer, w), line(x1, y2, x1, y1, layer, w)])


def text(kind, val, x, y, layer, hide=False):
    h = "\n\t\t(hide yes)" if hide else ""
    return (f'\t(property "{kind}" "{val}"\n\t\t(at {x} {y} 0)\n\t\t(layer "{layer}"){h}'
            f'\n\t\t(uuid "{U()}")\n\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 1 1)\n\t\t\t\t(thickness 0.15)'
            f'\n\t\t\t)\n\t\t)\n\t)')


def iec_din6():
    name = "IEC_DIN6_DualEarth"
    # body: 21.4 wide (SDF-60J, widest), front face 2.5 mm in front of the earth row
    # (Lumberg: earth pins 2.5 behind the front), 18.9 deep (Lumberg, deepest) [D]
    front = 12.5
    back = round(front - 18.9, 2)
    parts = [
        f'(footprint "{name}"', '\t(version 20241229)', '\t(generator "picod64_gen")',
        '\t(generator_version "9.0")', '\t(layer "F.Cu")',
        '\t(descr "IEC bus socket: 6-pin 240 deg DIN, right angle. Fits generic (earth +/-5), '
        'Lumberg 0105 06 and Same Sky SDF-60J (earth +/-2.5) via slotted earth pads. '
        'PicoD64 HARDWARE.md")',
        '\t(tags "DIN-6 IEC C64 serial 240")',
        text("Reference", "REF**", 0, back - 1.5, "F.SilkS"),
        text("Value", name, 0, front + 1.5, "F.Fab"),
        text("Footprint", "", 0, 0, "F.Fab", True),
        text("Datasheet", "", 0, 0, "F.Fab", True),
        text("Description", "", 0, 0, "F.Fab", True),
        '\t(attr through_hole)',
        rect(-10.7, back, 10.7, front, "F.Fab", 0.1),
        rect(-11.0, round(back - 0.3, 2), 11.0, round(front + 0.3, 2), "F.CrtYd", 0.05),
        line(-10.8, front, 10.8, front, "F.SilkS", 0.12),        # the front face: board edge
        rect(-10.8, back - 0.1, 10.8, back - 0.1, "F.SilkS", 0.12),
    ]
    sig = {"5": (-7.5, 0), "3": (0, 0), "1": (7.5, 0), "4": (-5, -2.5), "2": (5, -2.5), "6": (0, -5)}
    for n, (x, y) in sig.items():
        parts.append(pad_round(n, x, y))
    # earth slots: centre x = +/-3.75, span 2.5..5 -> drill 4.0 x 1.5, pad 4.9 x 2.4
    for x in (-3.75, 3.75):
        parts.append(pad_slot("7", x, 10, 4.9, 2.4, 4.0, 1.5))
    for x in (-7.5, 7.5):                                    # SDF-60J plastic posts
        parts.append(npth(x, 5, 2.4))
    parts.append('\t(embedded_fonts no)')
    parts.append(')')
    os.makedirs(OUT, exist_ok=True)
    write_fp(name, "\n".join(parts))
    print("wrote", name)


def sd_mem2075():
    """
    microSD_GCT_MEM2075 -- GCT MEM2075-00-140-01-A, push-push, 1.40 mm profile, locating posts.
    Chosen 2026-10-25 because EVERY tail is outside the body, for hand soldering: the
    Molex 104031 and Hirose DM3AT both hide a card-detect tail under the shell.
    All numbers from the GCT drawing MEM2075 rev A1, "Recommended PCB Layout, as viewed from
    component side" [D] (datasheets/parts/gct_MEM2075.pdf). Origin: midway between the shell
    pads (they are symmetric: CD - 1.38 and P1 + 5.97, 16.15 apart). Rear (contacts) is -y,
    the card mouth +y.
      signal row: CD, P8 .. P1 at 1.10 pitch, CD -> P1 = 8.80; pads 0.62 x 1.40
      shell pads 1.10 x 2.40 at x = +/-8.075; y = signal row + 2.00, then 9.00 further
      posts Ø0.95, 6.80 apart, right one P1 + 0.81, 0.42 below the lower shell pads
      body 15.05 x 14.50; signal row centre 0.90 behind the body's rear edge (15.40 - 14.50)
      keep-out (component side): 10.20 x 4.05, from the body's left edge, 2.60 in from its rear
    Card detect (pad 9) closes to the shell (pad 10, ground) with a card in [D: switch table].
    """
    name = "microSD_GCT_MEM2075"
    sig_y = -8.15
    cd_x = -6.695
    by0, by1, bx = -7.25, 7.25, 7.525
    parts = [
        f'(footprint "{name}"', '\t(version 20241229)', '\t(generator "picod64_gen")',
        '\t(generator_version "9.0")', '\t(layer "F.Cu")',
        '\t(descr "microSD push-push, GCT MEM2075-00-140-01-A: every tail outside the body '
        '(hand soldering). PicoD64 HARDWARE.md")',
        '\t(tags "microSD TF push-push card detect")',
        text("Reference", "REF**", 0, by0 - 2.6, "F.SilkS"),
        text("Value", name, 0, 0, "F.Fab"),
        text("Footprint", "", 0, 0, "F.Fab", True),
        text("Datasheet", "", 0, 0, "F.Fab", True),
        text("Description", "", 0, 0, "F.Fab", True),
        '\t(attr smd)',
        rect(-bx, by0, bx, by1, "F.Fab", 0.1),
        rect(-8.9, -9.1, 8.9, by1 + 0.25, "F.CrtYd", 0.05),
        # silk: the sides between the shell pads only (clear of copper), and a pin-1 tick
        line(-bx - 0.15, -4.75, -bx - 0.15, 1.45, "F.SilkS", 0.12),
        line(bx + 0.15, -4.75, bx + 0.15, 1.45, "F.SilkS", 0.12),
        # stop short of the mouth: it is flush with the board edge (silk_edge_clearance)
        line(-bx - 0.15, 4.25, -bx - 0.15, by1 - 0.6, "F.SilkS", 0.12),
        line(bx + 0.15, 4.25, bx + 0.15, by1 - 0.6, "F.SilkS", 0.12),
    ]
    xs = {"9": cd_x}
    for n in range(1, 9):
        xs[str(n)] = round(cd_x + 8.8 - (n - 1) * 1.1, 3)
    for n, x in xs.items():
        parts.append(f'\t(pad "{n}" smd rect\n\t\t(at {x} {sig_y})\n\t\t(size 0.62 1.4)'
                     f'\n\t\t(layers "F.Cu" "F.Paste" "F.Mask")\n\t\t(uuid "{U()}")\n\t)')
    p1 = xs["1"]
    parts.append(line(p1, sig_y - 1.0, p1, sig_y - 1.5, "F.SilkS", 0.12))
    for x in (-8.075, 8.075):
        for y in (round(sig_y + 2.0, 3), round(sig_y + 11.0, 3)):
            parts.append(f'\t(pad "10" smd rect\n\t\t(at {x} {y})\n\t\t(size 1.1 2.4)'
                         f'\n\t\t(layers "F.Cu" "F.Paste" "F.Mask")\n\t\t(uuid "{U()}")\n\t)')
    post_y = round(sig_y + 11.42, 3)
    for x in (round(p1 + 0.81, 3), round(p1 + 0.81 - 6.8, 3)):
        parts.append(npth(x, post_y, 0.95))
    kx0, ky0 = -bx, by0 + 2.6
    kx1, ky1 = round(-bx + 10.2, 3), round(by0 + 2.6 + 4.05, 3)
    parts.append(
        f'\t(zone\n\t\t(net 0)\n\t\t(net_name "")\n\t\t(layer "F.Cu")\n\t\t(uuid "{U()}")'
        f'\n\t\t(hatch edge 0.5)\n\t\t(connect_pads\n\t\t\t(clearance 0)\n\t\t)\n\t\t(min_thickness 0.25)'
        f'\n\t\t(filled_areas_thickness no)\n\t\t(keepout\n\t\t\t(tracks not_allowed)\n\t\t\t(vias not_allowed)'
        f'\n\t\t\t(pads not_allowed)\n\t\t\t(copperpour not_allowed)\n\t\t\t(footprints allowed)\n\t\t)'
        f'\n\t\t(placement\n\t\t\t(enabled no)\n\t\t\t(sheetname "")\n\t\t)\n\t\t(fill\n\t\t\t(thermal_gap 0.5)'
        f'\n\t\t\t(thermal_bridge_width 0.5)\n\t\t)\n\t\t(polygon\n\t\t\t(pts\n\t\t\t\t(xy {kx0} {ky0}) (xy {kx1} {ky0})'
        f' (xy {kx1} {ky1}) (xy {kx0} {ky1})\n\t\t\t)\n\t\t)\n\t)')
    parts.append('\t(embedded_fonts no)')
    parts.append(')')
    write_fp(name, "\n".join(parts))
    print("wrote", name)


def ec11e_alps():
    """
    EC11E_Switch_Alps: KiCad's RotaryEncoder_Alps_EC11E-Switch_Vertical_H20mm with the two
    mounting slots moved to the Alps drawing (AUDIT A5/C4, 2026-10-26). Alps EC11E catalogue
    (Update 2510) Drawing No.2, "Mounting Hole Dimensions, viewed from mounting side": slots
    1.5 (+0.1) wide, 2.6 (+0.1) long, **12.5 +/-0.05 over their outer edges** -> centres 11.0
    apart. KiCad has them 11.2 apart (y -3.1 / 8.1 about C at 2.5). Everything else matches the
    drawing: A-C-B 2.5 pitch 7.5 from the shaft centre, D-E (S2/S1) 5 apart 7.0 the other side,
    5 x Ø1.0 holes. Slot drill 2.8 x 1.5 kept (the +0.1 tolerance on 2.6 x 1.5).
    """
    src = "/usr/share/kicad/footprints/Rotary_Encoder.pretty/RotaryEncoder_Alps_EC11E-Switch_Vertical_H20mm.kicad_mod"
    s = open(src).read()
    for a, b in (("(at 7.5 -3.1)", "(at 7.5 -3)"), ("(at 7.5 8.1)", "(at 7.5 8)")):
        assert s.count(a) == 1, a
        s = s.replace(a, b)
    # JLCPCB: a plated slot's length should be >= 2 x its width (AUDIT E1): 2.8 x 1.5 -> 3.0 x 1.5
    assert s.count("(drill oval 2.8 1.5)") == 2 and s.count("(size 3.2 2)") == 2
    s = s.replace("(drill oval 2.8 1.5)", "(drill oval 3 1.5)").replace("(size 3.2 2)", "(size 3.4 2)")
    s = s.replace('(footprint "RotaryEncoder_Alps_EC11E-Switch_Vertical_H20mm"', '(footprint "EC11E_Switch_Alps"', 1)
    write_fp("EC11E_Switch_Alps", s)
    print("wrote EC11E_Switch_Alps")


iec_din6()
sd_mem2075()
ec11e_alps()
