#!/bin/sh
# Host checks for the F1 bench. Run from anywhere:  sh host/run.sh
#   1. Klaus Dormann's functional test [S5]
#   2. bus-identical to perfect6502, cycle by cycle [S4]
#   3. the golden fingerprints the Pico must reproduce (FEASIBILITY.md)
set -e
cd "$(dirname "$0")"
KLAUS=${KLAUS_HEX:-$HOME/tools/tests/klaus.hex}
gcc -O2 -Wall -Wextra -o cpu_test cpu_test.c
gcc -O2 -Wall -Wextra -o drive_test drive_test.c
(cd ../bench && mkdir -p build && acme -DTIMER=10000 -o build/wl_dos.bin workload.asm >/dev/null \
               && acme -DTIMER=150 -o build/wl_stress.bin workload.asm >/dev/null)
echo "--- Klaus Dormann functional test"
./cpu_test klaus "$KLAUS"
echo "--- perfect6502 bus comparison"
python3 -I diff.py --cases ${CASES:-400} --blocks 300 --seed ${SEED:-7} | tail -2
echo "--- golden fingerprints, 5,000,000 cycles"
for w in dos stress; do printf "%-7s" "$w"; ./drive_test ../bench/build/wl_$w.bin 5000000 | head -1; done
