#!/usr/bin/env python3
"""
Order files for PicoD64: Gerbers + drill (zip for JLCPCB) and the BOM.

    python3 hardware/gen/fab.py            (after gen/board.sh has passed)

Writes hardware/fab/:
  <FAB_NAME>-rev<R>-gerbers.zip  F/B copper, mask, silk, paste, outline + PTH/NPTH Excellon
  <FAB_NAME>-bom.csv              one line per part, Farnell UK codes, fit / do-not-fit
  BOM.md                          the same, readable, plus the parts not on the PCB

The BOM is built from the SCHEMATIC (kicad-cli), joined to the PARTS table below; it stops if a
schematic reference has no PARTS entry or a PARTS entry names a reference the schematic lacks,
so the two cannot drift. Stock figures are from hardware/AUDIT.md (F1, read 2026-10-26).
"""
import csv
import io
import os
import subprocess
import sys
import zipfile

HW = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(HW, "fab")
SCH = os.path.join(HW, "picod64.kicad_sch")
PCB = os.path.join(HW, "picod64.kicad_pcb")
REV = "1.1"               # rev 1.1 = rev 1 with the PicoD64 silkscreen (copper identical); rev 1 as ordered: fab/rev1/
FAB_NAME = "picod64"

# refs -> (description, manufacturer, MPN, Farnell UK, note). [C] = picked by spec, not checked.
PARTS = [
    ("A1", "Raspberry Pi Pico 2 (RP2350), no headers; soldered flat on its castellations",
     "Raspberry Pi", "SC1631", "4531086", ""),
    ("J1 J2", "DIN 6-pin 240° socket, right angle, PCB", "Lumberg", "0105 06", "2677786",
     "Same Sky SDF-60J also fits the footprint"),
    ("J3", "microSD socket, push-push, all tails exposed", "GCT", "MEM2075-00-140-01-A", "2895610",
     "LCSC C3029735; the only SMD part (drag-solder, flux)"),
    ("SW1", "Rotary encoder with push switch, 15 PPR / 30 detents, 20 mm D shaft, 0.5 mm travel",
     "Alps Alpine", "EC11E15244G1", "2064986", "NOT the B2 (1.5 mm travel): the knob would bottom"),
    ("RN1", "Resistor network 10k x 8, bussed, SIP-9 (pin 1 = common, dot)", "Bourns",
     "4609X-101-103LF", "9356819", "pin 1 = square pad"),
    ("RN2", "Resistor network 10k x 4, bussed, SIP-5 (pin 1 = common, dot)", "Bourns",
     "4605X-101-103LF", "9356061", "pin 1 = square pad"),
    ("RN3", "Resistor network 1k x 4, bussed, SIP-5", "Bourns", "4605X-101-102LF", "9356053",
     "1k bus pull-ups, as a real 1541; pin 1 (dot) = square pad; only on USB power (AUDIT R39)"),
    ("D4", "Schottky 1N5819, DO-41", "Vishay", "1N5819-E3/54", "1336533",
     "blocks the bus powering the Pico; band (K) toward RN3 pin 1"),
    ("D3", "Schottky 1N5819, DO-41", "Vishay", "1N5819-E3/54", "1336533", "only with J6 (external 5 V)"),
    ("Q1 Q2", "NPN 2N3904, TO-92 (E-B-C)", "onsemi", "2N3904BU", "1700648",
     "bend the outer legs out to the 2.54 mm holes; flat face as the silk"),
    ("R1 R2 R3 R4 R15", "Resistor 6k8 1 % 0.25 W metal film", "Multicomp Pro", "MF25 6K8", "9342168", ""),
    ("R5 R7", "Resistor 680R 1 % 0.25 W", "Multicomp Pro", "MF25 680R", "9342150", ""),
    ("R6 R8 R11 R18", "Resistor 10k 1 % 0.25 W", "Multicomp Pro", "MF25 10K", "9341110",
     "R18 stands upright (2.54 mm holes), at the left edge"),
    ("Q3", "NPN 2N3904, TO-92 (E-B-C)", "onsemi", "2N3904BU", "1700648",
     "SRQ out (rev 1): fit only if a fast serial mode ever needs it, with R16 and R17"),
    ("R16", "Resistor 680R 1 % 0.25 W", "Multicomp Pro", "MF25 680R", "9342150", "with Q3 only"),
    ("R17", "Resistor 10k 1 % 0.25 W", "Multicomp Pro", "MF25 10K", "9341110", "with Q3 only"),
    ("R12", "Resistor 470R 1 % 0.25 W", "Multicomp Pro", "MF25 470R", "9341943", "was 1k: 470R matches D1 to D2 (~2.8 mA each)"),
    ("R13", "Resistor 470R 1 % 0.25 W", "Multicomp Pro", "MF25 470R", "9341943", ""),
    ("R9 R10", "Resistor 4k7 1 % 0.25 W (I²C pull-ups)", "Multicomp Pro", "MF25 4K7", "9341951",
     "always fitted: fine with or without pull-ups on the module (AUDIT R37); under the module, so fit before it"),
    ("R14", "Resistor 100R 1 % 0.25 W", "Multicomp Pro", "MF25 100R", "9341099", "BZ1 series"),
    ("C1", "Electrolytic 10 µF 50 V, 5 x 11 mm, 2.0 mm pitch", "Multicomp Pro",
     "MCGPR50V106M5X11", "9451382", "+ to the square pad"),
    ("C2 C3", "Ceramic 100 nF 50 V X7R, radial, 2.5 mm lead spacing", "Vishay", "K104K15X7RF53L2", "1141775",
     "suffix L2 = straight 2.5 mm [D: Vishay 45171]; 165,160 in stock 2026-10-26. NOT ...TH5 (5.0 mm)"),
    ("C4 C5", "Ceramic 10 nF 50 V X7R, radial, 2.5 mm lead spacing", "Vishay", "K103K15X7RF53L2", "1141772",
     "L2 = straight 2.5 mm; 35,115 in stock 2026-10-26. NOT ...TH5 (5.0 mm)"),
    ("D1", "LED 3 mm green diffused, 568 nm, 8-20 mcd @ 10 mA", "Kingbright", "L-934GD", "1142502",
     "same family as D2 (L-934); flat/short leg = square pad"),
    ("D2", "LED 3 mm high-efficiency red diffused, 625 nm, 8-20 mcd @ 10 mA", "Kingbright", "L-934ID", "1142517",
     "same family as D1; R13 470R -> ~2.8 mA from GP21 (< 4 mA drive)"),
    ("J4", "Socket 1 x 4, 2.54 mm, 8.5 mm high", "Würth", "61300411821", "2827888",
     "the OLED plugs in here, last"),
    ("JP1 JP2", "Pin header 1 x 3, 2.54 mm", "Würth", "61304011121 (cut)", "",
     "3 pins cut from the 1 x 40 strip; each takes a jumper cap"),
    ("J5", "Pin header 1 x 3, 2.54 mm (debug UART)", "Würth", "61304011121 (cut)", "", "3 pins cut from the 1 x 40 strip"),
    ("J7", "Pin header 1 x 5, 2.54 mm (spare GP2/3/6, 3V3, GND)", "Würth", "61304011121 (cut)", "",
     "optional; 5 pins cut from the 1 x 40 strip"),
    ("J6", "Pin header 1 x 2, 2.54 mm", "Würth", "61304011121 (cut)", "", "external 5 V, with D3; 2 pins cut from the strip"),
    ("BZ1", "Piezo transducer, external drive, Ø12.2 x 6.5 mm, 5.0 mm pitch", "TDK", "PS1240P02BT", "3267212",
     "drive sounds, always fitted (AUDIT R40); 12,019 in stock 2026-10-26. NOT a magnetic/self-drive buzzer"),
]
DNP_EXPECTED = {"J6", "D3", "Q3", "R16", "R17", "J7"}
EXTRA = [   # not on the PCB
    ("1", "Pin header strip 1 x 40, 2.54 mm, breakaway: cut J5, JP1, JP2 (3 each), J6 (2), J7 (5)", "Würth",
     "61304011121", "2356175", "one strip instead of three Harwin lines (Farnell minimums of 10-100)"),
    ("2", "Jumper cap 2.54 mm (JP1, JP2)", "Harwin", "M7686-46", "3756818", "order code unique in Farnell search (M7566-05's 150411 was not)"),
    ("4", "Printed OLED pillar, snaps into H5-H8 (case file, part \"oled_pillars\")", "", "", "",
     "no screws: they slide onto the module's edges (BUILD-NOTES: OLED)"),
    ("1", "OLED module 1.3\" SH1106, I²C, 4-pin (VCC/GND order either way)", "[C] any", "", "",
     "measure it for the pillars (BUILD-NOTES: OLED)"),
    ("4", "M2.6 x 16 countersunk self-tapping screw", "[C] any", "", "",
     "from below into the lid posts (picod64-lid-front.stl + picod64-lid-rear.stl)"),
    ("(4)", "OPTIONAL: M3 x 16 countersunk machine screw + M3 heat-set insert (Ø4.0 mm hole, <= 7 mm long)", "[C] any", "", "",
     "instead, only with the picod64-lid-*-insert.stl pair, for a case opened often"),
    ("4", "Self-adhesive rubber foot, 10 mm (max 10.6)", "[C] any", "", "", "stick on over the screw heads, centred in the rings marked round them"),
    ("2", "Printed LED standoff (case file, part \"led_standoffs\")", "", "", "",
     "sets D1/D2 so the dome stands 1.5 mm proud of the lid; needs >= 14 mm of LED lead"),
    ("1", "Printed case: tray; lid front + lid rear, top face down; knob; 4 OLED pillars; 2 LED standoffs; 3 badges; the knob seating tool (no brim, no supports)", "", "", "",
     "case/picod64-case.scad"),
]


def run(*a):
    r = subprocess.run(a, capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"*** {' '.join(a[:4])} failed:\n{r.stdout}{r.stderr}")


def schematic_refs():
    tmp = os.path.join(OUT, ".bom_raw.csv")
    run("kicad-cli", "sch", "export", "bom", "--fields", "Reference,Value,Footprint,${DNP}",
        "--labels", "Ref,Value,Footprint,DNP", "-o", tmp, SCH)
    rows = list(csv.DictReader(open(tmp)))
    os.remove(tmp)
    return {r["Ref"]: r for r in rows}


os.makedirs(OUT, exist_ok=True)
sch = schematic_refs()
listed = {ref for refs, *_ in PARTS for ref in refs.split()}
missing, extra = sorted(set(sch) - listed), sorted(listed - set(sch))
if missing or extra:
    sys.exit(f"*** BOM out of step with the schematic: no PARTS entry for {missing}; not in schematic {extra}")
dnp = {r for r, v in sch.items() if v["DNP"]}
if dnp != DNP_EXPECTED:
    sys.exit(f"*** DNP set changed: schematic {sorted(dnp)} vs expected {sorted(DNP_EXPECTED)}")

lines = []
for refs, desc, maker, mpn, farnell, note in PARTS:
    rl = refs.split()
    if len({r in dnp for r in rl}) > 1:
        sys.exit(f"*** {refs}: mixes fitted and do-not-fit references -- split the line")
    fit = "do not fit" if rl[0] in dnp else "fit"
    vals = sorted({sch[r]["Value"] for r in rl})
    lines.append({"Refs": " ".join(rl), "Qty": len(rl), "Fit": fit, "Value": "/".join(vals),
                  "Description": desc, "Manufacturer": maker, "MPN": mpn, "Farnell UK": farnell,
                  "Footprint": sch[rl[0]]["Footprint"].split(":")[-1], "Notes": note})
lines.sort(key=lambda l: (l["Fit"] != "fit", l["Refs"]))
cols = ["Refs", "Qty", "Fit", "Value", "Description", "Manufacturer", "MPN", "Farnell UK", "Footprint", "Notes"]
with open(os.path.join(OUT, f"{FAB_NAME}-bom.csv"), "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=cols)
    w.writeheader()
    w.writerows(lines)
    for qty, desc, maker, mpn, farnell, note in EXTRA:
        w.writerow({"Refs": "(off board)", "Qty": qty, "Fit": "fit", "Description": desc,
                    "Manufacturer": maker, "MPN": mpn, "Farnell UK": farnell, "Notes": note})

md = [f"# PicoD64 rev {REV}: bill of materials", "",
      "Generated by `hardware/gen/fab.py` from the schematic. Farnell UK stock checked 2026-10-26 "
      "(hardware/AUDIT.md F1). [C] = choose by the spec given.", "",
      "## On the board (fit)", "", "| Refs | Qty | Value | Part | Maker / MPN | Farnell UK | Notes |",
      "|---|---|---|---|---|---|---|"]
for fitflag, title in (("fit", None), ("do not fit", "## Optional (do not fit for a standard build)")):
    if title:
        md += ["", title, "", "| Refs | Qty | Value | Part | Maker / MPN | Farnell UK | Notes |", "|---|---|---|---|---|---|---|"]
    for l in lines:
        if l["Fit"] == fitflag:
            md.append(f"| {l['Refs']} | {l['Qty']} | {l['Value']} | {l['Description']} | "
                      f"{(l['Manufacturer'] + ' ' + l['MPN']).strip()} | {l['Farnell UK']} | {l['Notes']} |")
md += ["", "## Not on the board", "", "| Qty | Part | Maker / MPN | Farnell UK | Notes |", "|---|---|---|---|---|"]
for qty, desc, maker, mpn, farnell, note in EXTRA:
    md.append(f"| {qty} | {desc} | {(maker + ' ' + mpn).strip()} | {farnell} | {note} |")
open(os.path.join(OUT, "BOM.md"), "w").write("\n".join(md) + "\n")

# ---- Gerbers + drill
gdir = os.path.join(OUT, "gerbers")
os.makedirs(gdir, exist_ok=True)
for f in os.listdir(gdir):
    os.remove(os.path.join(gdir, f))
run("kicad-cli", "pcb", "export", "gerbers", "--layers",
    "F.Cu,B.Cu,F.Mask,B.Mask,F.SilkS,B.SilkS,F.Paste,Edge.Cuts", "-o", gdir + "/", PCB)
run("kicad-cli", "pcb", "export", "drill", "--format", "excellon", "--excellon-separate-th",
    "--excellon-units", "mm", "-o", gdir + "/", PCB)
names = sorted(os.listdir(gdir))
need = ["-F_Cu.", "-B_Cu.", "-F_Mask.", "-B_Mask.", "-F_Silkscreen.", "-Edge_Cuts.", "-PTH.drl", "-NPTH.drl"]
for n in need:
    if not any(n in x for x in names):
        sys.exit(f"*** missing fab layer {n}")
zp = os.path.join(OUT, f"{FAB_NAME}-rev{REV}-gerbers.zip")
with zipfile.ZipFile(zp, "w", zipfile.ZIP_DEFLATED) as z:
    for n in names:
        z.write(os.path.join(gdir, n), n)
fit_n = sum(l["Qty"] for l in lines if l["Fit"] == "fit")
print(f"BOM: {len(lines)} lines, {fit_n} parts to fit, {sum(l['Qty'] for l in lines) - fit_n} optional; "
      f"every schematic ref covered")
print(f"Gerbers: {len(names)} files -> {os.path.relpath(zp, os.path.dirname(HW))}")
