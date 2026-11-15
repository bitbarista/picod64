#!/usr/bin/env python3
"""
Check the schematic's REAL connectivity -- as KiCad reads it -- against the design.

ERC passing proves the sheet is internally consistent, not that it is the design.
A label one grid square off a stub end, or a stub landing on the wrong pin, can
pass ERC and still wire CLK to DATA. So this exports KiCad's own netlist and checks
it two ways:

  1. against gen/intended_nets.json, written by gen_sch.py from its NETS table --
     catches geometry: labels that did not attach, stubs that shorted;
  2. against SPEC below, typed INDEPENDENTLY from HARDWARE.md and the
     datasheets -- catches a wrong NETS table, which (1) cannot, because (1)
     compares the generator with itself.

Usage: check_sch.py [netlist.net]     (default: exports a fresh one)
Exit 0 = every check passed.
"""
import json
import os
import re
import subprocess
import sys

HW = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCH = os.path.join(HW, "picod64.kicad_sch")

# ---- 2. the independent table -------------------------------------------------
# Each entry: a set of pins that MUST be on one net, and nothing else may be.
# Typed by hand from HARDWARE.md and the sources, NOT from gen_sch.py's NETS:
#   IEC pins: C64 Programmer's Reference Guide p.363 (1 SRQ 2 GND 3 ATN 4 CLK 5 DATA 6 RESET)
#   Pico 2 module pins: GP0 1, GP1 2, GP4 6, GP5 7, GP7 10, GP8 11, GP9 12, GP10 14,
#     GP11 15, GP12 16, GP13 17, GP14 19, GP15 20, GP16 21, GP17 22, GP18 24, GP19 25,
#     GP20 26, GP21 27, GP22 29; 36 3V3, 39 VSYS, 40 VBUS (Pico 2 silkscreen)
#   SPI1 on GP8 RX / GP9 CSn / GP10 SCK / GP11 TX; I2C0 GP4 SDA / GP5 SCL (RP2350 datasheet)
#   microSD SPI mode: CS = DAT3 (pin 2), MOSI = CMD (3), SCK = CLK (5), MISO = DAT0 (7)
#   Bourns 4600X bussed ('101'): pin 1 = common bus, pins 2.. = one resistor each [D].
#   2N3904 (KiCad): 1 E, 2 B, 3 C. LED: 1 K, 2 A. 1N5819: 1 K, 2 A.
SPEC = [
    {"J1.1", "J2.1", "R15.1", "Q3.3", "TP5.1"},                  # SRQ passes through; rev 1: divider, Q3 (DNP), pad
    {"J1.3", "J2.3", "R1.1", "RN3.2", "TP1.1"},                  # ATN bus -> divider, test pad
    {"J1.4", "J2.4", "R2.1", "Q1.3", "RN3.3", "TP2.1"},          # CLK bus: divider + collector
    {"J1.5", "J2.5", "R3.1", "Q2.3", "RN3.4", "TP3.1"},          # DATA bus: divider + collector
    {"J1.6", "J2.6", "R4.1", "RN3.5", "TP4.1"},                  # RESET bus -> divider
    {"A1.16", "R1.2", "RN2.2"},                                  # GP12 ATN_IN (divider tap)
    {"A1.17", "R2.2", "RN2.3"},                                  # GP13 CLK_IN
    {"A1.19", "R3.2", "RN2.4"},                                  # GP14 DATA_IN
    {"A1.20", "R4.2", "RN2.5"},                                  # GP15 RST_IN
    {"A1.31", "R15.2", "R18.1"},                                 # GP26 SRQ_IN (rev 1): 6k8 over 10k
    {"A1.32", "R16.1"}, {"R16.2", "R17.1", "Q3.2"},              # GP27 SRQ_OUT -> Q3 base (DNP)
    {"A1.4", "J7.1"}, {"A1.5", "J7.2"}, {"A1.9", "J7.3"}, {"A1.34", "TP7.1"},  # GP2 GP3 GP6 spare; GP28 on a pad
    {"A1.21", "R5.1"}, {"R5.2", "R6.1", "Q1.2"},                 # GP16 CLK_OUT -> Q1 base
    {"A1.22", "R7.1"}, {"R7.2", "R8.1", "Q2.2"},                 # GP17 DATA_OUT -> Q2 base
    {"A1.40", "D4.2"},                                           # VBUS -> D4 anode
    {"D4.1", "RN3.1"},                                           # D4 cathode -> pull-up common: bus cannot feed VBUS
    {"A1.14", "J3.5"},                                           # GP10 SCK
    {"A1.15", "J3.3", "RN1.4"},                                  # GP11 MOSI = CMD
    {"A1.11", "J3.7", "RN1.5"},                                  # GP8 MISO = DAT0
    {"A1.12", "J3.2", "RN1.3"},                                  # GP9 CS = DAT3
    {"J3.1", "RN1.2"}, {"J3.8", "RN1.6"},                        # DAT2, DAT1 held high
    {"A1.10", "J3.9", "RN1.7"},                                  # GP7 card detect
    {"A1.6", "J4.4", "R10.2"}, {"A1.7", "J4.3", "R9.2"},        # I2C0 SDA, SCL
    {"J4.1", "JP1.2"}, {"J4.2", "JP2.2"},                        # OLED power pins via jumpers
    {"A1.24", "SW1.A", "RN1.8", "C4.1"},                         # GP18 ENC_A
    {"A1.25", "SW1.B", "RN1.9", "C5.1"},                         # GP19 ENC_B
    {"A1.26", "SW1.S1", "R11.2"},                                # GP20 ENC_SW
    {"A1.27", "R13.1"}, {"R13.2", "D2.2"},                       # GP21 activity LED
    {"R12.2", "D1.2"},                                           # power LED
    {"A1.29", "R14.1"}, {"R14.2", "BZ1.1"},                      # GP22 piezo
    {"A1.1", "J5.1"}, {"A1.2", "J5.2"},                          # GP0/GP1 debug UART
    {"J6.1", "JP3.1"}, {"JP3.2", "D3.2"}, {"A1.39", "D3.1"},     # +5V ext -> JP3 (closed) -> 1N5819 -> VSYS
    {"A1.36",
     "J3.4", "RN1.1", "C1.1", "C2.1", "C3.1",
     "JP1.3", "JP2.3", "R9.1", "R10.1", "R11.1", "R12.1", "J7.4"},   # +3V3
    {"J1.2", "J1.7", "J2.2", "J2.7", "Q1.1", "Q2.1", "R6.2", "R8.2",
     "RN2.1",
     "A1.3", "A1.8", "A1.13", "A1.18", "A1.23", "A1.28", "A1.33", "A1.38",
     "J3.6", "J3.10", "C1.2", "C2.2", "C3.2", "C4.2", "C5.2", "JP1.1", "JP2.1",
     "SW1.C", "SW1.S2", "D1.1", "D2.1", "BZ1.2", "J5.3", "J6.2",
     "Q3.1", "R17.2", "R18.2", "J7.5", "TP6.1"},                                         # GND
]
SPEC_NC = {f"A1.{p}" for p in (30, 35, 37)}   # RUN ADC_VREF 3V3_EN (rev 1: GP2/3/6/28 to J7, GP26/27 SRQ)


def export(net_path):
    r = subprocess.run(["kicad-cli", "sch", "export", "netlist", "--format", "kicadsexpr",
                        "-o", net_path, SCH], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"netlist export failed: {r.stderr}")


def parse(net_path):
    t = open(net_path).read()
    nets = {}
    for m in re.finditer(r'\(net \(code "?\d+"?\) \(name "([^"]*)"\)(.*?)\)\s*(?=\(net |\)\s*\)\s*$)',
                         t, re.S):
        pins = {f"{a}.{b}" for a, b in
                re.findall(r'\(node \(ref "([^"]+)"\) \(pin "([^"]+)"', m.group(2))}
        nets[m.group(1)] = pins
    if not nets:
        sys.exit("parsed no nets from the netlist -- the parser, not the design, is broken")
    return nets


def main():
    net_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HW, "picod64.net")
    if len(sys.argv) == 1:
        export(net_path)
    nets = parse(net_path)
    pin_net = {}
    for name, pins in nets.items():
        for p in pins:
            pin_net[p] = name
    fails = []

    # 1. against the generator's own table
    intended = json.load(open(os.path.join(HW, "gen", "intended_nets.json")))
    groups = {}
    for pin, n in intended.items():
        groups.setdefault(n, set()).add(pin)
    for n, pins in groups.items():
        got = {pin_net.get(p) for p in pins}
        if len(got) != 1 or None in got:
            fails.append(f"[gen] net {n}: pins split across {sorted(map(str, got))}")
            continue
        actual = nets[got.pop()]
        extra = {p for p in actual if p in intended and intended[p] != n}
        if extra:
            fails.append(f"[gen] net {n} also contains {sorted(extra)} -- a short")

    # 2. against the independent table
    for group in SPEC:
        got = {pin_net.get(p) for p in group}
        if len(got) != 1 or None in got:
            fails.append(f"[spec] {sorted(group)} not on one net: "
                         f"{ {p: pin_net.get(p) for p in sorted(group)} }")
            continue
        actual = nets[got.pop()]
        if actual != group:
            fails.append(f"[spec] net with {sorted(group)} also has {sorted(actual - group)}")
    spec_pins = set().union(*SPEC)
    for p in SPEC_NC:
        n = pin_net.get(p)
        if n is not None and len(nets[n]) > 1:
            fails.append(f"[spec] {p} must be unconnected but is on {n} with {sorted(nets[n])}")
    # every pin KiCad sees on a multi-pin net must be accounted for by SPEC
    for name, pins in nets.items():
        if len(pins) > 1 and not pins <= spec_pins:
            fails.append(f"[spec] net {name} has pins SPEC does not know: "
                         f"{sorted(pins - spec_pins)}")

    if fails:
        print("SCHEMATIC CHECK FAILED:")
        for f in fails:
            print("  " + f)
        sys.exit(1)
    print(f"schematic check: {len(nets)} nets read from KiCad's netlist; "
          f"{len(groups)} generator nets and {len(SPEC)} independent spec groups match; "
          f"{len(SPEC_NC)} pins confirmed unconnected")


if __name__ == "__main__":
    main()
