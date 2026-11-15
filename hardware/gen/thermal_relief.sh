#!/bin/sh
# Thermal relief on EVERY pad of the GND pours -- an earlier board's finishing steps,
# ported whole.   sh gen/thermal_relief.sh [board]     (board.sh step 5c calls it)
#
# ⚠ THE PLANE TOOLS SILENTLY MAKE EVERY GND PAD SOLID. gen_pcb.py writes the pours with thermal
# relief; KRT's route_planes/repair_planes rewrite them to `connect_pads yes`, 0.2/0.2. Rev A was
# FABRICATED that way (2026-10-16): no ground pad had thermal relief, so hand-soldering them was
# near impossible. The sibling project had found and fixed exactly this; the step had not been ported.
# ⚠ EVERY PAD, not THT-only as on an earlier board: Rev B's prototype is hand-soldered,
# SMD included; relief does not stop JLC reflowing it.
# ⚠ THE ORDER IS THE TRICK (sibling): the relief gaps carve the pour into islands, a spoke that
# reaches only an island is a pad with no ground (starved_thermal). So: relief, repair the islands
# it made, clear any via the repair planted in a pad, relief again, repair again, clear again, fill.
set -e
cd "$(dirname "$0")/.."
BRD=${1:-picod64.kicad_pcb}
KRT=${KRT:-$HOME/tools/KiCadRoutingTools}/py_router
LOG=${LOG:-/tmp/picod64-thermal.$$}
export KICAD_RIP_PREEXISTING=0
GAP=0.3; SPOKE=0.4

relief() {
    python3 - "$BRD" "$GAP" "$SPOKE" <<'PY'
import pcbnew, sys
b = pcbnew.LoadBoard(sys.argv[1]); gap, spoke = float(sys.argv[2]), float(sys.argv[3])
n = 0
for z in b.Zones():
    if z.GetNetname() == "GND" and not z.GetIsRuleArea():
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetThermalReliefGap(pcbnew.FromMM(gap))
        z.SetThermalReliefSpokeWidth(pcbnew.FromMM(spoke))
        n += 1
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(sys.argv[1], b)
print(f"   {n} GND pour(s): every pad -> thermal relief, gap {gap}, spokes {spoke}")
PY
}
repair() {
    python3 $KRT/repair_planes.py "$BRD" --overwrite \
        --nets GND GND --plane-layers F.Cu B.Cu --layers F.Cu B.Cu \
        --via-size 0.6 --via-drill 0.3 --track-width 0.25 --clearance 0.22 \
        --no-repair-pads --fab-overrides gen/fab_floor.txt > $LOG.$1 2>&1 || true
    grep -oE '"remaining": [0-9]+' $LOG.$1 | tail -1 | sed "s/^/   islands after $1: /"
    python3 gen/remove_vias_in_pads.py "$BRD" | sed 's/^/   /'
}
relief
repair first
relief
repair second
relief
python3 gen/project.py | sed 's/^/   /'     # the plane tools rewrite the rules; put them back
python3 - "$BRD" <<'PY'
import pcbnew, sys
b = pcbnew.LoadBoard(sys.argv[1])
bad = [z.GetNetname() for z in b.Zones() if z.GetNetname() == "GND" and not z.GetIsRuleArea()
       and z.GetPadConnection() != pcbnew.ZONE_CONNECTION_THERMAL]
if bad:
    sys.exit(f"   *** {len(bad)} GND pour(s) not on thermal relief at the end")
print("   GND pours end on thermal relief")
PY
