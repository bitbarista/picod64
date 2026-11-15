#!/usr/bin/env python3
"""BOM upload files for one board, EVERY part (optional ones too: the first boards test the
whole design), from fab/picod64-bom.csv (gen/fab.py):

  fab/upload-farnell.csv   for uk.farnell.com/bom-tool (map Quantity + Farnell Order Code)
  fab/upload-farnell-5.csv the same for five boards
  fab/upload-lcsc-untested.csv  for lcsc.com/bom, never ordered (map Quantity + LCSC Part Number)

LCSC does not carry everything Farnell does; LCSC[] gives the LCSC part for each Farnell MPN,
the same part where LCSC has it in stock, else an equivalent (marked "ALT"), else None (buy
elsewhere: listed at the end of the LCSC file). Codes and stock checked 2026-10-30.
"""
import csv, math, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
FAB = os.path.join(HERE, "..", "fab")

# Farnell MPN -> (LCSC code, LCSC MPN, maker, note)
LCSC = {
    "SC1631": None,                                  # Pico 2: not on LCSC (Pi Hut, Pimoroni, Farnell)
    "0105 06": None,                                 # Lumberg DIN: not on LCSC; SDF-60J (C6802381) out of stock
    "MEM2075-00-140-01-A": ("C3029735", "MEM2075-00-140-01-A", "GCT", ""),
    "EC11E15244G1": ("C370970", "EC11E15244G1", "Alps Alpine", ""),
    "2N3904BU": ("C124482", "2N3904BU", "onsemi", ""),
    "L-934GD": ("C598459", "L-934GD", "Kingbright", ""),
    "L-934ID": ("C598462", "L-934ID", "Kingbright", "low stock (10 on 2026-10-30)"),
    "MCGPR50V106M5X11": ("C88726", "50YXF10MEFC5X11", "Rubycon", "ALT: 10 uF 50 V 5 x 11, 2.0 mm pitch"),
    "K104K15X7RF53L2": ("C1620173", "K104K15X7RF53L2", "Vishay", ""),
    "K103K15X7RF53L2": ("C1620203", "K103K15X7RF53L2", "Vishay", ""),
    "MF25 6K8": ("C58600", "MFR0W4F6801A50", "Uni-Royal", "ALT: metal film 1/4 W 1 %"),
    "MF25 10K": ("C57436", "MFR0W4F1002A50", "Uni-Royal", "ALT: metal film 1/4 W 1 %"),
    "MF25 680R": ("C58599", "MFR0W4F6800A50", "Uni-Royal", "ALT: metal film 1/4 W 1 %"),
    "MF25 470R": ("C58592", "MFR0W4F4700A50", "Uni-Royal", "ALT: metal film 1/4 W 1 %"),
    "MF25 4K7": ("C57204", "MFR0W4F4701A50", "Uni-Royal", "ALT: metal film 1/4 W 1 %"),
    "MF25 100R": ("C57438", "MFR0W4F1000A50", "Uni-Royal", "ALT: metal film 1/4 W 1 %"),
    "4609X-101-103LF": ("C840660", "4609X-101-103LF", "Bourns", ""),
    "4605X-101-103LF": ("C840666", "4605X-101-103LF", "Bourns", ""),
    "4605X-101-102LF": ("C11389", "A05-102JP", "FH", "ALT: 1k x 4 bussed SIP-5, 5 %; type A = pin 1 common, as Bourns"),
    "1N5819-E3/54": ("C145202", "1N5819-E3/54", "Vishay", ""),
    "PS1240P02BT": ("C76871", "PS1240P02BT", "TDK", ""),
    "61300411821": ("C5380967", "M20-7820446", "Harwin", "ALT: Würth not on LCSC; same 8.5 mm 1 x 4 socket"),
    "61304011121": ("C2337", "2.54-1*40P", "BOOMELE", "ALT: 1 x 40 breakaway strip; cut J5 JP1 JP2 J6 J7 from it"),
    "M7686-46": ("C3715628", "QPC02SXGN-RC", "Sullins", "ALT: 2.54 mm jumper cap"),
}


def qty(s):
    m = re.match(r"\(?(\d+)\)?", s or "")
    return int(m.group(1)) if m else 0


rows = []   # (qty, refs, desc, maker, mpn, farnell)
with open(os.path.join(FAB, "picod64-bom.csv"), newline="", encoding="utf-8") as f:
    for r in csv.DictReader(f):
        if not r["MPN"] or not r["Farnell UK"]:
            continue                                  # printed parts, screws, OLED: not ordered here
        rows.append((qty(r["Qty"]), r["Refs"], r["Description"], r["Manufacturer"], r["MPN"], r["Farnell UK"]))

# one line per order code (headers, 1N5819, 2N3904, resistors appear on several BOM lines)
merged = {}
for q, refs, desc, mk, mpn, code in rows:
    m = merged.setdefault(code, [0, [], desc, mk, mpn])
    m[0] += q
    m[1].append(refs)

STRIP_PINS = 3 + 3 + 3 + 2 + 5        # J5 JP1 JP2 J6 J7, cut from the 40-pin strip (2356175)

# Every order code must find ONE product in Farnell search: the BOM tool matches it as text
# (9341951 and 150411 also hit other products: "Multiple Matches", 2026-10-30).
# Farnell minimum order / order multiple, read off uk.farnell.com 2026-10-30 (2895610: the quote
# sold 1, cut tape). Quantities are rounded up to these, so the file asks for what is charged.
MOQ = {
    "4531086": (1, 1), "9451382": (1, 1), "1141775": (10, 10), "1141772": (10, 10),
    "1142502": (5, 5), "1142517": (5, 5), "2677786": (1, 1), "2895610": (1, 1),
    "2827888": (10, 10), "1700648": (5, 5), "9342168": (10, 10), "9341943": (10, 10),
    "9342150": (10, 10), "9341110": (10, 10), "9356819": (1, 1), "9356061": (1, 1),
    "2064986": (1, 1), "3267212": (5, 5), "1336533": (5, 5), "9341099": (10, 10),
    "9341951": (10, 10), "9356053": (1, 1), "2356175": (1, 1), "3756818": (10, 10),
}


def farnell(name, boards):
    # ONLY quantity, order code and our reference, in ASCII: with the MPN column present the BOM
    # tool also matched "MF25 4K7" and "M7566-05" against longer part numbers ("Multiple
    # Matches", 2026-10-30)
    with open(os.path.join(FAB, name), "w", newline="", encoding="ascii") as f:
        w = csv.writer(f)
        w.writerow(["Quantity", "Farnell Order Code", "Customer Reference"])
        for code, (q, refs, desc, mk, mpn) in merged.items():
            n = math.ceil(STRIP_PINS * boards / 40) if code == "2356175" else q * boards
            if code not in MOQ:
                raise SystemExit(f"*** no Farnell minimum for {code} ({mpn}): add it to MOQ")
            lo, mult = MOQ[code]
            n = max(lo, math.ceil(n / mult) * mult)
            # refs only: the tool may match any of the first 20 columns, and an MPN here would be matched too
            ref = "J5 JP1 JP2 J6 J7 cut from strip" if code == "2356175" else " ".join(refs).replace("(off board)", "jumper caps JP1 JP2")
            w.writerow([n, code, ref + (f" x{boards} boards" if boards > 1 else "")])


farnell("upload-farnell.csv", 1)
farnell("upload-farnell-5.csv", 5)

missing = []
lc = {}
for code, (q, refs, desc, mk, mpn) in merged.items():
    if mpn not in LCSC:
        raise SystemExit(f"*** no LCSC decision for {mpn}: add it to LCSC[]")
    e = LCSC[mpn]
    if e is None:
        missing.append((q, mpn, mk, desc, " ".join(refs), code))
        continue
    m = lc.setdefault(e[0], [0, [], e[1], e[2], desc, e[3]])
    m[0] += q
    m[1].append(" ".join(refs))

with open(os.path.join(FAB, "upload-lcsc-untested.csv"), "w", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    w.writerow(["Quantity", "LCSC Part Number", "Manufacture Part Number", "Manufacturer", "Description", "Customer Reference", "Note"])
    for c, (q, refs, mpn, mk, desc, note) in lc.items():
        w.writerow([q, c, mpn, mk, desc, " ".join(refs), note])

print(f"Farnell: {len(merged)} lines, {sum(v[0] for v in merged.values())} parts")
print(f"LCSC:    {len(lc)} lines, {sum(v[0] for v in lc.values())} parts; not on LCSC:")
for q, mpn, mk, desc, refs, code in missing:
    print(f"  {q} x {mk} {mpn} ({refs}) -- Farnell {code}")
