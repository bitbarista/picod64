#!/bin/sh
# Serial O (line timing) on the host: fw/linetime/linetime.prg on the host C64 against a model of PicoD64
# with known delays, through fw/linetime.h's sweep. (The board's pin loop needs the board.)
#   sh host/linetime.sh
set -e
cd "$(dirname "$0")"
K=${KERNAL:-$HOME/.local/share/vice/C64/kernal-901227-03.bin}
B=${BASIC:-$HOME/.local/share/vice/C64/basic-901226-01.bin}
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
(cd ../fw/linetime && acme -f cbm -o "$W/linetime.prg" linetime.asm 2>/dev/null)
cmp -s "$W/linetime.prg" ../fw/linetime/linetime.prg || { echo "FAIL  fw/linetime/linetime.prg is stale: sh fw/linetime/build.sh"; exit 1; }
gcc -O2 -Wall -Wextra -o "$W/lt" linetime_test.c c64.c -lm
"$W/lt" "$K" "$B" ../fw/linetime/linetime.prg
