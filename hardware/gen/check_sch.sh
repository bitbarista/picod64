#!/bin/sh
# Regenerate the schematic and prove it: ERC clean, connectivity = design,
# and the connectivity check itself can still fail. One command, one verdict.
#   sh hardware/gen/check_sch.sh
set -e
cd "$(dirname "$0")/.."
python3 gen/gen_footprints.py >/dev/null
python3 gen/gen_sch.py
kicad-cli sch erc --severity-all -o erc.rpt picod64.kicad_sch >/dev/null
# ⚠ Read the count from the report itself -- not an exit status through a pipe.
errs=$(sed -n 's/.*Errors \([0-9]*\).*/\1/p' erc.rpt)
warns=$(sed -n 's/.*Warnings \([0-9]*\).*/\1/p' erc.rpt)
[ -n "$errs" ] || { echo "could not read the ERC report"; exit 1; }
[ "$errs" -eq 0 ] && [ "$warns" -eq 0 ] || { echo "ERC: $errs errors, $warns warnings -- see hardware/erc.rpt"; exit 1; }
echo "ERC: 0 errors, 0 warnings"
python3 gen/check_sch.py
python3 gen/test_check_can_fail.py
kicad-cli sch export pdf -o picod64-sch.pdf picod64.kicad_sch >/dev/null
echo "OK -- schematic regenerated and verified; PDF at hardware/picod64-sch.pdf"
