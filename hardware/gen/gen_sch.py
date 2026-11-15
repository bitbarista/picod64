#!/usr/bin/env python3
"""
Generate the PicoD64 schematic (KiCad 9).   python3 hardware/gen/gen_sch.py
(then sh hardware/gen/check_sch.sh)

Method and framework from an earlier board's generator:
Python emits the .kicad_sch directly; kicad-cli does ERC and netlist export;
connectivity is by LOCAL LABEL at the end of a short stub on every pin, so the
sheet reads like a pin table, and check_sch.py compares KiCad's netlist with the
design table below. Format traps carried over from there:
  * a junction only counts on a wire ENDPOINT (no junctions are used here)
  * symbol sub-units keep BARE names in lib_symbols

Design source: HARDWARE.md (decisions and parts of 2026-10-25). Every pin
assignment below cites its source; [A] marks what is not yet verified.
"""
import math
import os
import re
import sys
import uuid

BOARD_NAME = "PicoD64"
BOARD_REV = "1.1"
BOARD_REV_DATE = "2026-11"     # set BY HAND when a revision is cut, never from the clock

SYMDIR = "/usr/share/kicad/symbols"
HW = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECT = "picod64"
OUT = os.path.join(HW, f"{PROJECT}.kicad_sch")
LIBNAME = "picod64"
LIBFILE = os.path.join(HW, "lib", f"{LIBNAME}.kicad_sym")


# Deterministic UUIDs: the same design writes the same file, so a re-run (check_sch.sh) changes nothing.
# Was uuid4(), which rewrote every UUID on every run (AUDIT R22). Symbols and their pins are keyed by
# reference/unit/pin, so adding a part does not renumber the others; wires, labels, etc. by emit order.
_NS = uuid.UUID("5d0c3a4e-8f1b-4c2a-9e6d-1f0a2b3c4d5e")   # fixed, arbitrary: PicoD64's schematic namespace
_seq = [0]


def U(key=None):
    if key is None:
        _seq[0] += 1
        key = f"seq {_seq[0]}"
    return str(uuid.uuid5(_NS, key))


# ------------------------------------------------------------------ symbols
def grab(text, start):
    d, in_str, esc = 0, False, False
    for j in range(start, len(text)):
        c = text[j]
        if esc:
            esc = False
            continue
        if c == "\\":
            esc = True
            continue
        if c == '"':
            in_str = not in_str
            continue
        if in_str:
            continue
        if c == "(":
            d += 1
        elif c == ")":
            d -= 1
            if d == 0:
                return text[start:j + 1]
    raise ValueError("unbalanced s-expression")


_cache = {}


def libtext(lib):
    if lib not in _cache:
        path = LIBFILE if lib == LIBNAME else f"{SYMDIR}/{lib}.kicad_sym"
        _cache[lib] = open(path).read()
    return _cache[lib]


def symbol_block(lib, name):
    t = libtext(lib)
    m = re.search(r'\(symbol "%s"\s' % re.escape(name), t)
    if not m:
        raise KeyError(f"{lib}:{name}")
    return grab(t, m.start())


def resolve(lib, name):
    """lib_symbols entry for lib:name, flattening (extends ...)."""
    blk = symbol_block(lib, name)
    ext = re.search(r'\(extends "([^"]+)"\)', blk)
    if ext:
        parent = ext.group(1)
        pblk = symbol_block(lib, parent)
        child_props = [grab(blk, m.start())
                       for m in re.finditer(r'\t\t\(property ', blk)]
        # Parent-only properties (e.g. ki_locked) are part of what KiCad itself
        # produces when it flattens, so dropping them shows as lib_symbol_mismatch.
        have = {re.search(r'\(property "([^"]+)"', c).group(1) for c in child_props}
        for pm in re.finditer(r'\t\t\(property "([^"]+)"', pblk):
            if pm.group(1) not in have:
                child_props.append(grab(pblk, pm.start()))
        subs = []
        for sm in re.finditer(r'\t\t\(symbol "%s_\d+_\d+"' % re.escape(parent), pblk):
            sub = grab(pblk, sm.start())
            sub = sub.replace(f'"{parent}_', f'"{name}_', 1)   # sub-units keep BARE names
            subs.append(sub)
        flags = []
        for f in ("exclude_from_sim", "in_bom", "on_board"):
            fm = re.search(r"\(%s (yes|no)\)" % f, pblk)
            if fm:
                flags.append("\t\t" + fm.group(0))
        for f in ("pin_numbers", "pin_names"):
            fm = re.search(r"\(%s\b" % f, pblk)
            if fm:
                flags.append("\t\t" + grab(pblk, fm.start()))
        head = re.match(r'\(symbol "[^"]+"', blk).group(0)
        blk = head + "\n" + "\n".join(flags + child_props + subs) + "\n\t)"
    blk = re.sub(r'^\(symbol "%s"' % re.escape(name),
                 '(symbol "%s:%s"' % (lib, name), blk)
    return "\t\t" + blk.replace("\n", "\n\t\t")


def base_block(lib, name):
    blk = symbol_block(lib, name)
    ext = re.search(r'\(extends "([^"]+)"\)', blk)
    if ext:
        return symbol_block(lib, ext.group(1)), ext.group(1)
    return blk, name


def pin_geometry(lib, name, unit):
    """[(number, x, y, angle)] for one unit, symbol-local coordinates, body style 1."""
    blk, bname = base_block(lib, name)
    out = []
    for sm in re.finditer(r'\(symbol "%s_(\d+)_(\d+)"' % re.escape(bname), blk):
        u, style = int(sm.group(1)), int(sm.group(2))
        if u not in (0, unit) or style not in (0, 1):
            continue
        sub = grab(blk, sm.start())
        for pm in re.finditer(r"\(pin\s", sub):
            p = grab(sub, pm.start())
            at = re.search(r"\(at ([-\d.]+) ([-\d.]+)(?: ([-\d.]+))?\)", p)
            num = re.search(r'\(number "([^"]+)"', p)
            out.append((num.group(1), float(at.group(1)), float(at.group(2)),
                        float(at.group(3) or 0)))
    return out


def symbol_bbox(lib, name, unit):
    blk, bname = base_block(lib, name)
    xs, ys = [], []
    for sm in re.finditer(r'\(symbol "%s_(\d+)_(\d+)"' % re.escape(bname), blk):
        if int(sm.group(1)) not in (0, unit) or int(sm.group(2)) not in (0, 1):
            continue
        sub = grab(blk, sm.start())
        for pat in (r'\(xy ([-\d.]+) ([-\d.]+)\)',
                    r'\((?:start|end|center|mid|at) ([-\d.]+) ([-\d.]+)'):
            for m in re.finditer(pat, sub):
                xs.append(float(m.group(1)))
                ys.append(float(m.group(2)))
    return (min(xs), min(ys), max(xs), max(ys))



# ----------------------------------------------------- the IEC socket symbol
# Pin names: Commodore 64 Programmer's Reference Guide, "Serial I/O", p. 363
# (datasheets/c64-programmers-reference-guide.txt l. 17421) [D]:
#   1 /SERIAL SRQ IN, 2 GND, 3 SERIAL ATN, 4 SERIAL CLK, 5 SERIAL DATA, 6 /RESET.
# Corroborated for 3-6 by the 1541 Service Manual (ATN pin 3, CLK and DATA pins
# 4 and 5, reset pin 6 of P2/P3) [C]. Pin 7 = the earth/shield tabs.
IEC_PINS = [("1", "~{SRQ}"), ("2", "GND"), ("3", "ATN"), ("4", "CLK"), ("5", "DATA"),
            ("6", "~{RESET}"), ("7", "SHIELD")]


def write_lib():
    body = []
    for i, (num, nm) in enumerate(IEC_PINS):
        y = round((3 - i) * 2.54, 2)
        body.append(
            f'\t\t\t(pin passive line\n\t\t\t\t(at 7.62 {y} 180)\n\t\t\t\t(length 2.54)\n'
            f'\t\t\t\t(name "{nm}"\n\t\t\t\t\t(effects\n\t\t\t\t\t\t(font\n\t\t\t\t\t\t\t(size 1.27 1.27)\n'
            f'\t\t\t\t\t\t)\n\t\t\t\t\t)\n\t\t\t\t)\n\t\t\t\t(number "{num}"\n\t\t\t\t\t(effects\n'
            f'\t\t\t\t\t\t(font\n\t\t\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t\t\t)\n\t\t\t\t\t)\n\t\t\t\t)\n\t\t\t)')
    prop = lambda k, v, y, hide: (
        f'\t\t(property "{k}" "{v}"\n\t\t\t(at 0 {y} 0)\n\t\t\t(effects\n\t\t\t\t(font\n'
        f'\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)' + ('\n\t\t\t\t(hide yes)' if hide else '') +
        '\n\t\t\t)\n\t\t)')
    sym = "\n".join([
        '(kicad_symbol_lib', '\t(version 20241209)', '\t(generator "picod64_gen")',
        '\t(generator_version "9.0")',
        '\t(symbol "IEC_DIN6"', '\t\t(pin_names\n\t\t\t(offset 1.016)\n\t\t)',
        '\t\t(exclude_from_sim no)', '\t\t(in_bom yes)', '\t\t(on_board yes)',
        prop("Reference", "J", 11.43, False),
        prop("Value", "IEC_DIN6", -11.43, False),
        prop("Footprint", f"{LIBNAME}:IEC_DIN6_DualEarth", -13.97, True),
        prop("Datasheet", "~", -16.51, True),
        prop("Description", "Commodore serial (IEC) bus socket, 6-pin 240 deg DIN, "
             "pins per C64 Programmer's Reference Guide p.363", -19.05, True),
        '\t\t(symbol "IEC_DIN6_0_1"',
        '\t\t\t(rectangle\n\t\t\t\t(start -5.08 10.16)\n\t\t\t\t(end 5.08 -10.16)\n'
        '\t\t\t\t(stroke\n\t\t\t\t\t(width 0.254)\n\t\t\t\t\t(type default)\n\t\t\t\t)\n'
        '\t\t\t\t(fill\n\t\t\t\t\t(type background)\n\t\t\t\t)\n\t\t\t)',
        '\t\t)',
        '\t\t(symbol "IEC_DIN6_1_1"', "\n".join(body), '\t\t)',
        '\t\t(embedded_fonts no)', '\t)', ')'])
    os.makedirs(os.path.dirname(LIBFILE), exist_ok=True)
    open(LIBFILE, "w").write(sym + "\n")


# ------------------------------------------------- the Pico 2 symbol
# KiCad's MCU_Module:RaspberryPi_Pico (whose description says it supports the
# Pico 2) declares GND pin 3 and AGND pin 33 as POWER OUTPUTS. On this board
# ERC reports two power outputs shorted (GND and AGND both on GND). Corrected in a
# project-library copy rather than patched in the sheet, so the library and the
# sheet agree and no lib_symbol_mismatch warning has to be carried as a known
# exception. Every other pin is untouched; the pinout matches the Pico 2
# silkscreen [S20] pin for pin (checked 2026-10-09).
PICO_FIX = {("3", "GND"), ("33", "AGND")}


def pico2_symbol():
    blk = symbol_block("MCU_Module", "RaspberryPi_Pico")
    n = 0
    for num, nm in PICO_FIX:
        pat = re.compile(r'\(pin power_out (\w+)((?:(?!\(pin ).)*?\(name "%s"(?:(?!\(pin ).)*?'
                         r'\(number "%s")' % (re.escape(nm), num), re.S)
        blk, k = pat.subn(lambda m: f'(pin power_in {m.group(1)}{m.group(2)}', blk)
        n += k
    if n != len(PICO_FIX):
        raise SystemExit(f"Pico pin-type fix matched {n} pins, expected {len(PICO_FIX)} "
                         "-- the upstream symbol changed; re-check")
    # sub-units first: renaming the top level first would let this hit it again
    blk = re.sub(r'"RaspberryPi_Pico_(\d+_\d+)"', r'"RaspberryPi_Pico_2_\1"', blk)
    blk = blk.replace('(symbol "RaspberryPi_Pico"', '(symbol "RaspberryPi_Pico_2"', 1)
    blk = re.sub(r'\(property "Value" "[^"]*"', '(property "Value" "Raspberry Pi Pico 2"',
                 blk, count=1)
    return "\t" + blk



write_lib()
_lib = open(LIBFILE).read().rstrip()
assert _lib.endswith(")")
open(LIBFILE, "w").write(_lib[:-1] + pico2_symbol() + "\n)\n")
_cache.pop(LIBNAME, None)

# --------------------------------------------------------------- the design
# THROUGH-HOLE everywhere it can be (2026-10-25: "could we not go primarily thru hole?").
# The only SMD parts: the microSD socket (drag-soldered) and the Pico's castellations.
FP_PICO = f"{LIBNAME}:RaspberryPi_Pico_SMD_HandSolder_Pin1Rect"   # castellations flat (decision 4)
FP_R = "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal"   # 1/4 W axial
FP_C = "Capacitor_THT:C_Disc_D3.0mm_W2.0mm_P2.50mm"                     # radial ceramic
FP_CP = "Capacitor_THT:CP_Radial_D5.0mm_P2.00mm"                        # electrolytic
# Bourns 4600X bussed SIPs, "101" circuit: every resistor between the common bus (pin 1)
# and its own pin (Bourns 4600X datasheet [D]); Farnell UK stock checked 2026-10-25.
FP_SIP9 = "Resistor_THT:R_Array_SIP9"     # 4609X-101-103LF, 10k x 8 (9356819)
FP_SIP5 = "Resistor_THT:R_Array_SIP5"     # 4605X-101-103LF 10k x 4 (9356061); -102LF 1k x 4 (9356053)
FP_TP = "TestPoint:TestPoint_THTPad_D2.0mm_Drill1.0mm"   # bare pads: a probe or a soldered loop
FP_H6 = "Connector_PinHeader_2.54mm:PinHeader_1x06_P2.54mm_Vertical"
FP_SJ = "Jumper:SolderJumper-2_P1.3mm_Bridged_RoundedPad1.0x1.5mm"   # closed as made; cut to open
FP_TO92 = "Package_TO_SOT_THT:TO-92_Inline_Wide"   # 2.54 pitch (starved GND thermal at 1.27, 2026-10-25)
FP_DO41 = "Diode_THT:D_DO-41_SOD81_P10.16mm_Horizontal"
FP_IEC = f"{LIBNAME}:IEC_DIN6_DualEarth"
FP_SD = f"{LIBNAME}:microSD_GCT_MEM2075"   # every tail outside the body: hand-solderable (2026-10-25)
FP_S4 = "Connector_PinSocket_2.54mm:PinSocket_1x04_P2.54mm_Vertical"   # the OLED plugs in, last
FP_H3 = "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical"
FP_H2 = "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical"
FP_ENC = f"{LIBNAME}:EC11E_Switch_Alps"   # slots per the Alps drawing (AUDIT C4)
FP_LED = "LED_THT:LED_D3.0mm"
FP_BZ = "Buzzer_Beeper:Buzzer_TDK_PS1240P02BT_D12.2mm_H6.5mm"   # TDK PS1240P02BT, piezo, external drive
FP_HOLE = "MountingHole:MountingHole_3.2mm_M3"
FP_HOLE25 = "MountingHole:MountingHole_2.7mm_M2.5"

# (ref, lib, name, value, footprint, x, y, unit)
COMPS = [
    ("A1", LIBNAME, "RaspberryPi_Pico_2", "Raspberry Pi Pico 2", FP_PICO, 218.44, 139.7, 1),
    # IEC: two sockets in parallel (passthrough)
    ("J1", LIBNAME, "IEC_DIN6", "IEC_DIN6", FP_IEC, 33.02, 63.5, 1),
    ("J2", LIBNAME, "IEC_DIN6", "IEC_DIN6", FP_IEC, 33.02, 101.6, 1),
    # inputs: 6k8 / 10k dividers (x0.595), outputs: NPN open collector
    ("R1", "Device", "R", "6k8", FP_R, 66.04, 76.2, 1),
    ("R2", "Device", "R", "6k8", FP_R, 101.6, 76.2, 1),
    ("R3", "Device", "R", "6k8", FP_R, 137.16, 76.2, 1),
    ("R4", "Device", "R", "6k8", FP_R, 172.72, 76.2, 1),
    ("RN2", "Device", "R_Network04", "10k bussed", FP_SIP5, 127.0, 116.84, 1),
    ("R18", "Device", "R", "10k", "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P2.54mm_Vertical", 220.98, 76.2, 1),
    # SRQ (rev 1, design for change): input divider like the others; output DNP
    ("R15", "Device", "R", "6k8", FP_R, 208.28, 76.2, 1),
    ("Q3", "Transistor_BJT", "2N3904", "2N3904", FP_TO92, 83.82, 220.98, 1),
    ("R16", "Device", "R", "680", FP_R, 60.96, 223.52, 1),
    ("R17", "Device", "R", "10k", FP_R, 73.66, 233.68, 1),
    # test pads on the bus (rev 1): no parts
    ("TP1", "Connector", "TestPoint", "ATN", FP_TP, 220.98, 30.48, 1),
    ("TP2", "Connector", "TestPoint", "CLK", FP_TP, 231.14, 30.48, 1),
    ("TP3", "Connector", "TestPoint", "DATA", FP_TP, 241.3, 30.48, 1),
    ("TP4", "Connector", "TestPoint", "RESET", FP_TP, 251.46, 30.48, 1),
    ("TP5", "Connector", "TestPoint", "SRQ", FP_TP, 261.62, 30.48, 1),
    ("TP6", "Connector", "TestPoint", "GND", FP_TP, 271.78, 30.48, 1),
    ("Q1", "Transistor_BJT", "2N3904", "2N3904", FP_TO92, 83.82, 160.02, 1),
    ("Q2", "Transistor_BJT", "2N3904", "2N3904", FP_TO92, 83.82, 190.5, 1),
    ("R5", "Device", "R", "680", FP_R, 60.96, 162.56, 1),
    ("R6", "Device", "R", "10k", FP_R, 73.66, 172.72, 1),
    ("R7", "Device", "R", "680", FP_R, 60.96, 193.04, 1),
    ("R8", "Device", "R", "10k", FP_R, 73.66, 203.2, 1),
    # bus pull-ups, as a real 1541 (AUDIT R39): RN3
    ("RN3", "Device", "R_Network04", "1k bussed", FP_SIP5, 152.4, 40.64, 1),
    ("D4", "Diode", "1N5819", "1N5819", FP_DO41, 182.88, 25.4, 1),
    # microSD (SPI1)
    ("J3", "Connector", "Micro_SD_Card_Det1", "GCT MEM2075-00-140-01-A", FP_SD, 309.88, 63.5, 1),
    ("RN1", "Device", "R_Network08", "10k bussed", FP_SIP9, 294.64, 30.48, 1),
    ("C1", "Device", "C_Polarized", "10uF", FP_CP, 345.44, 55.88, 1),
    ("C2", "Device", "C", "100nF", FP_C, 358.14, 55.88, 1),
    # OLED: 4-pin I2C module, either power pin order (solder jumpers)
    ("J4", "Connector_Generic", "Conn_01x04", "OLED 1.3in I2C", FP_S4, 309.88, 139.7, 1),
    ("JP1", "Connector_Generic", "Conn_01x03", "OLED pin 1", FP_H3, 271.78, 129.54, 1),
    ("JP2", "Connector_Generic", "Conn_01x03", "OLED pin 2", FP_H3, 271.78, 152.4, 1),
    ("R9", "Device", "R", "4k7", FP_R, 340.36, 132.08, 1),
    ("R10", "Device", "R", "4k7", FP_R, 350.52, 132.08, 1),
    ("C3", "Device", "C", "100nF", FP_C, 360.68, 132.08, 1),
    # rotary encoder with push
    ("SW1", "Device", "RotaryEncoder_Switch", "EC11 + push", FP_ENC, 309.88, 203.2, 1),
    ("R11", "Device", "R", "10k", FP_R, 360.68, 190.5, 1),
    ("C4", "Device", "C", "10nF", FP_C, 340.36, 215.9, 1),
    ("C5", "Device", "C", "10nF", FP_C, 350.52, 215.9, 1),
    # LEDs, piezo
    ("D1", "Device", "LED", "green", FP_LED, 143.51, 241.3, 1),
    ("R12", "Device", "R", "470", FP_R, 125.73, 241.3, 1),
    ("D2", "Device", "LED", "red", FP_LED, 143.51, 259.08, 1),
    ("R13", "Device", "R", "470", FP_R, 125.73, 259.08, 1),
    ("BZ1", "Device", "Buzzer", "PS1240P02BT", FP_BZ, 190.5, 259.08, 1),
    ("R14", "Device", "R", "100", FP_R, 172.72, 254.0, 1),
    # debug UART, optional external 5 V
    ("J5", "Connector_Generic", "Conn_01x03", "debug UART", FP_H3, 33.02, 241.3, 1),
    ("J6", "Connector_Generic", "Conn_01x02", "+5V ext", FP_H2, 33.02, 269.24, 1),
    ("JP3", "Jumper", "SolderJumper_2_Bridged", "closed", FP_SJ, 99.06, 261.62, 1),
    # spare GPIO (rev 1, design for change)
    ("J7", "Connector_Generic", "Conn_01x05", "spare GPIO", "Connector_PinHeader_2.54mm:PinHeader_1x05_P2.54mm_Vertical", 190.5, 220.98, 1),
    ("TP7", "Connector", "TestPoint", "GP28", FP_TP, 281.94, 30.48, 1),
    ("D3", "Diode", "1N5819", "1N5819", FP_DO41, 68.58, 266.7, 1),
    # case mounting
    ("H1", "Mechanical", "MountingHole", "M3", FP_HOLE, 226.06, 241.3, 1),
    ("H2", "Mechanical", "MountingHole", "M3", FP_HOLE, 241.3, 241.3, 1),
    ("H3", "Mechanical", "MountingHole", "M3", FP_HOLE, 256.54, 241.3, 1),
    ("H4", "Mechanical", "MountingHole", "M3", FP_HOLE, 271.78, 241.3, 1),
    # OLED module support: 4 x M2.5 on its 30.40 x 28.50 hole pattern ("I don't see any mounting holes")
    ("H5", "Mechanical", "MountingHole", "M2.5", FP_HOLE25, 287.02, 241.3, 1),
    ("H6", "Mechanical", "MountingHole", "M2.5", FP_HOLE25, 302.26, 241.3, 1),
    ("H7", "Mechanical", "MountingHole", "M2.5", FP_HOLE25, 317.5, 241.3, 1),
    ("H8", "Mechanical", "MountingHole", "M2.5", FP_HOLE25, 332.74, 241.3, 1),
]

DESCR = {
    "A1": "Runs the emulated 1541 (core 1) and SD/UI (core 0). Powered by its own micro-USB",
    "J1": "IEC socket. Pins: C64 PRG p.363. Dual-earth footprint: generic, Lumberg 0105 06, SDF-60J",
    "J2": "IEC socket, in parallel with J1 (passthrough to the next device)",
    "R1": "ATN divider, bus side 6k8, over RN2 10k: x0.595; a 5 V bus reads 3.0 V, under the RP2350's 3.63 V limit even unpowered (datasheet tables 1430/1433)",
    "RN2": "Divider ground sides, 10k x 4 bussed to GND (pin 1 = GND)",
    "R18": "SRQ divider, ground side 10k (rev 1; a 10k x 5 SIP had no sure UK stock)",
    "R15": "SRQ divider, bus side (rev 1): SRQ readable on GP26",
    "Q3": "DNP. SRQ out, open collector (rev 1: for fast serial modes, if ever)", "R16": "DNP. Q3 base",
    "R17": "DNP. Q3 base pull-down",
    "TP1": "Test pad, IEC ATN (bus side)", "TP2": "Test pad, IEC CLK", "TP3": "Test pad, IEC DATA",
    "TP4": "Test pad, IEC RESET", "TP5": "Test pad, IEC SRQ", "TP6": "Test pad, GND",
    "JP3": "Closed as made. Cut to put an ammeter between J6 and D3 (datasette-power current, FIRMWARE.md H1)",
    "J7": "Spare GPIO (rev 1): GP2, GP3, GP6, +3V3, GND",
    "TP7": "Spare GP28 (ADC2) on a pad beside its Pico pin (rev 1)",
    "RN1": "Pull-ups to +3V3, 10k x 8 bussed (pin 1 = +3V3): SD DAT2/CS/CMD/DAT0/DAT1/detect, encoder A/B",
    "RN3": "1k bus pull-ups, as a real 1541, fed from VBUS through D4 (pin 1 = common)",
    "D4": "Blocks the bus powering the Pico through RN3 with USB unplugged (AUDIT B7)",
    "R2": "CLK divider, bus side", "R3": "DATA divider, bus side", "R4": "RESET divider, bus side",
    "Q1": "CLK out: NPN open collector onto the bus (GPIO high = line low, as the 1541's 7406)",
    "Q2": "DATA out: open collector; also the ATN acknowledge, driven by PIO",
    "R5": "Q1 base: 2.5 mA min (forced beta 10 at 25 mA, 2N3904 VCE(sat) spec point); AUDIT B2", "R6": "Q1 base pull-down: off while the Pico is unpowered or booting",
    "R7": "Q2 base", "R8": "Q2 base pull-down",
    "J3": "microSD, SPI1 on GP8-11 (RP2350 datasheet GPIO F1). GCT MEM2075 drawing rev A1. Farnell UK 2895610, LCSC C3029735",
    "C1": "microSD bulk (electrolytic: mind the polarity)", "C2": "microSD bypass",
    "J4": "OLED 1.3in SH1106 I2C, 4-pin. Pins 3/4 SCL/SDA; pins 1/2 power via JP1/JP2",
    "JP1": "OLED pin 1 to GND (cap on 1-2) or +3V3 (cap on 2-3): match the module (pin order varies by seller)",
    "JP2": "OLED pin 2 to GND or +3V3 (jumper cap): the other way to JP1",
    "R9": "SCL pull-up", "R10": "SDA pull-up",
    "C3": "OLED supply bypass",
    "SW1": "Rotary encoder with push (decision 2)", "R11": "ENC_SW pull-up",
    "C4": "ENC_A debounce", "C5": "ENC_B debounce",
    "D1": "Power LED, from +3V3", "R12": "D1 current", "D2": "Drive activity LED (VIA2 PB3)",
    "R13": "D2 current", "BZ1": "Drive sounds", "R14": "BZ1 series",
    "J5": "Debug UART0 (GP0 TX, GP1 RX)",
    "J6": "DNP. Optional external +5 V (e.g. datasette lead)",
    "D3": "DNP. 1N5819: +5V_EXT into VSYS; ORs with the Pico's own VBUS diode",
    "H1": "Case screw", "H2": "Case screw", "H3": "Case screw", "H4": "Case screw",
    "H5": "OLED standoff, M2.5 x 11", "H6": "OLED standoff, M2.5 x 11", "H7": "OLED standoff, M2.5 x 11",
    "H8": "OLED standoff, M2.5 x 11",
}
DNP = {"J6", "D3", "Q3", "R16", "R17", "J7"}
# reference and value BESIDE the body for the small vertical parts, so the
# pin labels running up and down from them never meet their text (first render)
FIELDS_RIGHT = {(c[0], 1): 1.27 for c in COMPS if re.match(r"(Q|R|C|D[12]$|BZ)", c[0])}
VALUE_AT = {("A1", 1): (3.81, 33.0)}
NOT_IN_BOM = {"H1", "H2", "H3", "H4", "H5", "H6", "H7", "H8", "TP1", "TP2", "TP3", "TP4", "TP5", "TP6", "TP7", "JP3"}

# ------------------------------------------------------------------ nets
NETS = {}


def net(ref, pin, name):
    key = (ref, str(pin))
    if key in NETS and NETS[key] != name:
        raise SystemExit(f"{ref}.{pin} assigned twice: {NETS[key]} and {name}")
    NETS[key] = name


# IEC sockets, in parallel. SRQ passes through (other devices use it); the 1541
# does not drive it [A], but rev 1 can read it (R15) and, fitted, drive it (Q3).
for j in ("J1", "J2"):
    net(j, 1, "IEC_SRQ"); net(j, 2, "GND"); net(j, 3, "IEC_ATN"); net(j, 4, "IEC_CLK")
    net(j, 5, "IEC_DATA"); net(j, 6, "IEC_RESET"); net(j, 7, "GND")

# Inputs: dividers. Pin = 0.6 x bus. RP2350 datasheet: FT pins
# take 3.63 V with IOVDD at 0 V or 1.8 V, 5.5 V at 3.3 V (table 1430); VIH 2.0 V, VIL 0.8 V
# at 3.3 V (table 1433). ⚠ Firmware: internal pulls OFF on GP12-15, Schmitt ON.
net("RN2", 1, "GND")
for k, (rt, bus, pin) in enumerate((("R1", "IEC_ATN", "ATN_IN"), ("R2", "IEC_CLK", "CLK_IN"),
                                    ("R3", "IEC_DATA", "DATA_IN"), ("R4", "IEC_RESET", "RST_IN"))):
    net(rt, 1, bus); net(rt, 2, pin)
    net("RN2", 2 + k, pin)                     # ground side: one bussed 10k network
net("R15", 1, "IEC_SRQ"); net("R15", 2, "SRQ_IN")      # SRQ (rev 1): the same divider, discrete
net("R18", 1, "SRQ_IN"); net("R18", 2, "GND")
# Outputs: 2N3904 (KiCad Transistor_BJT:2N3904: 1 E, 2 B, 3 C; TO-92 EBC)
for q, rs, rg, bus, pin, base in (("Q1", "R5", "R6", "IEC_CLK", "CLK_OUT", "CLK_B"),
                                  ("Q2", "R7", "R8", "IEC_DATA", "DATA_OUT", "DATA_B"),
                                  ("Q3", "R16", "R17", "IEC_SRQ", "SRQ_OUT", "SRQ_B")):
    net(q, 2, base); net(q, 1, "GND"); net(q, 3, bus)
    net(rs, 1, pin); net(rs, 2, base)
    net(rg, 1, base); net(rg, 2, "GND")
net("RN3", 1, "VBUS_PU")
net("D4", 2, "VBUS"); net("D4", 1, "VBUS_PU")   # A = VBUS, K = pull-up common: one way only
for k, bus in enumerate(("IEC_ATN", "IEC_CLK", "IEC_DATA", "IEC_RESET")):
    net("RN3", 2 + k, bus)

# Pico 2: GPn -> module pin, from the Pico 2 silkscreen
PICO_GP = {0: 1, 1: 2, 2: 4, 3: 5, 4: 6, 5: 7, 6: 9, 7: 10, 8: 11, 9: 12, 10: 14,
           11: 15, 12: 16, 13: 17, 14: 19, 15: 20, 16: 21, 17: 22, 18: 24, 19: 25,
           20: 26, 21: 27, 22: 29, 26: 31, 27: 32, 28: 34}
# Function choices: RP2350 datasheet GPIO function table (
# RP2350_datasheet.txt l. 1113-1127) [D]: GP4/GP5 = I2C0 SDA/SCL; GP8-11 =
# SPI1 RX/CSn/SCK/TX. IEC inputs on 4 consecutive pins (GP12-15) and outputs on
# 2 (GP16-17) so a PIO program can read/drive them as groups.
GP = {0: "DBG_TX", 1: "DBG_RX", 4: "OLED_SDA", 5: "OLED_SCL", 7: "SD_DET",
      8: "SD_MISO", 9: "SD_CS", 10: "SD_SCK", 11: "SD_MOSI",
      12: "ATN_IN", 13: "CLK_IN", 14: "DATA_IN", 15: "RST_IN",
      16: "CLK_OUT", 17: "DATA_OUT", 18: "ENC_A", 19: "ENC_B", 20: "ENC_SW",
      21: "LED_ACT", 22: "PIEZO", 26: "SRQ_IN", 27: "SRQ_OUT",
      2: "SPARE_GP2", 3: "SPARE_GP3", 6: "SPARE_GP6", 28: "SPARE_GP28"}
for g, n in GP.items():
    net("A1", PICO_GP[g], n)
for p in (3, 8, 13, 18, 23, 28, 33, 38):          # GND x7 and AGND
    net("A1", p, "GND")
net("A1", 36, "+3V3"); net("A1", 39, "VSYS"); net("A1", 40, "VBUS")
PICO_NC = [30, 35, 37]   # RUN, ADC_VREF, 3V3_EN (rev 1: the spare GPIOs go to J7, SRQ)

# microSD, SPI mode (Micro_SD_Card_Det1 matches the GCT MEM2075 footprint pad for pad:
# 1 DAT2, 2 DAT3/CD, 3 CMD, 4 VDD, 5 CLK, 6 VSS, 7 DAT0, 8 DAT1, 9 DET, 10 SHIELD;
# GCT drawing pin table P1-P8; card detect closes to the shell with a card in [D])
for p, n in ((1, "SD_DAT2"), (2, "SD_CS"), (3, "SD_MOSI"), (4, "+3V3"), (5, "SD_SCK"),
             (6, "GND"), (7, "SD_MISO"), (8, "SD_DAT1"), (9, "SD_DET"), (10, "GND")):
    net("J3", p, n)
net("RN1", 1, "+3V3")
for k, n in enumerate(("SD_DAT2", "SD_CS", "SD_MOSI", "SD_MISO", "SD_DAT1", "SD_DET", "ENC_A", "ENC_B")):
    net("RN1", 2 + k, n)                       # 10k: within the SD spec's pull-up range [A: cite]
for c in ("C1", "C2", "C3"):
    net(c, 1, "+3V3"); net(c, 2, "GND")

# OLED. SolderJumper_3: 1 A, 2 C (common), 3 B. A = GND, B = +3V3.
net("J4", 1, "OLED_P1"); net("J4", 2, "OLED_P2"); net("J4", 3, "OLED_SCL"); net("J4", 4, "OLED_SDA")
for jp, pn in (("JP1", "OLED_P1"), ("JP2", "OLED_P2")):      # 3-pin header + jumper cap
    net(jp, 1, "GND"); net(jp, 2, pn); net(jp, 3, "+3V3")
net("R9", 1, "+3V3"); net("R9", 2, "OLED_SCL")
net("R10", 1, "+3V3"); net("R10", 2, "OLED_SDA")

# Encoder: A, B, C (common), S1/S2 the push switch
net("SW1", "A", "ENC_A"); net("SW1", "B", "ENC_B"); net("SW1", "C", "GND")
net("SW1", "S1", "ENC_SW"); net("SW1", "S2", "GND")
net("R11", 1, "+3V3"); net("R11", 2, "ENC_SW")      # A/B pull-ups are in RN1
net("C4", 1, "ENC_A"); net("C4", 2, "GND")
net("C5", 1, "ENC_B"); net("C5", 2, "GND")

# LEDs (Device:LED: 1 K, 2 A), piezo (Device:Buzzer: 1 +, 2 -)
net("R12", 1, "+3V3"); net("R12", 2, "LED_PWR_A"); net("D1", 2, "LED_PWR_A"); net("D1", 1, "GND")
net("R13", 1, "LED_ACT"); net("R13", 2, "LED_ACT_A"); net("D2", 2, "LED_ACT_A"); net("D2", 1, "GND")
net("R14", 1, "PIEZO"); net("R14", 2, "PIEZO_P"); net("BZ1", 1, "PIEZO_P"); net("BZ1", 2, "GND")

# Debug, external power (1N5819: 1 K, 2 A)
net("J5", 1, "DBG_TX"); net("J5", 2, "DBG_RX"); net("J5", 3, "GND")
net("J6", 1, "+5V_J6"); net("J6", 2, "GND")
net("JP3", 1, "+5V_J6"); net("JP3", 2, "+5V_EXT")    # closed as made; cut to measure the current
for k, n in enumerate(("SPARE_GP2", "SPARE_GP3", "SPARE_GP6", "+3V3", "GND")):
    net("J7", 1 + k, n)
net("TP7", 1, "SPARE_GP28")
for k, n in enumerate(("IEC_ATN", "IEC_CLK", "IEC_DATA", "IEC_RESET", "IEC_SRQ", "GND")):
    net(f"TP{k + 1}", 1, n)
net("D3", 2, "+5V_EXT"); net("D3", 1, "VSYS")

NO_CONNECT = {("A1", str(p)) for p in PICO_NC}
POWER_NETS = {"GND", "+3V3"}
# PWR_FLAGs: GND (the Pico symbol's GND pins are inputs after the fix above) and
# VSYS (fed inside the module from VBUS; the symbol calls it an input).
FLAGS = [("GND", 101.6, 276.86), ("VSYS", 132.08, 276.86)]

# Every pin of every placed unit must be either connected or deliberately NC.
for ref, lib, name, val, fp, x, y, unit in COMPS:
    for num, *_ in pin_geometry(lib, name, unit):
        if (ref, num) not in NETS and (ref, num) not in NO_CONNECT:
            raise SystemExit(f"{ref}.{num} is neither connected nor marked no-connect")
for (ref, num) in NO_CONNECT:
    if (ref, num) in NETS:
        raise SystemExit(f"{ref}.{num} is both connected and no-connect")
_pins = {(c[0], n) for c in COMPS for n, *_ in pin_geometry(c[1], c[2], c[7])}
for key in NETS:
    if key not in _pins:
        raise SystemExit(f"{key[0]}.{key[1]} is in the net table but the symbol has no such pin")

# ------------------------------------------------------------------- emit
sheet = U("root sheet")
libs = []
for ref, lib, name, *_ in COMPS:
    if (lib, name) not in libs:
        libs.append((lib, name))
for pn in sorted(POWER_NETS):
    libs.append(("power", pn))
libs.append(("power", "PWR_FLAG"))

o = ['(kicad_sch', '\t(version 20250114)', '\t(generator "eeschema")',
     '\t(generator_version "9.0")', f'\t(uuid "{sheet}")', '\t(paper "A3")',
     f'\t(title_block\n\t\t(title "{BOARD_NAME}")\n\t\t(date "{BOARD_REV_DATE}")\n'
     f'\t\t(rev "{BOARD_REV}")\n'
     '\t\t(comment 1 "Cycle-exact 1541 on a Pico 2: IEC via dividers + 2N3904 drivers, microSD, OLED, encoder. Through-hole")\n'
     '\t\t(comment 2 "Generated by hardware/gen/gen_sch.py - do not edit by hand")\n'
     '\t\t(comment 3 "Design: HARDWARE.md")\n\t)',
     '\t(lib_symbols']
for lib, name in libs:
    o.append(resolve(lib, name))
o.append('\t)')

pw_n = [0]


# Power symbols point AWAY from the pin they hang off. Upright ones on adjacent
# side pins (J1 pins 1-3) overlapped one another and the next pin's label.
# KiCad rotation is counter-clockwise; +5V's arrow points up, GND's bars down.
POWER_ROT = {  # pin angle -> {net: symbol rotation}
    0:   {"+5V": 90,  "+3V3": 90,  "GND": 270},   # pin on a left edge, stub runs left
    180: {"+5V": 270, "+3V3": 270, "GND": 90},    # right edge, stub runs right
    90:  {"+5V": 180, "+3V3": 180, "GND": 0},     # bottom edge, stub runs down
    270: {"+5V": 0,   "+3V3": 0,   "GND": 180},   # top edge, stub runs up
}


def emit_power(netname, x, y, rot=0):
    pw_n[0] += 1
    o.append(f'\t(symbol\n\t\t(lib_id "power:{netname}")\n\t\t(at {x} {y} {rot})\n\t\t(unit 1)')
    o.append('\t\t(exclude_from_sim no)\n\t\t(in_bom yes)\n\t\t(on_board yes)\n\t\t(dnp no)')
    o.append(f'\t\t(uuid "{U(f"#PWR{pw_n[0]:03d}")}")')
    o.append(f'\t\t(property "Reference" "#PWR{pw_n[0]:03d}"\n\t\t\t(at {x} {y} 0)'
             '\n\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)'
             '\n\t\t\t\t(hide yes)\n\t\t\t)\n\t\t)')
    # Rails visible, GND hidden.
    hide = '\n\t\t\t\t(hide yes)' if netname == 'GND' else ''
    # value text sits beyond the arrow, horizontal, justified away from the pin
    vx, vy, just = {0: (x, y - 3.5, ""), 180: (x, y + 3.5, ""),
                    90: (x - 3.0, y, "\n\t\t\t\t(justify right)"),
                    270: (x + 3.0, y, "\n\t\t\t\t(justify left)")}[rot % 360]
    if netname == "GND":
        vy = y + 3.5
    # A field's angle is RELATIVE to its symbol (a resistor at 90 deg needs 270 for
    # its value to read level), so a rotated rail needs the inverse angle to read horizontally.
    fang = (360 - rot) % 360
    o.append(f'\t\t(property "Value" "{netname}"\n\t\t\t(at {vx} {vy} {fang})'
             '\n\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)'
             f'{just}{hide}\n\t\t\t)\n\t\t)')
    for p in ("Footprint", "Datasheet", "Description"):
        o.append(f'\t\t(property "{p}" ""\n\t\t\t(at {x} {y} 0)\n\t\t\t(effects'
                 '\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)'
                 '\n\t\t\t\t(hide yes)\n\t\t\t)\n\t\t)')
    o.append(f'\t\t(pin "1"\n\t\t\t(uuid "{U(f"#PWR{pw_n[0]:03d} pin 1")}")\n\t\t)')
    o.append(f'\t\t(instances\n\t\t\t(project "{PROJECT}"\n\t\t\t\t(path "/{sheet}"'
             f'\n\t\t\t\t\t(reference "#PWR{pw_n[0]:03d}")\n\t\t\t\t\t(unit 1)'
             '\n\t\t\t\t)\n\t\t\t)\n\t\t)')
    o.append('\t)')


def emit_wire(x1, y1, x2, y2):
    o.append(f'\t(wire\n\t\t(pts\n\t\t\t(xy {x1} {y1}) (xy {x2} {y2})\n\t\t)'
             '\n\t\t(stroke\n\t\t\t(width 0)\n\t\t\t(type default)\n\t\t)'
             f'\n\t\t(uuid "{U()}")\n\t)')


def emit_nc(x, y):
    o.append(f'\t(no_connect\n\t\t(at {x} {y})\n\t\t(uuid "{U()}")\n\t)')


def stub_end(sx, sy, ang, length=2.54):
    """(sx,sy) is the pin's connection end; the body lies along `ang`, so the
    stub extends the opposite way."""
    a = math.radians(ang)
    return (round(sx - length * math.cos(a), 2), round(sy + length * math.sin(a), 2))


def label_style(pin_ang):
    a = int(pin_ang) % 360
    return {0: (0, "right"), 180: (0, "left"), 90: (90, "right"), 270: (90, "left")}[a]


def emit_label(text, x, y, ang):
    lang, just = label_style(ang)
    o.append(f'\t(label "{text}"\n\t\t(at {x} {y} {lang})\n\t\t(effects'
             '\n\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n\t\t\t)'
             f'\n\t\t\t(justify {just} bottom)\n\t\t)'
             f'\n\t\t(uuid "{U()}")\n\t)')


endpoints = {}
placed = 0
for ref, lib, name, val, fp, x, y, unit in COMPS:
    pins = pin_geometry(lib, name, unit)
    bb = symbol_bbox(lib, name, unit)
    top = max(bb[3], max((p[2] for p in pins), default=bb[3]))
    bot = min(bb[1], min((p[2] for p in pins), default=bb[1]))
    in_bom = "no" if ref in NOT_IN_BOM else "yes"
    o.append(f'\t(symbol\n\t\t(lib_id "{lib}:{name}")\n\t\t(at {x} {y} 0)\n\t\t(unit {unit})')
    o.append(f'\t\t(exclude_from_sim no)\n\t\t(in_bom {in_bom})\n\t\t(on_board yes)'
             f'\n\t\t(dnp {"yes" if ref in DNP else "no"})')
    o.append(f'\t\t(uuid "{U(f"{ref} unit {unit}")}")')
    show = True
    # Default: reference above, value below. Parts with power stubs leaving the
    # top or bottom put their text BESIDE the body instead, or it lands on the
    # stub and the power symbol (seen on the first render: U1E, C1, A1).
    side = FIELDS_RIGHT.get((ref, unit))
    if side:
        fx = round(x + bb[2] + side, 2)
        fields = (("Reference", ref, fx, round(y - 1.27, 2), "left"),
                  ("Value", val, fx, round(y + 1.27, 2), "left"))
    else:
        fields = (("Reference", ref, x, round(y - top - 4.0, 2), None),
                  ("Value", val, x, round(y - bot + 4.0, 2), None))
    if (ref, unit) in VALUE_AT:
        dx, dy = VALUE_AT[(ref, unit)]
        fields = (fields[0], ("Value", val, round(x + dx, 2), round(y + dy, 2), "left"))
    for pname, pval, px_, py, just in fields:
        hid = "" if show or pname == "Reference" else "\n\t\t\t\t(hide yes)"
        jst = f"\n\t\t\t\t(justify {just})" if just else ""
        o.append(f'\t\t(property "{pname}" "{pval}"\n\t\t\t(at {px_} {py} 0)'
                 '\n\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)'
                 f'{jst}{hid}\n\t\t\t)\n\t\t)')
    for pname, pval in (("Footprint", fp), ("Datasheet", "~"),
                        ("Description", DESCR.get(ref, ""))):
        o.append(f'\t\t(property "{pname}" "{pval}"\n\t\t\t(at {x} {y} 0)\n\t\t\t(effects'
                 '\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)'
                 '\n\t\t\t\t(hide yes)\n\t\t\t)\n\t\t)')
    for num, *_ in pins:
        o.append(f'\t\t(pin "{num}"\n\t\t\t(uuid "{U(f"{ref} unit {unit} pin {num}")}")\n\t\t)')
    o.append(f'\t\t(instances\n\t\t\t(project "{PROJECT}"\n\t\t\t\t(path "/{sheet}"'
             f'\n\t\t\t\t\t(reference "{ref}")\n\t\t\t\t\t(unit {unit})\n\t\t\t\t)'
             '\n\t\t\t)\n\t\t)')
    o.append('\t)')
    for num, px, py, ang in pins:
        sx, sy = round(x + px, 2), round(y - py, 2)      # symbol Y up, sheet Y down
        if (ref, num) in NO_CONNECT:
            emit_nc(sx, sy)
            continue
        n = NETS[(ref, num)]
        ex, ey = stub_end(sx, sy, ang, 5.08 if n in POWER_NETS else 2.54)
        prev = endpoints.get((ex, ey))
        if prev and prev != n:
            raise SystemExit(f"stub collision at ({ex},{ey}): {prev} vs {n} ({ref}.{num})")
        endpoints[(ex, ey)] = n
        emit_wire(sx, sy, ex, ey)
        if n in POWER_NETS:
            emit_power(n, ex, ey, POWER_ROT[int(ang) % 360][n])
        else:
            emit_label(n, ex, ey, ang)
        placed += 1

# PWR_FLAGs: +5V and GND come in through the passive edge fingers, and VSYS
# through D1, a passive -- ERC would report each as undriven without a flag.
# VSYS is board-local (no power: symbol), so it is tied by label, as on an earlier board.
# Each flag sits beside an UPRIGHT rail symbol on a short horizontal wire. Drawn
# vertically, the +5V symbol had to be rotated 180 deg and KiCad printed its
# name upside down (render 3); upright symbols need no text tricks at all.
for netname, fx, fy in FLAGS:
    emit_wire(fx - 10.16, fy, fx, fy)      # long enough that the two names do not meet
    if netname in POWER_NETS:
        emit_power(netname, fx - 10.16, fy, 0)
    else:
        emit_label(netname, fx - 10.16, fy, 0)
    pw_n[0] += 1
    o.append(f'\t(symbol\n\t\t(lib_id "power:PWR_FLAG")\n\t\t(at {fx} {fy} 0)\n\t\t(unit 1)')
    o.append('\t\t(exclude_from_sim no)\n\t\t(in_bom yes)\n\t\t(on_board yes)\n\t\t(dnp no)')
    o.append(f'\t\t(uuid "{U(f"#FLG{pw_n[0]:03d}")}")')
    o.append(f'\t\t(property "Reference" "#FLG{pw_n[0]:03d}"\n\t\t\t(at {fx} {fy} 0)'
             '\n\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)'
             '\n\t\t\t\t(hide yes)\n\t\t\t)\n\t\t)')
    o.append(f'\t\t(property "Value" "PWR_FLAG"\n\t\t\t(at {fx} {fy - 3.5} 0)'
             '\n\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)\n\t\t\t)\n\t\t)')
    for p in ("Footprint", "Datasheet", "Description"):
        o.append(f'\t\t(property "{p}" ""\n\t\t\t(at {fx} {fy} 0)\n\t\t\t(effects'
                 '\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)'
                 '\n\t\t\t\t(hide yes)\n\t\t\t)\n\t\t)')
    o.append(f'\t\t(pin "1"\n\t\t\t(uuid "{U(f"#FLG{pw_n[0]:03d} pin 1")}")\n\t\t)')
    o.append(f'\t\t(instances\n\t\t\t(project "{PROJECT}"\n\t\t\t\t(path "/{sheet}"'
             f'\n\t\t\t\t\t(reference "#FLG{pw_n[0]:03d}")\n\t\t\t\t\t(unit 1)'
             '\n\t\t\t\t)\n\t\t\t)\n\t\t)')
    o.append('\t)')


o.append('\t(sheet_instances\n\t\t(path "/"\n\t\t\t(page "1")\n\t\t)\n\t)')
o.append('\t(embedded_fonts no)')
o.append(')')

open(OUT, "w").write("\n".join(o) + "\n")

import json
json.dump({f"{r}.{p}": n for (r, p), n in sorted(NETS.items())},
          open(os.path.join(HW, "gen", "intended_nets.json"), "w"), indent=1)
print(f"wrote {OUT}")
print(f"  components: {len(set(c[0] for c in COMPS))}  units placed: {len(COMPS)}  "
      f"connected pins: {placed}  no-connects: {len(NO_CONNECT)}")
