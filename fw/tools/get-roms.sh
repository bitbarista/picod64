#!/bin/bash
# Put the ROMs PicoD64 needs on its SD card, from the zimmers.net Commodore firmware archive. They are
# Commodore's, so PicoD64 does not include them.
#   bash fw/tools/get-roms.sh /media/you/SDCARD
#   dos1541.bin  the 1541 DOS: 325302-01 ($C000) + 901229-05 ($E000), joined in that order. Required.
#                This exact ROM is the one PicoD64 is tested against (MD5 below).
set -e
DEST=${1:?usage: get-roms.sh <SD card folder>}
[ -d "$DEST" ] || { echo "no such folder: $DEST"; exit 1; }
Z=https://www.zimmers.net/anonftp/pub/cbm/firmware
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
curl -fsSL -o "$T/c000.bin" "$Z/drives/new/1541/1541-c000.325302-01.bin"
curl -fsSL -o "$T/e000.bin" "$Z/drives/new/1541/1541-e000.901229-05.bin"
cat "$T/c000.bin" "$T/e000.bin" > "$T/dos1541.bin"
echo "a0ce8439d1b8dcf2e1430461f7233a72  $T/dos1541.bin" | md5sum -c --quiet - || { echo "dos1541.bin: MD5 differs from the tested ROM"; exit 1; }
cp "$T/dos1541.bin" "$DEST/"
echo "dos1541.bin (16,384 bytes, MD5 checked) written to $DEST"
