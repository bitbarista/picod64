#!/bin/sh
# Assemble the C64 side of serial O (line timing). linetime.prg goes on the card (any folder).
#   sh fw/linetime/build.sh
set -e
cd "$(dirname "$0")"
acme linetime.asm
echo "linetime.prg: $(wc -c < linetime.prg) bytes"
