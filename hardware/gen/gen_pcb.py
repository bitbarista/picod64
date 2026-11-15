#!/usr/bin/env python3
"""
Generate the PicoD64 PCB floorplan -- outline, placement, nets, zones (KiCad 9).
Emits an UNROUTED board; gen/board.sh routes it with FREEROUTING (never KRT for
signals), finishes the GND copper and runs DRC. Method and finishing scripts:
an earlier board's generator (gen_pcb.py, board.sh).

Nets come from KiCad's OWN netlist export of the schematic (check_sch.export),
never from a second hand-kept table, so the board cannot drift from the sheet.

Coordinates: board top-left at (X0, Y0), Y down.
    TOP edge    (y = Y0)     = the REAR of the device: IEC sockets and USB
    BOTTOM edge (y = Y0 + L) = the FRONT, facing the user: microSD slot

THE LAYOUT IS FOR THE PERSON USING IT (HARDWARE.md, 2026-10-25):
  * every cable at the back -- both IEC sockets AND the Pico's micro-USB on the
    rear edge, so nothing trails across the desk in front;
  * the user-facing parts at the front: the OLED front-left, the encoder knob
    front-right where a right hand falls, the PWR/ACT LEDs between them, and the
    microSD slot in the front edge, the card pushed straight in;
  * the only tall parts (IEC sockets, 21.4 mm) are at the back, so the case can
    step DOWN at the front: the OLED sits just under a low front lid, and the
    encoder shaft stands well clear of it for a proper knob.
"""
import math
import os
import re
import sys
import uuid

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_sch  # noqa: E402  -- reuse its netlist exporter and parser

HW = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FPDIR = "/usr/share/kicad/footprints"
PROJECT = "picod64"
OUT = os.path.join(HW, f"{PROJECT}.kicad_pcb")
NETFILE = os.path.join(HW, f"{PROJECT}.net")

BOARD_NAME = "PicoD64"
SILK_NAME = "PicoD64"      # rev 1 (ordered 2026-10-30) said "FloppyFree"; rev 1.1 = the rename, silkscreen only
BOARD_REV = "1.1"
BOARD_REV_DATE = "2026-11"     # set BY HAND when a revision is cut, never from the clock

# ------------------------------------------------------------------ outline
# 100 x 85: THROUGH-HOLE parts (2026-10-25) need more room than the SMD rev; still
# inside JLCPCB's 100 x 100 mm price step [A: check the current rule when ordering].
BOARD_W = 100.0
BOARD_L = 85.0
X0, Y0 = 100.0, 100.0
CORNER_R = 3.0               # rounded corners: the case's inner corners can be radiused to match


def P(x, y):                 # board-relative mm -> absolute
    return (round(X0 + x, 3), round(Y0 + y, 3))


# ---------------------------------------------------------------- placement
# REAR EDGE. IEC sockets (IEC_DIN6_DualEarth, origin = pin 3, front at local +12.5) turned
# 180 to face out of the rear edge, front face 0.5 mm proud into the case wall's opening.
IEC_PROUD = 0.5
IEC_Y = 12.5 - IEC_PROUD
J1_X, J2_X = 19.0, 43.0      # 24 mm apart: bodies 21.4 wide (SDF-60J, widest) + 2.6 gap
# rev 1: +4.0, clear of the rear-left corner screw H1 (2026-10-29: screws in the corners)
# Pico 2 front-to-back on the right, USB at the rear edge (13.2 mm from the
# side edge for the GND escape vias; USB 1.8 mm proud into the case wall).
# rev 1: 5.8 further in, clear of the rear-right corner screw H2 (courtyards 69.22-92.38 vs 92.71)
PICO_X = 80.8
USB_PROUD = 1.8
PICO_Y = round(-USB_PROUD + 27.35, 3)
# Piezo (DNP, 9.5 mm tall): in the TALL rear section, right of J2.
BZ_X, BZ_Y = 59.0, 10.5   # pin 1; TDK PS1240P02BT pins 5.0 apart (rev 1: between J2 and the Pico)

# IEC interface grid, in front of the sockets: one column per bus line, top to bottom the
# divider's bus-side 10k, its ground-side 15k, and the 1k bus pull-up; then
# the two outputs (2N3904 + 680R base + 10k pull-down). Axial 7.62 mm resistors lying flat.
# Column pitch 10.8 (a resistor's courtyard is 9.7); row pitch 4.6 leaves each row's
# designator room above it.
GX = [6.0, 16.8, 27.6, 38.4, 49.2, 60.0]   # rev 1: 3 left, clear of the Pico (H1 left the left edge)
# rev 1: a strip between the sockets and the grid holds J5
STRIP_Y = 20.4
GY = [24.0, 28.6, 33.2, 37.8]   # 24.0: room for J1/J2 labels below the sockets AND row 1 labels above it

# FRONT. OLED: 1.3" SH1106 I2C module (~36 x 34 [C, sellers; MEASURE one]) PLUGGED into a
# 4-pin female socket J4 (rotation 90: pins run +X): it goes on last, sits ~8.5 mm up [A],
# close under the lid window, and the low parts beneath it are fitted (and labelled) first.
# OLED: three sellers' drawings (2026-10-26) [C]: the 4-pin I2C module (ours) 35.50 +/-0.5 x
# 33.70 +/-0.5, glass 34.50 x 23.00, holes 31.50 x 29.70, header centred; and two 7-pin SPI modules
# (35.40 x 33.50, header 2.00 from the edge; 35 x 31, header ~1.7). Referenced to the HEADER ROW,
# which the socket fixes. Case: AUDIT D2.
OLED_W, OLED_D = 35.5, 33.7
J4_X, J4_Y = 20.19, 41.0
OLED_X0 = round(J4_X + 3.81 - OLED_W / 2, 2)  # module centred on its header [C: both drawings]
OLED_Y0 = round(J4_Y - 2.0, 2)                # header 2.00 in from the module's rear edge [C: drawing 1]
# Encoder, front-right of the display, under a right hand. Shaft = pin A + (7.5, 2.5).
# Encoder in the FRONT-RIGHT corner, as far right as the board allows (2026-10-26: "the encoder
# really could benefit from going to the right as far as practically possible"; option B: the microSD
# moved to the front-left under the display). Body x 75-92.5: 0.5 clear of H2, 6.8 below the Pico.
SHAFT_X, SHAFT_Y = 84.0, 66.0
SW1_X, SW1_Y = SHAFT_X - 7.5, SHAFT_Y - 2.5
D1_X, D1_Y = 45.5, 45.0      # PWR
D2_X, D2_Y = 45.5, 51.0      # ACT
# microSD from the FRONT edge: GCT MEM2075, mouth (+y in the footprint) flush with the edge;
# a latched card stands 2.5 mm proud of it, 1.4 mm when pushed to release [D: GCT drawing].
SD_X = 24.0     # front-left, between the OLED standoffs H7/H8 (~4 mm each side), under the module's front edge
# the card is NOT centred on the footprint: on the GCT top view it sits 1.07 mm toward the CD
# side (0.9 in from the body's left edge, 3.1 from its right) [A: scaled off the drawing, no
# dimension given]. The case slot centres on the card (AUDIT D3).
SD_CARD_DX = -1.07
SD_Y = round(BOARD_L - 7.25, 2)

RTHT = "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal"
CDISC = "Capacitor_THT:C_Disc_D3.0mm_W2.0mm_P2.50mm"
CPOL = "Capacitor_THT:CP_Radial_D5.0mm_P2.00mm"
TO92 = "Package_TO_SOT_THT:TO-92_Inline_Wide"   # 2.54 pitch: room for GND spokes, easier to solder
HDR3 = "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical"
HOLE25 = "MountingHole:MountingHole_2.7mm_M2.5"
SIP5 = "Resistor_THT:R_Array_SIP5"
SIP9 = "Resistor_THT:R_Array_SIP9"

PLACE = [  # ref, footprint, x, y, rotation  (x, y board-relative)
    ("J1", "picod64:IEC_DIN6_DualEarth", J1_X, IEC_Y, 180),
    ("J2", "picod64:IEC_DIN6_DualEarth", J2_X, IEC_Y, 180),
    ("A1", "picod64:RaspberryPi_Pico_SMD_HandSolder_Pin1Rect", PICO_X, PICO_Y, 0),
    ("BZ1", "Buzzer_Beeper:Buzzer_TDK_PS1240P02BT_D12.2mm_H6.5mm", BZ_X, BZ_Y, 0),
    ("R14", RTHT, 59.0, 41.6, 0),                # piezo series, between Q2 and R12, clear of the Pico's GND escape vias (rev 1)
    ("J5", HDR3, 58.0, STRIP_Y, 90),              # debug UART, in the strip, toward GP0/GP1
    # bus test pads (rev 1): one row between D4 and the OLED socket, by the bus parts; lettered, not
    # designated. (A 2 x 3 block beside the Pico walled off its pin column: IEC_DATA would not route.)
    ("TP1", "TestPoint:TestPoint_THTPad_D2.0mm_Drill1.0mm", 31.0, 37.0, 0),
    ("TP2", "TestPoint:TestPoint_THTPad_D2.0mm_Drill1.0mm", 34.1, 37.0, 0),
    ("TP3", "TestPoint:TestPoint_THTPad_D2.0mm_Drill1.0mm", 37.2, 37.0, 0),
    ("TP4", "TestPoint:TestPoint_THTPad_D2.0mm_Drill1.0mm", 40.3, 37.0, 0),
    ("TP5", "TestPoint:TestPoint_THTPad_D2.0mm_Drill1.0mm", 43.4, 37.0, 0),
    ("TP6", "TestPoint:TestPoint_THTPad_D2.0mm_Drill1.0mm", 46.5, 37.0, 0),
    # spare GPIO header (rev 1, DNP), the left edge below H1
    ("J7", "Connector_PinHeader_2.54mm:PinHeader_1x05_P2.54mm_Vertical", 2.6, 11.0, 0),   # well clear of the H1 post (its gusset runs to the rear wall)
    # interface grid: the four bus-side 6k8 resistors in a row, the ground-side 10k network RN2
    # and the 1k pull-up network RN3 below them, then the two outputs
    ("R1", RTHT, GX[0], GY[0], 0), ("R2", RTHT, GX[1], GY[0], 0),
    ("R3", RTHT, GX[2], GY[0], 0), ("R4", RTHT, GX[3], GY[0], 0),
    ("RN2", SIP5, GX[0] + 2.0, GY[1] + 0.4, 0),
    ("R18", "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P2.54mm_Vertical", 2.6, 27.8, 270),   # rev 1: SRQ 10k, standing, left edge by R15
    # SRQ (rev 1): R15 divider; Q3 + R16/R17 output, DNP. Rows 2-3, columns 0-1
    # (clear of the OLED standoff H5 at (8.8, 41.5) and socket J4 below them: courtyards checked)
    ("R15", RTHT, GX[0], 32.5, 0), ("R16", RTHT, GX[1], 32.5, 0),
    ("R17", RTHT, GX[0], 36.4, 0), ("Q3", TO92, GX[1] + 1.23, 37.0, 0),
    ("RN3", SIP5, GX[2] + 2.0, GY[1] + 0.4, 0),
    # D4: VBUS -> RN3 common, so the C64 cannot power the Pico through RN3 (AUDIT B7).
    # K under RN3 pin 1, A toward the Pico, on the free grid row below
    ("D4", "Diode_THT:D_DO-41_SOD81_P10.16mm_Horizontal", GX[2] + 4.0, GY[2], 0),   # rev 1: +2, room for R16's label
    ("R5", RTHT, GX[4], GY[0], 0), ("R6", RTHT, GX[4], GY[1], 0), ("Q1", TO92, GX[4] + 1.23, GY[3] - 0.6, 0),
    ("R7", RTHT, GX[5], GY[0], 0), ("R8", RTHT, GX[5], GY[1], 0), ("Q2", TO92, GX[5] + 1.23, GY[3] - 0.6, 0),
    # OLED socket; its pin-order jumpers at the LEFT EDGE beside the module, reachable with it on
    ("J4", "Connector_PinSocket_2.54mm:PinSocket_1x04_P2.54mm_Vertical", J4_X, J4_Y, 90),
    ("JP1", HDR3, 2.6, 44.0, 0),
    ("JP2", HDR3, 2.6, 53.0, 0),
    ("R9", RTHT, 28.0, 45.0, 0),                 # under the module: fitted before it (x 30 -> 28: clear of H6)
    ("R10", RTHT, 28.0, 49.6, 0),
    ("C3", CDISC, 28.0, 54.2, 0),
    ("D1", "LED_THT:LED_D3.0mm", D1_X, D1_Y, 0),
    ("D2", "LED_THT:LED_D3.0mm", D2_X, D2_Y, 0),
    ("R12", RTHT, 50.0, 45.0, 0),
    ("R13", RTHT, 50.0, 51.0, 0),
    ("SW1", "picod64:EC11E_Switch_Alps", SW1_X, SW1_Y, 0),
    ("R11", RTHT, 76.0, 77.5, 0),                # below the encoder
    ("C4", CDISC, 79.0, 81.6, 0), ("C5", CDISC, 86.0, 81.6, 0),
    # microSD and its pull-ups, between the Pico's foot and the socket
    ("J3", "picod64:microSD_GCT_MEM2075", SD_X, SD_Y, 0),
    ("RN1", SIP9, 48.0, 58.0, 0),                 # 10k x 8 pull-ups: SD lines + encoder A/B; between them
    ("C1", CPOL, 37.0, 78.0, 0),                  # 11 mm tall: OUTSIDE the OLED footprint (its board is 11 up)
    ("C2", CDISC, 21.5, 66.5, 0),                 # right behind the socket's VDD pin (P4 at x 22.8), under the module
    # external 5 V (DNP): header at the left edge, diode under the module
    ("J6", "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical", 2.6, 63.0, 0),
    ("JP3", "Jumper:SolderJumper-2_P1.3mm_Bridged_RoundedPad1.0x1.5mm", 2.6, 70.0, 270),   # closed; cut to measure. Pad 1 up, toward J6
    # spare GP28 (rev 1): a pad right of the Pico, by its pin 34 (a trace across the board never routed)
    ("TP7", "TestPoint:TestPoint_THTPad_D2.0mm_Drill1.0mm", 95.0, 18.0, 0),
    ("D3", "Diode_THT:D_DO-41_SOD81_P10.16mm_Horizontal", 7.5, 61.5, 0),   # DNP, under the module (the SD took its spot)
    # case screws
    # rev 1: all four in the corners (2026-10-29; AUDIT D16)
    ("H1", "MountingHole:MountingHole_3.2mm_M3", 4.0, 4.0, 0),
    ("H2", "MountingHole:MountingHole_3.2mm_M3", 96.2, 4.0, 0),
    ("H3", "MountingHole:MountingHole_3.2mm_M3", 4.0, 81.0, 0),
    ("H4", "MountingHole:MountingHole_3.2mm_M3", 96.5, 81.5, 0),
    # OLED module support (AUDIT D7): its holes are 30.40 x 28.50, 2.50 in from its edges, the header row
    # 2.00-2.10 from its top edge [D: module spec WP013..L102B-01A; C: ProtoSupplies, seller drawing], so
    # relative to J4: x = header centre +/-15.20, y = header +0.5 / +29.0. M2.5 x 11 nylon standoffs
    # (socket 8.5 + header spacer 2.5); the module's Ø3.0-3.1 holes take the spread between sellers.
    ("H5", HOLE25, J4_X + 3.81 - 15.2, J4_Y + 0.5, 0), ("H6", HOLE25, J4_X + 3.81 + 15.2, J4_Y + 0.5, 0),
    ("H7", HOLE25, J4_X + 3.81 - 15.2, J4_Y + 29.0, 0), ("H8", HOLE25, J4_X + 3.81 + 15.2, J4_Y + 29.0, 0),
]
BACK = set()


def U():
    return str(uuid.uuid4())


def grab(t, i):
    d, ins, esc = 0, False, False
    for j in range(i, len(t)):
        c = t[j]
        if esc:
            esc = False
            continue
        if c == "\\":
            esc = True
            continue
        if c == '"':
            ins = not ins
            continue
        if ins:
            continue
        if c == "(":
            d += 1
        elif c == ")":
            d -= 1
            if d == 0:
                return t[i:j + 1]
    raise ValueError("unbalanced")


def load_fp(libref):
    lib, mod = libref.split(":", 1)
    for root in (os.path.join(HW, "lib"), FPDIR):
        p = f"{root}/{lib}.pretty/{mod}.kicad_mod"
        if os.path.exists(p):
            return open(p).read()
    raise SystemExit(f"footprint not found: {libref}")


# ---------------------------------------------------------------- nets, values
check_sch.export(NETFILE)
nets = check_sch.parse(NETFILE)
netof = {}
for name, pins in nets.items():
    for p in pins:
        ref, num = p.split(".", 1)
        netof[(ref, num)] = name
netnames = sorted({n for n in netof.values()})
netcode = {n: i + 1 for i, n in enumerate(netnames)}
_nt = open(NETFILE).read()
VALUES = {m.group(1): m.group(2) for m in
          re.finditer(r'\(comp \(ref "([^"]+)"\)\s*\(value "([^"]*)"\)', _nt)}
DNP = set(re.findall(r'\(comp \(ref "([^"]+)"\)(?:(?!\(comp ).)*?\(property \(name "dnp"\)', _nt, re.S))
_placed = {p[0] for p in PLACE}
_sheet = set(VALUES)
if _placed != _sheet:
    raise SystemExit(f"placement/schematic mismatch: unplaced {sorted(_sheet - _placed)}, "
                     f"unknown {sorted(_placed - _sheet)}")


def esc(s):
    return s.replace('"', '\\"')


def place(ref, libref, x, y, rot):
    x, y = P(x, y)
    t = load_fp(libref)
    body = grab(t, t.index("(footprint"))
    attr = re.search(r"\(attr [^)]*\)", body)
    out = [f'\t(footprint "{libref}"', '\t\t(layer "F.Cu")', f'\t\t(uuid "{U()}")',
           f'\t\t(at {x} {y}{" " + str(rot) if rot else ""})']
    if attr:
        a = attr.group(0)
        if ref in DNP and "dnp" not in a:
            a = a[:-1] + " dnp)"
        out.append("\t\t" + a)
    # a bridged solder jumper's copper bar joins its pads ON PURPOSE: keep its net-tie
    # declaration, or DRC calls the bar a short (rev 1, JP3)
    nt = re.search(r'\(net_tie_pad_groups "[^"]*"\)', body)
    if nt:
        out.append("\t\t" + nt.group(0))
    out.append(f'\t\t(property "Reference" "{ref}"\n\t\t\t(at 0 0 0)\n\t\t\t(layer "F.SilkS")'
               f'\n\t\t\t(hide yes)\n\t\t\t(uuid "{U()}")\n\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t'
               '(size 1 1)\n\t\t\t\t\t(thickness 0.15)\n\t\t\t\t)\n\t\t\t)\n\t\t)')
    out.append(f'\t\t(property "Value" "{esc(VALUES[ref])}"\n\t\t\t(at 0 0 0)\n\t\t\t(layer "F.Fab")'
               f'\n\t\t\t(hide yes)\n\t\t\t(uuid "{U()}")\n\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t'
               '(size 1 1)\n\t\t\t\t\t(thickness 0.15)\n\t\t\t\t)\n\t\t\t)\n\t\t)')
    inner = body[body.index('"'):]
    # ⚠ FRESH UUIDs. Some library footprints carry fixed uuids on their pads and graphics
    # (TestPoint_*: 8); every copy then shared them, and KiCad's DRC report named TP3's
    # IEC_DATA pad for TP6's GND one -- board.sh read IEC_DATA as open for 8 routing
    # attempts (rev 1, 2026-10-29)
    inner = re.sub(r'\(uuid "[^"]+"\)', lambda m: f'(uuid "{U()}")', inner)
    for pm in re.finditer(r'\n\t\((?:fp_line|fp_circle|fp_arc|fp_poly|fp_rect|fp_text|zone)\s', inner):
        g = grab(inner, pm.start() + 2)
        if g.startswith("(fp_text") and ("reference" in g[:20] or "value" in g[:20]):
            continue
        if g.startswith("(zone"):
            # ⚠ Zones inside a footprint are stored in ABSOLUTE board coordinates
            c, s_ = math.cos(math.radians(rot)), math.sin(math.radians(rot))

            def xf(m):
                px, py = float(m.group(1)), float(m.group(2))
                return f"(xy {round(x + px * c + py * s_, 4)} {round(y - px * s_ + py * c, 4)})"
            g = re.sub(r'\(xy ([-\d.]+) ([-\d.]+)\)', xf, g)
        if g.startswith("(fp_text") and rot:
            g = re.sub(r'\(at ([-\d.]+) ([-\d.]+)(?: [-\d.]+)?\)', r'(at \1 \2 0)', g, count=1)
        out.append("\t\t" + g.replace("\n", "\n\t\t"))
    used = set()
    for pm in re.finditer(r'\n\t\(pad\s', inner):
        pad = grab(inner, pm.start() + 2)
        num = re.match(r'\(pad "([^"]*)"', pad).group(1)
        if rot:
            def addrot(m):
                a = (float(m.group(3) or 0) + rot) % 360
                return f"(at {m.group(1)} {m.group(2)} {a:g})"
            pad = re.sub(r'\(at ([-\d.]+) ([-\d.]+)(?: ([-\d.]+))?\)', addrot, pad, count=1)
        net = netof.get((ref, num))
        if net:
            pad = pad[:-1] + f'\n\t\t(net {netcode[net]} "{esc(net)}")\n\t)'
            used.add(num)
        out.append("\t\t" + pad.replace("\n", "\n\t\t"))
    for pm in re.finditer(r'\n\t\(model\s', inner):
        out.append("\t\t" + grab(inner, pm.start() + 2).replace("\n", "\n\t\t"))
    out.append("\t)")
    missing = {num for (r, num) in netof if r == ref} - used
    if missing:
        raise SystemExit(f"{ref}: netlist pins {sorted(missing)} have no pad in {libref}")
    return "\n".join(out)


def gr_line(a, b, layer="Edge.Cuts", w=0.1):
    return (f'\t(gr_line\n\t\t(start {a[0]} {a[1]})\n\t\t(end {b[0]} {b[1]})'
            f'\n\t\t(stroke\n\t\t\t(width {w})\n\t\t\t(type default)\n\t\t)\n\t\t(layer "{layer}")'
            f'\n\t\t(uuid "{U()}")\n\t)')


def gr_arc(start, mid, end, layer="Edge.Cuts", w=0.1):
    return (f'\t(gr_arc\n\t\t(start {start[0]} {start[1]})\n\t\t(mid {mid[0]} {mid[1]})\n\t\t(end {end[0]} {end[1]})'
            f'\n\t\t(stroke\n\t\t\t(width {w})\n\t\t\t(type default)\n\t\t)\n\t\t(layer "{layer}")'
            f'\n\t\t(uuid "{U()}")\n\t)')


def gr_rect(x0, y0, x1, y1, layer, w=0.12):
    a, b, c, d = P(x0, y0), P(x1, y0), P(x1, y1), P(x0, y1)
    return "\n".join(gr_line(p, q, layer, w) for p, q in ((a, b), (b, c), (c, d), (d, a)))


def gr_circle(cx, cy, r, layer, w=0.12):
    c = P(cx, cy)
    return (f'\t(gr_circle\n\t\t(center {c[0]} {c[1]})\n\t\t(end {round(c[0] + r, 3)} {c[1]})'
            f'\n\t\t(stroke\n\t\t\t(width {w})\n\t\t\t(type default)\n\t\t)\n\t\t(fill no)\n\t\t(layer "{layer}")'
            f'\n\t\t(uuid "{U()}")\n\t)')


def gr_text(s, x, y, layer="F.SilkS", size=1.0, just=None):
    c = P(x, y)
    j = f"\n\t\t\t(justify {just})" if just else ""
    return (f'\t(gr_text "{esc(s)}"\n\t\t(at {c[0]} {c[1]} 0)\n\t\t(layer "{layer}")\n\t\t(uuid "{U()}")'
            f'\n\t\t(effects\n\t\t\t(font\n\t\t\t\t(size {size} {size})\n\t\t\t\t(thickness {round(size * 0.15, 3)})'
            f'\n\t\t\t){j}\n\t\t)\n\t)')


def zone(netname, layer, pts, keepout=False, name="", vias_only=False, pour_ok=False):
    pts_s = " ".join(f"(xy {round(a, 3)} {round(b, 3)})" for a, b in pts)
    if keepout:
        body = ('\n\t\t(keepout\n\t\t\t(tracks ' + ('allowed' if vias_only else 'not_allowed') +
                ')\n\t\t\t(vias not_allowed)\n\t\t\t(pads allowed)\n\t\t\t(copperpour ' +
                ('allowed' if pour_ok else 'not_allowed') + ')\n\t\t\t(footprints allowed)\n\t\t)'
                '\n\t\t(placement\n\t\t\t(enabled no)\n\t\t\t(sheetname "/")\n\t\t)')
        net = '\t\t(net 0)\n\t\t(net_name "")'
        lay = '\t\t(layers "F.Cu" "B.Cu")'
    else:
        body = ''
        net = f'\t\t(net {netcode[netname]})\n\t\t(net_name "{netname}")'
        lay = f'\t\t(layer "{layer}")'
    return (f'\t(zone\n{net}\n{lay}\n\t\t(uuid "{U()}")'
            f'{chr(10) + chr(9) * 2 + "(name " + chr(34) + name + chr(34) + ")" if name else ""}'
            '\n\t\t(hatch edge 0.5)\n\t\t(connect_pads\n\t\t\t(clearance 0.3)\n\t\t)'
            '\n\t\t(min_thickness 0.25)\n\t\t(filled_areas_thickness no)' + body +
            '\n\t\t(fill yes\n\t\t\t(thermal_gap 0.5)\n\t\t\t(thermal_bridge_width 0.5)' +
            ('' if keepout else '\n\t\t\t(island_removal_mode 0)') + '\n\t\t)'
            f'\n\t\t(polygon\n\t\t\t(pts\n\t\t\t\t{pts_s}\n\t\t\t)\n\t\t)\n\t)')


# ---------------------------------------------------------------- emit
o = ['(kicad_pcb', '\t(version 20241229)', '\t(generator "pcbnew")', '\t(generator_version "9.0")',
     '\t(general\n\t\t(thickness 1.6)\n\t\t(legacy_teardrops no)\n\t)', '\t(paper "A4")',
     f'\t(title_block\n\t\t(title "{BOARD_NAME}")\n\t\t(date "{BOARD_REV_DATE}")'
     f'\n\t\t(rev "{BOARD_REV}")\n\t\t(comment 1 "Generated by hardware/gen/gen_pcb.py + board.sh")\n\t)',
     '\t(layers', '\t\t(0 "F.Cu" signal)', '\t\t(2 "B.Cu" signal)',
     '\t\t(9 "F.Adhes" user "F.Adhesive")', '\t\t(11 "B.Adhes" user "B.Adhesive")',
     '\t\t(13 "F.Paste" user)', '\t\t(15 "B.Paste" user)',
     '\t\t(5 "F.SilkS" user "F.Silkscreen")', '\t\t(7 "B.SilkS" user "B.Silkscreen")',
     '\t\t(1 "F.Mask" user)', '\t\t(3 "B.Mask" user)',
     '\t\t(17 "Dwgs.User" user "User.Drawings")', '\t\t(19 "Cmts.User" user "User.Comments")',
     '\t\t(25 "Edge.Cuts" user)', '\t\t(27 "Margin" user)',
     '\t\t(31 "F.CrtYd" user "F.Courtyard")', '\t\t(29 "B.CrtYd" user "B.Courtyard")',
     '\t\t(35 "F.Fab" user)', '\t\t(33 "B.Fab" user)', '\t)',
     '\t(setup\n\t\t(pad_to_mask_clearance 0)\n\t\t(allow_soldermask_bridges_in_footprints no)'
     '\n\t\t(tenting front back)\n\t)',
     '\t(net 0 "")']
for n in netnames:
    o.append(f'\t(net {netcode[n]} "{esc(n)}")')

# outline: rounded rectangle
W, L, r = BOARD_W, BOARD_L, CORNER_R
k = r * (1 - 1 / math.sqrt(2))
o += [gr_line(P(r, 0), P(W - r, 0)), gr_line(P(W, r), P(W, L - r)),
      gr_line(P(W - r, L), P(r, L)), gr_line(P(0, L - r), P(0, r)),
      gr_arc(P(W - r, 0), P(W - k, k), P(W, r)), gr_arc(P(W, L - r), P(W - k, L - k), P(W - r, L)),
      gr_arc(P(r, L), P(k, L - k), P(0, L - r)), gr_arc(P(0, r), P(k, k), P(r, 0))]

for ref, libref, x, y, rot in PLACE:
    o.append(place(ref, libref, x, y, rot))

# GND pour, both faces, inset from the edge
inset = 0.4
pour = [P(inset, inset), P(W - inset, inset), P(W - inset, L - inset), P(inset, L - inset)]
for layer in ("F.Cu", "B.Cu"):
    o.append(zone("GND", layer, pour, name=f"GND {layer}"))

# ⚠ NPTH KEEP-OUTS. The GND pour filled to 0.2205 mm of the unplated holes (mounting
# holes, SDF-60J post holes) against the 0.25 mm hole-clearance rule: 16 DRC errors on the
# first routed build (2026-10-25). A no-pour circle, 0.6 mm wider than the hole, round each.
def npth_keepout(cxx, cyy, d, name):
    rr = d / 2 + 0.6
    pts = [P(cxx + rr * math.cos(2 * math.pi * i / 24), cyy + rr * math.sin(2 * math.pi * i / 24)) for i in range(24)]
    return zone(None, None, pts, keepout=True, name=name, vias_only=True, pour_ok=False)


for ref, fp, hx, hy, rot in PLACE:
    if ref.startswith("H"):
        o.append(npth_keepout(hx, hy, 2.7 if "2.7mm" in fp else 3.2, f"{ref}: no pour"))
for jx in (J1_X, J2_X):                       # posts at local (+-7.5, 5), turned 180
    for px in (-7.5, 7.5):
        o.append(npth_keepout(jx - px, IEC_Y - 5.0, 2.4, "IEC post: no pour"))
# Pico 2 test points TP1-TP7 lie bare on its underside, on our board [D: Pico 2 datasheet 3.1, Fig. 5,
# origin bottom-left, USB up; 1.5 x 1.5 mm]. No via or track under them (a tented via's bump would
# hold the module off the board, and TP6 is BOOTSEL); the pour may stay, under the mask
# ("acceptable to void", not required). Audit A2, 2026-10-25.
for tx, ty in ((9.5, 49.8), (11.5, 49.8), (10.5, 46.5), (8, 43), (8, 40.5), (8, 38), (11.5, 30.5)):
    bx_, by_ = PICO_X + tx - 10.5, PICO_Y + 25.5 - ty
    o.append(zone(None, None, [P(bx_ - 1.0, by_ - 1.0), P(bx_ + 1.0, by_ - 1.0), P(bx_ + 1.0, by_ + 1.0),
                               P(bx_ - 1.0, by_ + 1.0)], keepout=True, name="Pico test point", pour_ok=True))
for px in (2.915, -3.885):                    # J3 locating posts, local (x, 3.27), not turned
    o.append(npth_keepout(SD_X + px, SD_Y + 3.27, 0.95, "microSD post: no pour"))

# ---- the parts that are NOT on the board, drawn for the case and the assembler (Dwgs.User):
# the OLED module's outline, the knob, the card. ⚠ All [A] until a module is measured.
o.append(gr_rect(OLED_X0, OLED_Y0, OLED_X0 + OLED_W, OLED_Y0 + OLED_D, "Dwgs.User", 0.15))
o.append(gr_text("OLED module 35.5x33.7 [C]", OLED_X0 + OLED_W / 2, OLED_Y0 + OLED_D - 2,
                 "Dwgs.User", 1.0))
o.append(gr_circle(SHAFT_X, SHAFT_Y, 7.5, "Dwgs.User", 0.15))           # a 15 mm knob [A]
o.append(gr_rect(SD_X + SD_CARD_DX - 5.5, BOARD_L - 12.5, SD_X + SD_CARD_DX + 5.5, BOARD_L + 2.5, "Dwgs.User", 0.15))  # card, latched [D: GCT]

# ---- silkscreen for the user (the assembler first, then whoever opens it later)
# 1.0 mm: JLCPCB "Characters height less than 40 mil (1.0mm) will be unidentifiable" (AUDIT E1)
TEXTS = [
    (f"{SILK_NAME} rev {BOARD_REV}  {BOARD_REV_DATE}", 55.0, 81.0, 1.0, None),
    ("PWR", D1_X - 2.0, D1_Y, 1.0, "right"), ("ACT", D2_X - 2.0, D2_Y, 1.0, "right"),
    ("OLED", 2.6, 40.6, 1.0, None),
    ("G", 4.9, 44.0, 1.0, None), ("V", 4.9, 49.08, 1.0, None),
    ("G", 4.9, 53.0, 1.0, None), ("V", 4.9, 58.08, 1.0, None),
    ("5V", 4.9, 63.0, 1.0, None),
    ("GP28", 95.0, 20.4, 1.0, None),                  # TP7 (rev 1)
    # rev 1 test pads: TP1-6 ATN CLK DATA RESET SRQ GND
    ("A", 31.0, 39.0, 1.0, None), ("C", 34.1, 39.0, 1.0, None), ("D", 37.2, 39.0, 1.0, None), ("R", 40.3, 39.0, 1.0, None), ("S", 43.4, 39.0, 1.0, None), ("G", 46.5, 39.0, 1.0, None),
]
for s, tx, ty, sz, j in TEXTS:
    o.append(gr_text(s, tx, ty, "F.SilkS", sz, j))

# board text: keep router vias off it
for s, tx, ty, sz, j in TEXTS[:1]:
    a, b = P(tx - 13.0, ty - 1.0), P(tx + 13.0, ty + 1.0)
    o.append(zone(None, None, [(a[0], a[1]), (b[0], a[1]), (b[0], b[1]), (a[0], b[1])],
                  keepout=True, name="board text: vias out", vias_only=True, pour_ok=True))

# ⚠ GND ESCAPES FOR THE PICO, BEFORE ROUTING (from an earlier board): every GND
# castellation gets a locked stub out past its pad and a via, so freerouting routes AROUND a
# path to ground instead of walling the pad in.


def emit_track(a, b, w, layer, net):
    return (f'\t(segment\n\t\t(start {a[0]} {a[1]})\n\t\t(end {b[0]} {b[1]})'
            f'\n\t\t(width {w})\n\t\t(locked yes)\n\t\t(layer "{layer}")\n\t\t(net {netcode[net]})'
            f'\n\t\t(uuid "{U()}")\n\t)')


def emit_via(a, net):
    return (f'\t(via\n\t\t(at {a[0]} {a[1]})\n\t\t(size 0.6)\n\t\t(drill 0.3)'
            f'\n\t\t(layers "F.Cu" "B.Cu")\n\t\t(locked yes)\n\t\t(net {netcode[net]})\n\t\t(uuid "{U()}")\n\t)')


ESCAPE = 11.29 + 0.25 + 0.3 + 0.35
for pin in (3, 8, 13, 18, 23, 28, 33, 38):
    left = pin <= 20
    kk = (pin - 1) if left else (40 - pin)
    py = PICO_Y - 24.13 + 2.54 * kk
    px = PICO_X - 9.69 if left else PICO_X + 9.69
    vx = PICO_X - ESCAPE if left else PICO_X + ESCAPE
    o.append(emit_track(P(px, py), P(vx, py), 0.4, "F.Cu", "GND"))
    o.append(emit_via(P(vx, py), "GND"))

o.append('\t(embedded_fonts no)')
o.append(')')
open(OUT, "w").write("\n".join(o) + "\n")

# Trim the Pico's silk where the module meets the rear edge (USB 1.8 mm proud).
import pcbnew  # noqa: E402
_b = pcbnew.LoadBoard(OUT)
_pico = [f for f in _b.GetFootprints() if f.GetReference() == "A1"][0]
_lim = pcbnew.FromMM(Y0 + 0.25)
for _g in list(_pico.GraphicalItems()):
    if _g.GetLayer() != pcbnew.F_SilkS or _g.GetBoundingBox().GetTop() >= _lim:
        continue
    if _g.GetBoundingBox().GetBottom() <= _lim or _g.GetClass() != "PCB_SHAPE" or \
            _g.GetShape() != pcbnew.SHAPE_T_SEGMENT or _g.GetStart().x != _g.GetEnd().x:
        _pico.Remove(_g)
        continue
    _a, _e = _g.GetStart(), _g.GetEnd()
    if _a.y < _lim: _g.SetStart(pcbnew.VECTOR2I(_a.x, _lim))
    if _e.y < _lim: _g.SetEnd(pcbnew.VECTOR2I(_e.x, _lim))
pcbnew.SaveBoard(OUT, _b)

# the case's numbers: gen/gen_case_dims.py (reads this file's placement constants)
print(f"wrote {OUT}: {len(PLACE)} footprints, {len(netnames)} nets, {BOARD_W} x {BOARD_L} mm")
