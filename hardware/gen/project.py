#!/usr/bin/env python3
"""
Write picod64.kicad_pro's design rules from ONE place.

⚠ Re-run before the DSN, after ANY tool touches the board, and before DRC.
KRT's route.py rewrites the project's DRC floors "to match the routed floors";
on the first freerouting build it lowered min track width to 0.1874 mm, and a
0.19 mm segment then passed DRC unremarked. An earlier board's board.sh pins its
netclass inside its routing loop for exactly this reason.
--check: exit 1 if the file no longer holds these values (used just before DRC).
"""
import json, os, sys

HW = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PRO = os.path.join(HW, "picod64.kicad_pro")
RULES = {"min_clearance": 0.2, "min_track_width": 0.25, "min_via_diameter": 0.6,
         "min_through_hole_diameter": 0.3, "min_via_drill": 0.3, "min_via_annular_width": 0.15,
         "min_copper_edge_clearance": 0.5, "min_hole_to_hole": 0.25, "min_hole_clearance": 0.25}
CLASS = {"clearance": 0.2, "track_width": 0.25, "via_diameter": 0.6, "via_drill": 0.3}
# --route-margin: the netclass the DSN carries asks freerouting for 0.203 mm, so its geometry
# rounds INSIDE the 0.2 mm rule DRC checks. Rev B's first clean route had a track 0.1979 mm
# from a pad -- freerouting routes AT the clearance, and 2 um of rounding is a DRC error.
# The board's own rule (min_clearance) stays 0.2; board.sh restores the class after the SES.
if "--route-margin" in sys.argv:
    CLASS = dict(CLASS, clearance=0.203)   # 0.21 left A4 and D6 unroutable on 4 of 4 attempts

d = json.load(open(PRO)) if os.path.exists(PRO) else {"meta": {"filename": os.path.basename(PRO), "version": 3}}
if "--check" in sys.argv:
    r = d.get("board", {}).get("design_settings", {}).get("rules", {})
    bad = {k: (r.get(k), v) for k, v in RULES.items() if r.get(k) != v}
    for c in d.get("net_settings", {}).get("classes", []):
        bad.update({f"{c['name']}.{k}": (c.get(k), v) for k, v in CLASS.items() if c.get(k) != v})
    if bad:
        sys.exit(f"project rules were altered: {bad}")
    print("project rules intact: clearance 0.2, track 0.25, via 0.6/0.3, edge 0.5")
    sys.exit(0)
d.setdefault("board", {}).setdefault("design_settings", {}).setdefault("rules", {}).update(RULES)
ns = d.setdefault("net_settings", {"meta": {"version": 4}})
classes = ns.setdefault("classes", [])
if not classes:
    classes.append({"name": "Default", "priority": 2147483647, "wire_width": 6, "bus_width": 12,
                    "line_style": 0, "microvia_diameter": 0.3, "microvia_drill": 0.1,
                    "diff_pair_width": 0.2, "diff_pair_gap": 0.25, "diff_pair_via_gap": 0.25,
                    "pcb_color": "rgba(0, 0, 0, 0.000)", "schematic_color": "rgba(0, 0, 0, 0.000)"})
for c in classes:
    c.update(CLASS)
json.dump(d, open(PRO, "w"), indent=2)
print(f"project rules pinned: clearance {CLASS['clearance']}, track 0.25, via 0.6/0.3, edge 0.5")
