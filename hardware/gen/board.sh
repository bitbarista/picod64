#!/usr/bin/env bash
# =============================================================================
# gen/board.sh -- build and route the PicoD64 PCB with FREEROUTING.
# Its gates:
# the cartridge-only steps (edge-finger gate, sim/board) dropped.
#
# ⚠⚠ THE ROUTER IS FREEROUTING. NEVER KiCadRoutingTools route.py FOR SIGNALS.
# KRT's output was rejected on earlier boards and this one (2026-10-09): it draws 3:4 / 2:5
# slopes and short segments. This script follows
# KRT appears below ONLY to close a net
# freerouting left open, with --max-ripup 0 so it can add copper but never tear
# freerouting's apart -- the one job that sibling allows it.
#
# Steps: schematic + checks -> footprints -> floorplan -> DSN -> freerouting ->
# SES -> (close leftovers) -> stitch + fill -> DRC + metrics.
# =============================================================================
set -e
cd "$(dirname "$0")/.."
BRD=picod64.kicad_pcb
WORK=${WORK:-/tmp/picod64-pcb}; mkdir -p "$WORK"; LOG=$WORK/log
JAVA=${JAVA:-$HOME/tools/jdk-25.0.4+7/bin/java}          # the jar needs Java 25; /usr/bin/java is 21
FREEROUTING=${FREEROUTING:-$HOME/tools/freerouting.jar}
KRT=${KRT:-$HOME/tools/KiCadRoutingTools}/py_router
export KICAD_RIP_PREEXISTING=0

# ⚠ keep the STATUS, not the tail: `cmd | tail` reports tail's exit status.
_step() {
    _what="$1"; shift
    # ⚠ under set -e a failing `x=$(cmd)` exits on the spot, BEFORE the message below
    # (2026-10-29: the build stopped silently at "label spots"): test it in an if
    if _out=$("$@" 2>&1); then _rc=0; else _rc=$?; fi
    printf '%s\n' "$_out" | tail -3 | sed 's/^/   /'
    [ "$_rc" -eq 0 ] || { echo "   *** $_what FAILED (exit $_rc) -- stopping" >&2; exit "$_rc"; }
}

echo "== 1. schematic: generate, ERC, connectivity"
_step "schematic" python3 gen/gen_sch.py
kicad-cli sch erc --severity-all -o erc.rpt picod64.kicad_sch >/dev/null
grep -q "ERC messages: 0 " erc.rpt || { echo "   *** ERC not clean -- see hardware/erc.rpt"; exit 1; }
echo "   ERC: 0 errors, 0 warnings"
_step "connectivity" python3 gen/check_sch.py

echo "== 2. footprints + floorplan (unrouted)"
_step "footprints" python3 gen/gen_footprints.py
_step "floorplan" python3 gen/gen_pcb.py
_step "case dimensions" python3 gen/gen_case_dims.py
_step "project rules" python3 gen/project.py
# Every SMD GND pad (both sides) gets a locked stub + escape via BEFORE the DSN, so the
# router routes around a path to ground instead of walling the pad in (as learned on an earlier board).
_step "GND escape vias" python3 gen/fanout_gnd_pads.py "$BRD"
_step "fixed tracks" python3 gen/fixed_tracks.py "$BRD"
_step "label spots" python3 gen/label_keepouts.py "$BRD"

# ⚠ FREEROUTING IS NOT DETERMINISTIC ENOUGH TO TRUST ONE RUN,
# measured there on Rev D: identical floorplans left different nets open, and its "0 unrouted"
# did not always match KiCad. So, as there: up to ROUTE_TRIES attempts from the saved
# floorplan; each attempt's leftovers go to KRT with --max-ripup 0; the first attempt that
# leaves NO signal net open is kept. If none does, stop -- never finish an unrouted board, and
# never move parts to suit the router (memory: never-move-approved-layout-for-the-tool).
ROUTE_TRIES=${ROUTE_TRIES:-4}
cp "$BRD" $WORK/floorplan.kicad_pcb
ATTEMPT=1
open_nets() {    # signal nets KiCad reports open (GND is the pour's job, step 4b)
    kicad-cli pcb drc --severity-error -o "$1" "$BRD" >/dev/null 2>&1 || true
    python3 - "$1" <<'FIND'
import re, sys
nets = set()
for blk in re.split(r"\[unconnected_items\]", open(sys.argv[1]).read())[1:]:
    for _, n in re.findall(r"@\([^)]*\): (.+?) \[([^\]]*)\]", blk.split("\n[")[0]):
        if n and n != "GND":
            nets.add(n)
print(" ".join(sorted(nets)))
FIND
}
while :; do
cp $WORK/floorplan.kicad_pcb "$BRD"
python3 gen/project.py --route-margin >/dev/null   # the rules the DSN carries, every attempt: 0.21 for the router
python3 gen/gnd_pad_rings.py "$BRD" add            # spoke room round THT GND pads, for the route only
rm -f $WORK/board.dsn $WORK/board.ses           # a stale session must never pass the -s test
echo "== 3. DSN -> freerouting -> SES (attempt $ATTEMPT of $ROUTE_TRIES)"
python3 - "$BRD" "$WORK/board.dsn" <<'DSN'
import pcbnew, sys
b = pcbnew.LoadBoard(sys.argv[1])
if not pcbnew.ExportSpecctraDSN(b, sys.argv[2]):
    sys.exit("DSN export failed")
print("   exported DSN")
DSN
# -Djava.awt.headless=true: otherwise an exception opens a modal dialog and it hangs.
# -mp 100: early passes are much worse; it settles late. -Xmx4g: caps the heap.
"$JAVA" -Xmx4g -Djava.awt.headless=true -jar "$FREEROUTING" \
    -de $WORK/board.dsn -do $WORK/board.ses -mp 100 > $LOG.fr 2>&1 || true
grep -oE "final score: [0-9.]+ \([0-9]+ unrouted and [0-9]+ violations\)" $LOG.fr | tail -1 | sed 's/^/   /'
[ -s $WORK/board.ses ] || { echo "   *** NO SESSION FILE -- freerouting did not finish; see $LOG.fr"; exit 1; }
python3 - "$BRD" "$WORK/board.ses" <<'SES'
import pcbnew, sys
b = pcbnew.LoadBoard(sys.argv[1])
if not pcbnew.ImportSpecctraSES(b, sys.argv[2]):
    sys.exit("SES import failed")
pcbnew.SaveBoard(sys.argv[1], b)
print("   imported SES")
SES
python3 gen/project.py >/dev/null               # back to the 0.2 mm the board is checked at
python3 gen/gnd_pad_rings.py "$BRD" remove

echo "== 4. anything freerouting left open"
OPEN=$(open_nets $WORK/open.rpt)
if [ -n "$OPEN" ]; then
    echo "   open: $OPEN -- closing with KRT, --max-ripup 0 (adds copper, never rips)"
    python3 $KRT/route.py $BRD --output $BRD --keep-input-copper --nets $OPEN \
        --layers F.Cu B.Cu --fab-overrides gen/fab_floor.txt \
        --track-width 0.25 --clearance 0.22 --via-size 0.6 --via-drill 0.3 \
        --max-ripup 0 > $LOG.close 2>&1 || true
    rm -f routed.kicad_pro picod64.kicad_pro.bak 2>/dev/null
    python3 gen/project.py | sed 's/^/   /'      # KRT rewrites the rules: put them back
    grep -oE '"successful": [0-9]+, "failed": [0-9]+' $LOG.close | tail -1 | sed 's/^/   /'
else
    echo "   nothing open"
fi
STILL=$(open_nets $WORK/open2.rpt)
# ⚠ freerouting never puts a via in a pad; KRT's close can (A1 into U2.17, 2026-10-15). A
# SIGNAL via in a pad cannot be "removed" -- it is the connection -- so that attempt has failed.
if [ -z "$STILL" ] && ! python3 gen/fix_via_in_pad.py "$BRD" --check >/dev/null 2>&1; then
    STILL="(a via inside a pad, from the KRT close)"
fi
if [ -z "$STILL" ]; then
    echo "   attempt $ATTEMPT: every signal net routed, no via in a pad"
    break
fi
echo "   attempt $ATTEMPT: still open: $STILL"
if [ "$ATTEMPT" -ge "$ROUTE_TRIES" ]; then
    echo "   *** $ROUTE_TRIES attempts and still open: $STILL -- stopping. Not a reason to move parts."
    exit 1
fi
ATTEMPT=$((ATTEMPT + 1))
done

echo "== 4b. GND: tap pads to the pour, tie islands -- the OTHER job KRT is allowed"
# freerouting treats GND as a plane and
# calls it connected; the real fill is not -- on the first clean build three F.Cu
# fragments under the Pico (pads 23, 28, 38) were islands. These tools only ADD
# GND copper; they do not route signals. --no-repair-pads: its pad-level pass
# plants vias INSIDE pads. KICAD_RIP_PREEXISTING=0 (exported above): repair_planes
# otherwise rips finished signal nets to land a strap.
python3 $KRT/route_planes.py "$BRD" --overwrite \
    --nets GND GND --plane-layers F.Cu B.Cu --layers F.Cu B.Cu \
    --via-size 0.6 --via-drill 0.3 --track-width 0.25 \
    --same-net-pad-clearance 0.1 --clearance 0.22 --min-thickness 0.25 \
    --fab-overrides gen/fab_floor.txt > $LOG.tap 2>&1 || true
grep -oE "[0-9]+/[0-9]+ pads connected to plane on [A-Za-z.]+" $LOG.tap | tail -2 | sed 's/^/   /'
python3 $KRT/repair_planes.py "$BRD" --overwrite \
    --nets GND GND --plane-layers F.Cu B.Cu --layers F.Cu B.Cu \
    --via-size 0.6 --via-drill 0.3 --track-width 0.25 --clearance 0.22 \
    --no-repair-pads --fab-overrides gen/fab_floor.txt > $LOG.repair 2>&1 || true
grep -oE '"remaining": [0-9]+' $LOG.repair | tail -1 | sed 's/^/   islands /'
rm -f routed.kicad_pro 2>/dev/null
python3 gen/project.py | sed 's/^/   /'          # plane tools may rewrite the rules too
# ⚠ KRT's plane tools moved JP3's copper bridge to F.SilkS (rev 1): put every footprint
# graphic back where the floorplan had it, and prove it again just before DRC
_step "footprint graphics back on their layers" python3 gen/restore_fp_layers.py "$BRD" $WORK/floorplan.kicad_pcb

echo "== 5. stitch + fill"
_step "stitch" python3 gen/stitch.py "$BRD"

echo "== 5-. no via in a pad: fix what the routers left, then prove none remain"
# remove_vias_in_pads.py, which re-checks the copper and
# reconnects any pad it cut (or stops). fix_via_in_pad.py is BANNED there for MOVING vias -- it
# "damaged the board on every attempt" -- so here it only CHECKS, never fixes.
_step "GND vias out of pads" python3 gen/remove_vias_in_pads.py "$BRD"
# a removed via can leave a stub hanging (a 0.14 mm B.Cu end at Pico pad 18, 2026-10-15):
# stitch.py's first job is exactly that, and it adds stitches only where both pours fill
_step "tidy after the removal" python3 gen/stitch.py "$BRD"
# Every via AND track up to the floor. Here freerouting
# itself necks tracks down into pins (0.1874 mm) and KRT's plane tools emit small vias.
_step "copper to the floor" python3 gen/fix_undersized_copper.py "$BRD" --apply
_step "dangling copper" python3 gen/tidy_gnd_dangles.py "$BRD"
_step "via-in-pad gate" python3 gen/fix_via_in_pad.py "$BRD" --check

echo "== 5c. thermal relief on every GND pad (the plane tools made them solid; Rev A was built so)"
_step "thermal relief" sh gen/thermal_relief.sh "$BRD"
_step "tie via-less GND fragments" python3 gen/tie_islands.py "$BRD"
_step "copper to the floor, after the repair" python3 gen/fix_undersized_copper.py "$BRD" --apply
_step "via-in-pad gate, after the repair" python3 gen/fix_via_in_pad.py "$BRD" --check

echo "== 5a. silkscreen designators -- from KiCad's own extents; fails rather than force one"
_step "silk" python3 gen/silk.py "$BRD"

echo "== 6. DRC (with schematic parity) and the metrics to review"
_step "footprint graphics gate" python3 gen/restore_fp_layers.py "$BRD" $WORK/floorplan.kicad_pcb --check
mkdir -p reports
_step "rules intact" python3 gen/project.py --check
kicad-cli pcb drc --severity-all --schematic-parity -o reports/drc.rpt "$BRD" >/dev/null 2>&1 || true
grep -E "^\*\* Found" reports/drc.rpt | sed 's/^/   /'
grep -oE "^\[[a-z_]+\]" reports/drc.rpt | sort | uniq -c | sort -rn | sed 's/^/   /'
# ⚠ THE COUNTS GATE, not just print. Until 2026-10-11 this step only echoed them,
# so a violation scrolled past and the script still said "done". Three "Found 0"
# lines or stop -- a missing report (DRC crashed) is a stop too.
[ "$(grep -cE '^\*\* Found 0 ' reports/drc.rpt)" -eq 3 ] || {
    # a starved thermal is the route's luck (D1.1 between +3V3 and DATA_IN, 2026-10-25):
    # never force the pad solid (relief on EVERY GND pad) -- route again, from scratch
    BUILD_TRY=${BUILD_TRY:-1}
    if ! grep -oE "^\[[a-z_]+\]" reports/drc.rpt | grep -qv starved_thermal \
            && [ "$BUILD_TRY" -lt "${BUILD_TRIES:-3}" ]; then
        echo "   only starved thermals: whole build again (try $((BUILD_TRY + 1)))"
        BUILD_TRY=$((BUILD_TRY + 1)) exec bash gen/board.sh
    fi
    echo "   *** DRC is not clean (or did not run) -- stopping" >&2; exit 1; }
# ⚠ AND EVERY PAD ON ONE PIECE OF ITS NET (a gate from an earlier board's errata: one passed
# DRC with seven GND pads on isolated pour fragments -- KiCad names only the zones).
_step "pad connectivity" python3 gen/check_pad_connectivity.py "$BRD"
python3 - "$BRD" <<'METRICS'
import pcbnew, statistics, sys, math
b = pcbnew.LoadBoard(sys.argv[1])
segs = [t for t in b.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T]
vias = [t for t in b.GetTracks() if t.Type() == pcbnew.PCB_VIA_T]
L = [t.GetLength() / 1e6 for t in segs]
off45 = 0
for t in segs:
    dx, dy = t.GetEnd().x - t.GetStart().x, t.GetEnd().y - t.GetStart().y
    a = math.degrees(math.atan2(dy, dx)) % 45
    if min(a, 45 - a) > 0.5:
        off45 += 1
print(f"   {len(segs)} segments, median {statistics.median(L):.2f} mm, "
      f"{off45} not on 0/45/90 deg; {len(vias)} vias")
METRICS
echo "== done. Look at the render before believing any of this."
