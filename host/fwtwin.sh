#!/bin/sh
# The board firmware's host twin (host/fwtwin.c): fw/bus.h + fw/svc.c + fw/storage.c +
# FatFs on a FAT32 image, against the real C64 KERNAL. What f6.sh checks, through the
# code the board runs, plus SAVE written back to the card, the knob's mount / next disk /
# unmount, C64 reset, exFAT, and (FB64FL=...) FB64's fast loader with the C64 running
# during the replay.
#   sh host/fwtwin.sh
set -e
cd "$(dirname "$0")"
DOS=${DOS1541:-$HOME/src/vice-3.9/data/DRIVES/dos1541-325302-01+901229-05.bin}
K=${KERNAL:-$HOME/.local/share/vice/C64/kernal-901227-03.bin}
B=${BASIC:-$HOME/.local/share/vice/C64/basic-901226-01.bin}
W=$(mktemp -d)
[ -n "$FWKEEP" ] && echo "logs: $W" || trap 'rm -rf "$W"' EXIT
F="../fw/svc.c ../fw/storage.c ../fw/diskio_host.c ../fw/fatfs/ff.c ../fw/fatfs/ffunicode.c"
gcc -O2 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-stringop-truncation -o "$W/fwtwin" fwtwin.c c64.c $F
gcc -O2 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -o "$W/st" fw_storage_test.c $F
gcc -O2 -Wall -Wextra -Wno-unused-function -o "$W/ds" fw_diskset_test.c ../fw/diskset.c
OK=1
check() { if eval "$2"; then echo "ok    $1"; else echo "FAIL  $1"; OK=0; fi; }

mkdir -p "$W/sd/games" "$W/out" "$W/brout"
python3 -I - "$W" <<'EOF'
import sys, random
r = random.Random(9); w = sys.argv[1]
open(w + "/sd/hello.prg", "wb").write(bytes([1, 8]) + r.randbytes(1500))
open(w + "/sd/games/pong.prg", "wb").write(bytes([1, 8]) + r.randbytes(4000))
open(w + "/sd/games/maze.prg", "wb").write(bytes([1, 8]) + r.randbytes(300))
open(w + "/b.prg", "wb").write(bytes([1, 8]) + r.randbytes(508))
d = bytearray(r.randbytes(174848)); d[0x16500 + 162] = d[0x16500 + 163] = ord('F')
open(w + "/rand.d64", "wb").write(d)
EOF
c1541 -format "demo disk,dd" d64 "$W/sd/demo.d64" >/dev/null 2>&1
c1541 "$W/sd/demo.d64" -write "$W/b.prg" b508 >/dev/null 2>&1
c1541 "$W/sd/demo.d64" -read b508 "$W/ref_b508.prg" >/dev/null 2>&1
c1541 -format "first disk,a1" d64 "$W/g1.d64" >/dev/null 2>&1; c1541 "$W/g1.d64" -write "$W/b.prg" part-one >/dev/null 2>&1
c1541 -format "second disk,b2" d64 "$W/g2.d64" >/dev/null 2>&1; c1541 "$W/g2.d64" -write "$W/sd/games/maze.prg" part-two >/dev/null 2>&1

mkimg() {   # $1 image, $2 fat|exfat
  truncate -s 64M "$1"
  if [ "$2" = exfat ]; then /usr/sbin/mkfs.exfat "$1" >/dev/null 2>&1; else /usr/sbin/mkfs.vfat -F 32 "$1" >/dev/null; fi
  "$W/st" mkdir "$1" /games >/dev/null; "$W/st" mkdir "$1" /sets >/dev/null
  for f in hello.prg demo.d64; do "$W/st" put "$1" /$f "$W/sd/$f"; done
  for f in pong.prg maze.prg; do "$W/st" put "$1" /games/$f "$W/sd/games/$f"; done
  "$W/st" put "$1" /sets/Game-1.d64 "$W/g1.d64"; "$W/st" put "$1" /sets/Game-2.d64 "$W/g2.d64"
  "$W/st" put "$1" /rand.d64 "$W/rand.d64"
}
mkimg "$W/card.img" fat
cp "$W/card.img" "$W/sdcard.img"        # (before the storage test rewrites rand.d64 on card.img)
"$W/st" d64 "$W/card.img" /rand.d64 "$W/rand.d64" > "$W/st.log" || true
check "card: D64 streamed to GCR = d64_to_disk; tracks written back exactly" "grep -q PASS '$W/st.log'"
"$W/st" err "$W/card.img" /err.d64 "$W/rand.d64" > "$W/err.log" || true
check "card: D64 error bytes built into the GCR; a rewritten bad sector clears its byte; no table = as before" "grep -q PASS '$W/err.log'"
gcc -O2 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -o "$W/up" fw_update_test.c ../fw/update.c $F
truncate -s 64M "$W/up.img"; /usr/sbin/mkfs.vfat -F 32 "$W/up.img" >/dev/null
UF2=${UF2:-../fw/build/picod64.uf2}
if [ -f "$UF2" ]; then "$W/up" "$W/up.img" "$UF2" > "$W/up.log" || true
  check "update from the card: the same firmware renamed .done, a new one staged, 8 kinds of bad file refused untouched, the build's UF2 taken" "grep -q PASS '$W/up.log'"
else echo "(update from the card: skipped, no $UF2: bash fw/build.sh)"; fi
"$W/ds" > "$W/ds.log" || true
gcc -O2 -Wall -Wextra -o "$W/sk" fw_stack_test.c
check "stack high-water mark" "'$W/sk' | grep -q PASS"
check "next disk: sets and folder order (17 cases)" "grep -q PASS '$W/ds.log'"
gcc -O2 -w -o "$W/ui" fw_ui_test.c ../fw/ui.c ../fw/diskset.c $F
mkdir -p "$W/shots"; "$W/ui" "$W/card.img" "$W/shots/" > "$W/ui.log" || true
check "UI: knob decoding, press/hold, list names, screens over the card" "grep -q PASS '$W/ui.log'"
gcc -O2 -w -o "$W/sdt" fw_sd_test.c ../fw/sd_spi.c ../fw/storage.c ../fw/fatfs/ff.c ../fw/fatfs/ffunicode.c
"$W/sdt" "$W/sdcard.img" "$W/rand.d64" /rand.d64 > "$W/sd.log" || true
check "SD over SPI (simulated card, SDHC and SDSC): FatFs, D64 in and back" "grep -q PASS '$W/sd.log'"

"$W/fwtwin" "$DOS" "$K" "$B" "$W/card.img" "$W/out" \
  err: '$' load:hello.prg cmd:cd:games '$' load:pong.prg cmd:cd_ load:nosuch err: cmd:cd:demo.d64 '$' load:b508 save:newfile err: cmd:cd_ '$' \
  > "$W/a.log" 2>&1 || true
check "power-on error channel"            "grep -q '\"73,PICOD64 BROWSE,00,00|\"' '$W/a.log'"
check "root listing from the card"        "grep -q '\"GAMES\" *DIR' '$W/a.log' && grep -q '\"HELLO.PRG\"' '$W/a.log'"
check "hello.prg byte for byte"           "tail -c +3 '$W/sd/hello.prg' | cmp -s - '$W/out/3.bin'"
check "CD:games, pong.prg byte for byte"  "tail -c +3 '$W/sd/games/pong.prg' | cmp -s - '$W/out/6.bin'"
check "missing name: 62 FILE NOT FOUND"   "grep -q '\"62, FILE NOT FOUND,00,00|\"' '$W/a.log'"
check "mount: the real DOS lists the D64" "grep -q '\"DEMO DISK *\" DD 2A' '$W/a.log'"
check "mount: b508 identical to c1541"    "tail -c +3 '$W/ref_b508.prg' | cmp -s - '$W/out/12.bin'"
check "SAVE written back to the card"     "grep -q 'sectors written back to /demo.d64' '$W/a.log'"
"$W/st" get "$W/card.img" /demo.d64 "$W/after.d64"
c1541 "$W/after.d64" -read newfile "$W/newfile.prg" >/dev/null 2>&1 || true
python3 -I - "$W" > "$W/nf.log" <<'EOF'
import sys
w = sys.argv[1]; d = open(w + "/newfile.prg", "rb").read(); x = 77; e = bytearray()
for k in range(0x400):
    x = (x * 1103515245 + 12345) & 0xffffffff; e.append((x >> 16) & 255)
print("same" if d[:2] == b"\x00\x20" and d[2:] == bytes(e) else "differs")
EOF
check "... and c1541 reads it back exactly from the card's D64" "grep -q same '$W/nf.log'"
check "CD_: browse mode again"            "grep -q 'unmounted: browse mode' '$W/a.log' && [ \$(grep -c '\"PICOD64 *\" FF 2A' '$W/a.log') = 2 ]"

"$W/fwtwin" "$DOS" "$K" "$B" "$W/card.img" "$W/out" cmd:cd:games save:mysave save:mysave err: load:mysave.prg load:mysave > "$W/s.log" 2>&1 || true
"$W/st" get "$W/card.img" /games/mysave.prg "$W/mysave.prg" || true
python3 -I - "$W" > "$W/ms.log" <<'EOF'
import sys
w = sys.argv[1]; d = open(w + "/mysave.prg", "rb").read(); x = 77; e = bytearray()
for k in range(0x400):
    x = (x * 1103515245 + 12345) & 0xffffffff; e.append((x >> 16) & 255)
print("same" if d[:2] == b"\x00\x20" and d[2:] == bytes(e) else "differs %d" % len(d))
EOF
check "browse SAVE: the file on the card, byte for byte" "grep -q same '$W/ms.log'"
check "browse SAVE again: 63 FILE EXISTS"  "grep -q '\"63, FILE EXISTS,00,00|\"' '$W/s.log'"
check "... and it LOADs back"              "grep -q '^load:mysave.prg: done end=\$1400 .* err=0' '$W/s.log'"
check "... and by the name it was saved as (no .prg)" "grep -q '^load:mysave: done end=\$1400 .* err=0' '$W/s.log'"

"$W/fwtwin" "$DOS" "$K" "$B" "$W/card.img" "$W/out" \
  ui:mount:/sets/Game-1.d64 '$' ui:next:/sets/Game-2.d64 '$' load:part-two reset: '$' ui:unmount: '$' > "$W/b.log" 2>&1 || true
check "knob: mount disk 1"                "grep -q '\"FIRST DISK *\" A1 2A' '$W/b.log'"
check "knob: next disk, the DOS sees disk 2" "grep -q '\"SECOND DISK *\" B2 2A' '$W/b.log' && grep -q '^load:part-two: done .* err=0' '$W/b.log'"
check "C64 reset: the drive resets, keeps disk 2" "grep -q 'resets 1' '$W/b.log' && [ \$(grep -c '\"SECOND DISK *\" B2 2A' '$W/b.log') = 2 ]"
check "knob: unmount, browse in the set's folder" "grep -q '\"SETS *\" FF 2A' '$W/b.log'"

mkimg "$W/ex.img" exfat
"$W/fwtwin" "$DOS" "$K" "$B" "$W/ex.img" "$W/out" '$' load:hello.prg cmd:cd:demo.d64 '$' > "$W/c.log" 2>&1 || true
check "exFAT card: listing, load, mount"  "grep -q '\"HELLO.PRG\"' '$W/c.log' && tail -c +3 '$W/sd/hello.prg' | cmp -s - '$W/out/2.bin' && grep -q '\"DEMO DISK *\" DD 2A' '$W/c.log'"

if [ -n "$FB64FL" ] && [ -f "$FB64FL" ]; then
  python3 -I - "$W" <<'EOF'
import sys
w = sys.argv[1]
def basic(lines):
    out = bytearray([1, 8]); addr = 0x0801
    for num, body in lines:
        nxt = addr + 4 + len(body) + 1
        out += bytes([nxt & 255, nxt >> 8, num & 255, num >> 8]) + body + b'\0'; addr = nxt
    return out + b'\0\0'
open(w + "/hb.prg", "wb").write(basic([(10, bytes([0x99]) + b'"HELLO FROM PICOD64"')]))
EOF
  truncate -s 64M "$W/fb.img"; /usr/sbin/mkfs.vfat -F 32 "$W/fb.img" >/dev/null
  "$W/st" mkdir "$W/fb.img" /games >/dev/null
  "$W/st" put "$W/fb.img" /fbfl.prg "$FB64FL"; "$W/st" put "$W/fb.img" /hello.prg "$W/hb.prg"; "$W/st" put "$W/fb.img" /demo.d64 "$W/sd/demo.d64"
  "$W/fwtwin" "$DOS" "$K" "$B" "$W/fb.img" "$W/out" 'type:load"fbfl.prg",8|' wait:2 'type:run|' wait:6 'type:~~~~~' wait:1 'type:|' wait:20 screen: state: > "$W/d.log" 2>&1 || true   # (BROWSER, /games, demo.d64, fbfl.prg, hello.prg)
  check "FB64 FL: virtual D64 on the card, replay, its loader runs HELLO" "grep -q 'HELLO FROM PICOD64' '$W/d.log' && grep -q 'replays 1/0' '$W/d.log'"
else
  echo "(FB64 FL part skipped: set FB64FL=/path/to/fb20fl.prg)"
fi
# JiffyDOS on both sides (JIFFYK=C64 KERNAL, JIFFYD=1541 ROM, not bundled): every byte, and the timing window.
# A byte count isn't enough: this read the right number of wrong bytes for a while (AUDIT R36).
if [ -n "$JIFFYK" ] && [ -n "$JIFFYD" ] && [ -f "$JIFFYK" ] && [ -f "$JIFFYD" ]; then
  python3 -I -c "
import sys
open(sys.argv[1], 'wb').write(bytes([1, 8]) + bytes((i * 37 + 11) & 255 for i in range(25000)))" "$W/jp.prg"
  c1541 -format "jiffy,jd" d64 "$W/jp.d64" -attach "$W/jp.d64" -write "$W/jp.prg" pat >/dev/null 2>&1
  truncate -s 64M "$W/jd.img"; /usr/sbin/mkfs.vfat -F 32 "$W/jd.img" >/dev/null; "$W/st" put "$W/jd.img" /p.d64 "$W/jp.d64"
  jd() { FWDR=$1 "$W/fwtwin" "$JIFFYD" "$JIFFYK" "$B" "$W/jd.img" "$W/out" cmd:cd:p.d64 wait:2 load:pat c64mem:1000:25000 > "$W/j$1.log" 2>&1 || true
    python3 -I -c "
import re, sys
t = open(sys.argv[1], errors='ignore').read(); w = open(sys.argv[2], 'rb').read()[2:]
m = re.findall(r'C64 .1000:((?: [0-9A-F]{2})+)', t); g = bytes(int(x, 16) for x in m[-1].split()) if m else b''
s = re.findall(r'load:pat: done .* time=([0-9.]+)s', t)
sys.exit(0 if g == w and s and float(s[0]) < 15 else 1)" "$W/j$1.log" "$W/jp.prg"; }
  check "JiffyDOS both sides: every byte right, under 15 s" "jd 1"
  check "JiffyDOS both sides: right across the window (2, 3 cycles)" "jd 2 && jd 3"
  check "JiffyDOS both sides: this check can fail (0 cycles, 5 cycles)" "! jd 0 && ! jd 5"
else
  echo "(JiffyDOS part skipped: set JIFFYK= and JIFFYD=)"
fi

# FB64 Turbo (FB64T=/path/to/fb64-turbo.prg; CSDb 223021, DeeKay & TSM, not bundled): it "CDs into"
# a PRG and turbo-loads it with a cycle-timed loader that needs the drive's ATN gate to act at
# once (core/drive1541.h PLAT_IEC_GATE; 2026-10-30)
if [ -n "$FB64T" ] && [ -f "$FB64T" ]; then
  python3 -I - "$W" <<'EOF'
import sys
w = sys.argv[1]
def basic(lines):
    out = bytearray([1, 8]); addr = 0x0801
    for num, body in lines:
        nxt = addr + 4 + len(body) + 1
        out += bytes([nxt & 255, nxt >> 8, num & 255, num >> 8]) + body + b'\0'; addr = nxt
    return out + b'\0\0'
lines = [(i, bytes([0x8F]) + b' ' + b'X' * 80) for i in range(1, 121)] + [(1000, bytes([0x99]) + b'"BIG ONE LOADED OK"')]
open(w + "/big.prg", "wb").write(basic(lines))
EOF
  truncate -s 64M "$W/ft.img"; /usr/sbin/mkfs.vfat -F 32 "$W/ft.img" >/dev/null
  "$W/st" put "$W/ft.img" /fb64-turbo.prg "$FB64T"; "$W/st" put "$W/ft.img" /big.prg "$W/big.prg"; "$W/st" put "$W/ft.img" /demo.d64 "$W/sd/demo.d64"
  "$W/fwtwin" "$DOS" "$K" "$B" "$W/ft.img" "$W/out" 'type:load"fb64-turbo.prg",8|' wait:2 'type:run|' wait:6 'type:~~~' wait:1 'type:|' wait:6 'type:~~' wait:1 'type:|' wait:15 screen: > "$W/e.log" 2>&1 || true
  # (the list: //, BROWSER, big.prg, demo.d64, fb64-turbo.prg)
  check "FB64 Turbo: CD into a PRG, its turbo loads and runs it" "grep -q 'BIG ONE LOADED OK' '$W/e.log'"
  "$W/fwtwin" "$DOS" "$K" "$B" "$W/ft.img" "$W/out" 'type:load"fb64-turbo.prg",8|' wait:2 'type:run|' wait:6 'type:~~~~' wait:1 'type:|' wait:10 'type:~~' wait:1 'type:|' wait:15 c64mem:0801:8 > "$W/f.log" 2>&1 || true
  check "FB64 Turbo: from a D64 through the real DOS, the bytes right" "grep -q 'C64 \$0801: AB 1C 95 12 0E 87 91 21' '$W/f.log'"
else
  echo "(FB64 Turbo part skipped: set FB64T=/path/to/fb64-turbo.prg)"
fi
# The browser built into the firmware (fw/browser/): LOAD"*",8 at the root, as a user would
python3 -I - "$W" <<'EOP'
import sys
w = sys.argv[1]
def basic(text):
    body = bytes([0x99]) + b'"' + text + b'"'; nxt = 0x0801 + 4 + len(body) + 1
    return bytes([1, 8, nxt & 255, nxt >> 8, 10, 0]) + body + b'\0\0\0'
open(w + '/bh.prg', 'wb').write(basic(b'HELLO FROM PICOD64'))
open(w + '/bp.prg', 'wb').write(basic(b'PONG FROM THE GAMES FOLDER'))
open(w + '/bb.prg', 'wb').write(basic(b'B2 FROM THE D64'))
EOP
c1541 -format "demo disk,dd" d64 "$W/bd.d64" >/dev/null 2>&1; c1541 "$W/bd.d64" -write "$W/bb.prg" b2 >/dev/null 2>&1
truncate -s 64M "$W/br.img"; /usr/sbin/mkfs.vfat -F 32 "$W/br.img" >/dev/null
"$W/st" put "$W/br.img" /hello.prg "$W/bh.prg"; "$W/st" put "$W/br.img" /demo.d64 "$W/bd.d64"
"$W/st" mkdir "$W/br.img" /games; "$W/st" put "$W/br.img" /games/pong.prg "$W/bp.prg"
F1=$(printf '\205'); F3=$(printf '\206'); STOP=$(printf '\003')
cp --sparse=always "$W/br.img" "$W/br0.img"; cp --sparse=always "$W/br.img" "$W/brk.img"
br() { n=$1; shift; cp --sparse=always "$W/brk.img" "$W/br.img"; "$W/fwtwin" "$DOS" "$K" "$B" "$W/br.img" "$W/out" 'type:load"*",8|' wait:3 'type:run|' wait:4 "$@" screen: state: > "$W/$n.log" 2>&1 || true; }
br b1
check "browser: LOAD\"*\",8 and RUN, in a few seconds, show the card" "grep -q 'PicoD64  ' '$W/b1.log' && grep -q 'games' '$W/b1.log' && grep -q 'hello' '$W/b1.log'"
check "browser: long names and formats: no .prg or .d64 in the name, a column for them" "! grep -q 'hello.prg' '$W/b1.log' && grep -q 'hello .* PRG' '$W/b1.log' && grep -q 'demo .* D64' '$W/b1.log'"
check "browser: the footer says what RETURN does" "grep -q 'Return open' '$W/b1.log'"
"$W/fwtwin" "$DOS" "$K" "$B" "$W/br.img" "$W/brout" 'load:$' > "$W/b1d.log" 2>&1 || true
check "browser: LOAD\"\$\" lists it for other programs" "cat '$W'/brout/* | grep -a -q 'BROWSER'"
br b2 'type:g|' wait:3 'type:|' wait:5
check "browser: into a folder, the cursor on its first item, RETURN runs it" "grep -q 'PONG FROM THE GAMES FOLDER' '$W/b2.log'"
br b3 'type:g|' wait:3
check "browser: the path and position" "grep -q '/games  *1 of 1' '$W/b3.log' && grep -q 'Back' '$W/b3.log'"
br b4 'type:g|' wait:3 'type:<' wait:3
check "browser: DEL back, the cursor on the folder it left" "grep -q '/  *1 of 3' '$W/b4.log'"
br b5 'type:g|' wait:3 'type:^|' wait:3
check "browser: the BACK row goes back too" "grep -q '/  *1 of 3' '$W/b5.log'"
br b6 'type:hello|' wait:5
check "browser: typing filters, RETURN runs the match" "grep -q 'HELLO FROM PICOD64' '$W/b6.log'"
br b7 'type:zz' wait:1
check "browser: a filter with no match says so" "grep -q 'No match' '$W/b7.log'"
br b8 "type:g$STOP" wait:1
check "browser: RUN/STOP clears the filter" "grep -q 'Type to filter' '$W/b8.log' && grep -q '1 of 3' '$W/b8.log'"
br b9 "type:${F3}hello$F1~|" wait:6
check "browser: the menu's LOAD with the turbo off: loads, doesn't run" "grep -q 'SEARCHING FOR #' '$W/b9.log' && ! grep -q 'HELLO FROM' '$W/b9.log'"
br b11 "type:demo$F1~|" wait:12 peek:12 peek:13
check "browser: the menu's RUN FIRST" "grep -q 'B2 FROM THE D64' '$W/b11.log'"
check "browser: after RUN FIRST the drive has read the disk, as after a LOAD: its ID DD at \$12/\$13 (it loaded the file itself; sent from the image, a loader's DOS job got 29: Shards of Fancy, 2026-11-17)" "grep -q '\$0012 = \$44' '$W/b11.log' && grep -q '\$0013 = \$44' '$W/b11.log'"
br b14 'type:demo|' wait:6 'type:|' wait:8 peek:12 peek:13
check "browser: RETURN on a file in an opened D64 runs it, and the drive has read the disk" "grep -q 'B2 FROM THE D64' '$W/b14.log' && grep -q '\$0012 = \$44' '$W/b14.log' && grep -q '\$0013 = \$44' '$W/b14.log'"
br rfr "type:demo$F1~|" wait:12 reset: 'type:load"*",8|' wait:3 'type:run|' wait:5
check "browser: a reset takes out the disk RUN FIRST put in, LOAD\"*\" is the browser again (2026-11-07)" "grep -q 'PicoD64  ' '$W/rfr.log' && grep -q 'online 0' '$W/rfr.log'"
check "browser: RUN FIRST loads the plain way, through the drive, with the disk in (no turbo from a D64 since 2026-11-17)" "grep -q 'SEARCHING' '$W/b11.log' && grep -q 'mounted \"/demo.d64\"' '$W/b11.log'"
br b12 "type:demo$F1~~|" wait:6
check "browser: the menu's INSERT mounts, to BASIC" "grep -q 'mounted \"/demo.d64\"' '$W/b12.log' && ! grep -q 'Type to filter' '$W/b12.log'"
br b13 'type:demo|' wait:6 'type:<' wait:4
check "browser: DEL out of a D64 unmounts, the cursor on it" "grep -q 'online 0' '$W/b13.log' && grep -q '1 of 3\|2 of 3' '$W/b13.log'"
br rr 'type:g|' wait:3 'type:|' wait:5 reset: 'type:load"*",8|' wait:3 'type:run|' wait:5
check "C64 reset in a folder: LOAD\"*\",8 is the browser again, back in that folder (2026-11-07)" "grep -q 'PicoD64  ' '$W/rr.log' && grep -q '/games' '$W/rr.log'"
# Joystick 2: down moves, left is back, a tap of fire is RETURN, a held fire is the menu
br j1 joy:2 wait:0.1 joy:0 wait:0.4
check "browser: joystick down moves the cursor" "grep -q '2 of 3' '$W/j1.log'"
br j1r joy:2 wait:1.5 joy:0 wait:0.3
check "browser: joystick held down keeps moving (it repeats, 2026-11-08)" "grep -q '3 of 3' '$W/j1r.log'"
br j2 'type:hello' wait:1 joy:10 wait:1 joy:0 wait:0.3
check "browser: holding fire opens the menu, and letting go doesn't choose" "grep -q 'Run slow\\|Run first' '$W/j2.log'"
br j3 'type:hello' wait:1 joy:10 wait:0.1 joy:0 wait:5
check "browser: a tap of fire runs" "grep -q 'HELLO FROM PICOD64' '$W/j3.log'"
br j4 'type:demo|' wait:6 joy:4 wait:0.1 joy:0 wait:4
check "browser: joystick left goes back" "grep -q 'online 0' '$W/j4.log'"
br j5 joy:10 wait:1 joy:0 wait:0.5
check "browser: holding fire on a folder does nothing" "grep -q '1 of 3' '$W/j5.log' && ! grep -q '/games' '$W/j5.log'"
# Interrupts back on after every transfer: the keyboard depends on them (php/cli/plp left them off, 2026-11-02)
br i1 c64mem:a2:1 wait:0.5 c64mem:a2:1
[ "$(grep -c 'C64 \$00A2' "$W/i1.log")" = 2 ] && [ "$(grep 'C64 \$00A2' "$W/i1.log" | sort -u | wc -l)" = 2 ]
IRC=$?; check "browser: the clock runs while it waits for a key, after a turbo listing" "[ $IRC = 0 ]"
check "browser: no keys were lost to dead interrupts" "! grep -l 'KEYBOARD DEAD' '$W'/*.log >/dev/null"

# The turbo: a 25 KB program, the bytes checked, faster than a plain LOAD; a wrong byte falls back; F3 and RUN SLOW
python3 -I - "$W" <<'EOP'
import sys
w = sys.argv[1]
out = bytearray([1, 8]); addr = 0x801
lines = [(i, bytes([0x8F]) + b' ' + bytes(0x40 + (i + k) % 26 for k in range(200))) for i in range(1, 121)] + [(1000, bytes([0x99]) + b'"BIG ONE LOADED OK"')]
for num, body in lines:
    nxt = addr + 4 + len(body) + 1
    out += bytes([nxt & 255, nxt >> 8, num & 255, num >> 8]) + body + b'\0'; addr = nxt
out += b'\0\0'
open(w + '/tbig.prg', 'wb').write(out)
EOP
truncate -s 64M "$W/tb.img"; /usr/sbin/mkfs.vfat -F 32 "$W/tb.img" >/dev/null
"$W/st" put "$W/tb.img" /big.prg "$W/tbig.prg"; "$W/st" put "$W/tb.img" /hello.prg "$W/bh.prg"; "$W/st" put "$W/tb.img" /demo.d64 "$W/bd.d64"
"$W/st" put "$W/tb.img" /Game.crt "$W/bh.prg"; "$W/st" put "$W/tb.img" /Game.d64 "$W/bd.d64"; "$W/st" put "$W/tb.img" /Game.t64 "$W/bh.prg"
"$W/st" put "$W/tb.img" /millie_and_molly_v1.1.prg "$W/bh.prg"; "$W/st" put "$W/tb.img" /millie_and_molly_v1.2.prg "$W/bp.prg"
c1541 "$W/bd.d64" -write "$W/tbig.prg" huge >/dev/null 2>&1; "$W/st" put "$W/tb.img" /fat.d64 "$W/bd.d64"
cp --sparse=always "$W/tb.img" "$W/tbk.img"
tb() { n=$1; shift; [ -n "$TBKEEP" ] || cp --sparse=always "$W/tbk.img" "$W/tb.img"; "$W/fwtwin" "$DOS" "$K" "$B" "$W/tb.img" "$W/out" 'type:load"*",8|' wait:3 'type:run|' wait:4 "$@" screen: state: > "$W/$n.log" 2>&1 || true; }
tb t1 'type:big|' wait:5
check "turbo: 25 KB, the program runs" "grep -q 'BIG ONE LOADED OK' '$W/t1.log' && grep -q 'turbos 3' '$W/t1.log'"
check "turbo: not through a typed LOAD" "! grep -q 'SEARCHING' '$W/t1.log'"
cp --sparse=always "$W/tbk.img" "$W/tb.img"; FWTBAD=1 "$W/fwtwin" "$DOS" "$K" "$B" "$W/tb.img" "$W/out" 'type:load"*",8|' wait:40 'type:run|' wait:40 'type:big|' wait:90 screen: state: > "$W/t2.log" 2>&1 || true
check "turbo: a wrong byte in the transfers: still ends with the program running" "grep -q 'BIG ONE LOADED OK' '$W/t2.log'"
tb t3 "type:$F3" wait:1
check "turbo: F3 says off" "grep -q 'Turbo off' '$W/t3.log'"
TBKEEP=; tb t4 "type:$F3" wait:1 'type:hello|' wait:6
check "turbo: off: a plain LOAD, by the alias" "grep -q 'SEARCHING FOR #' '$W/t4.log' && grep -q 'HELLO FROM' '$W/t4.log'"
TBKEEP=1 tb t4b wait:1
check "settings: turbo off is still off after a restart" "grep -q 'Turbo off' '$W/t4b.log'"
tb t5 "type:hello$F1~~|" wait:6
check "turbo: RUN SLOW: a plain LOAD, this once" "grep -q 'SEARCHING FOR #' '$W/t5.log' && grep -q 'HELLO FROM' '$W/t5.log'"
tb t6 "type:hello$F1~|" wait:3
check "turbo: the menu's LOAD: loaded, not run" "! grep -q 'HELLO FROM' '$W/t6.log' && ! grep -q 'Type to filter' '$W/t6.log'"
tb t6i 'type:hello' "type:$F1~|" wait:6 c64mem:a2:1 wait:0.5 c64mem:a2:1
[ "$(grep 'C64 \$00A2' "$W/t6i.log" | sort -u | wc -l)" = 2 ]
IRC=$?; check "turbo: after a turbo LOAD left at READY, the clock runs (the keyboard works)" "[ $IRC = 0 ]"
tb t7 'type:demo|' wait:6 'type:|' wait:8
check "a file in a mounted D64: loaded the plain way, the disk back in the drive" "grep -q 'B2 FROM THE D64' '$W/t7.log' && grep -q 'mounted \"/demo.d64\"' '$W/t7.log' && grep -q 'mounts 2' '$W/t7.log'"
tb t8 'type:fat|' wait:6 'type:~|' wait:80
check "a 25 KB file in a D64, the plain way (no turbo from a D64 since 2026-11-17)" "grep -q 'BIG ONE LOADED OK' '$W/t8.log'"
tb t9 wait:1
check "formats: the same name in three formats, told apart" "grep -q 'Game .* CRT' '$W/t9.log' && grep -q 'Game .* D64' '$W/t9.log' && grep -q 'Game .* T64' '$W/t9.log'"
tb t10 'type:v1.2|' wait:6
check "long names: v1.2, not v1.1, though they match for 16 characters" "grep -q 'PONG FROM' '$W/t10.log'"
tb t11 'type:millie' wait:1
check "long names: shown whole, with the underscore" "grep -q 'millie_and_molly_v1.1' '$W/t11.log'"
check "formats: a CRT can't run from here, and says so" "tb t12 'type:game.' wait:1; grep -q 'Game' '$W/t12.log'"
tb t13 'type:game|' wait:2
check "formats: RETURN on a format it can't run does nothing" "grep -q 'Type to filter\|Filter:' '$W/t13.log'"

# Remembering where you were: on the card, through a restart
tb s1 'type:hel|' wait:5
TBKEEP=1 tb s2 wait:1
check "settings: back on the program last run, after a restart" "grep -q ' of ' '$W/s2.log' && ! grep -q '  1 of ' '$W/s2.log'"
# A program whose name ends in a space, run from inside a D64 (2026-11-14: Uncensored's " - UNCENSORED - "): its XM
# lost the space to the command trim, went the plain way and its place wasn't saved
cp --sparse=always "$W/bd.d64" "$W/spc.d64"; c1541 "$W/spc.d64" -write "$W/bh.prg" "zz " >/dev/null 2>&1
cp --sparse=always "$W/tbk.img" "$W/sp.img"; "$W/st" mkdir "$W/sp.img" /Sp; "$W/st" put "$W/sp.img" /Sp/spc.d64 "$W/spc.d64"
"$W/fwtwin" "$DOS" "$K" "$B" "$W/sp.img" "$W/out" 'type:load"*",8|' wait:3 'type:run|' wait:4 'type:sp|' wait:2 'type:spc|' wait:8 'type:zz|' wait:12 screen: state: > "$W/sp.log" 2>&1 || true
"$W/st" get "$W/sp.img" /.picod64.cfg "$W/sp.cfg" >/dev/null 2>&1 || true
check "a name ending in a space, in a D64: the plain LOAD finds it, and its place saved" "grep -q 'HELLO FROM' '$W/sp.log' && grep -q 'SEARCHING' '$W/sp.log' && grep -q '^/Sp' '$W/sp.cfg'"

# The place saved as a disk goes in, not only when the turbo loads (2026-11-14: a load with the turbo off, refused or by
# the knob left the browser in the folder before)
cp --sparse=always "$W/tbk.img" "$W/pl.img"; "$W/st" mkdir "$W/pl.img" /Sp; "$W/st" put "$W/pl.img" /Sp/spc.d64 "$W/spc.d64"
"$W/fwtwin" "$DOS" "$K" "$B" "$W/pl.img" "$W/out" 'type:load"*",8|' wait:3 'type:run|' wait:4 'type:sp|' wait:2 'type:spc|' wait:4 > "$W/pl.log" 2>&1 || true
"$W/st" get "$W/pl.img" /.picod64.cfg "$W/pl.cfg" >/dev/null 2>&1 || true
check "settings: a D64 opened in the browser saves its place before anything loads" "[ \"\$(sed -n 2p '$W/pl.cfg')\" = /Sp ] && [ \"\$(sed -n 3p '$W/pl.cfg')\" = spc.d64 ]"
cp --sparse=always "$W/tbk.img" "$W/pk.img"; "$W/st" mkdir "$W/pk.img" /Sp; "$W/st" put "$W/pk.img" /Sp/spc.d64 "$W/spc.d64"
"$W/fwtwin" "$DOS" "$K" "$B" "$W/pk.img" "$W/out" wait:1 ui:mount:/Sp/spc.d64 wait:2 > "$W/pk.log" 2>&1 || true
"$W/st" get "$W/pk.img" /.picod64.cfg "$W/pk.cfg" >/dev/null 2>&1 || true
check "settings: a disk the knob puts in saves the browser's place" "[ \"\$(sed -n 2p '$W/pk.cfg')\" = /Sp ] && [ \"\$(sed -n 3p '$W/pk.cfg')\" = spc.d64 ]"

# A folder too big for the C64: 1,500 entries
python3 -I - "$W" <<'EOP'
import sys
w = sys.argv[1]
open(w + '/bigf.txt', 'w').write('\n'.join('game %04d.prg' % i for i in range(2500)))
EOP
truncate -s 64M "$W/bf.img"; /usr/sbin/mkfs.vfat -F 32 "$W/bf.img" >/dev/null
"$W/st" mkdir "$W/bf.img" /huge
"$W/st" putmany "$W/bf.img" /huge "$W/bigf.txt" "$W/bh.prg"
bf() { n=$1; shift; "$W/fwtwin" "$DOS" "$K" "$B" "$W/bf.img" "$W/out" 'type:load"*",8|' wait:3 'type:run|' wait:4 'type:|' wait:6 "$@" screen: state: > "$W/$n.log" 2>&1 || true; }
bf f1
check "big folder: opens, the first 2048, says how to find the rest" "grep -q 'of 2048' '$W/f1.log' && grep -q 'first 2048' '$W/f1.log'"
PGDN=$(printf '\035\035\035\035\035\035\035\035\035\035'); 
PGUP=$(printf '\235\235\235\235\235\235\235\235\235\235')
DOWN=""; UP=""; for i in 1 2 3 4 5 6 7 8 9 10 11 12 13; do DOWN="$DOWN type:$PGDN wait:0.8"; UP="$UP type:$PGUP wait:0.8"; done
bf f2 $DOWN wait:3 screen: $UP wait:3
check "big folder: paging down window after window, to the end" "grep -q '2048 of 2048' '$W/f2.log'"
check "big folder: and back up to the start" "tail -30 '$W/f2.log' | grep -q '/huge  *2048 |' && tail -30 '$W/f2.log' | grep -q 'game 0000'"
bf f3 'type:game 2499' wait:6
check "big folder: a filter finds an entry past the first 2048" "grep -q 'game 2499' '$W/f3.log'"

# Folders of long names, as real collections are (one real D64/G folder: 802 names, 37 KB of them): every name
# there, sorted past the 12 characters that the cache's keys hold, in any directory order
python3 -I - "$W" <<'EOP'
import random, sys
w = sys.argv[1]
random.seed(64)
names = []
for i in range(760):                 # 12 the same in tens, as real sets are; then the rest of the name decides
    names.append('Game Num %02d %03d (19%02d)(Some Long Publisher Name)[cr Cracker].d64' % (i // 40, 999 - i, 80 + i % 15))
for i in range(40):
    names.append('zset_with_a_very_long_shared_name_disk_%d%s.d64' % (i // 2, 'ab'[i % 2]))
random.shuffle(names)
open(w + '/longn.txt', 'w').write('\n'.join(names))
def key(n):
    k = ''.join((c.upper() if 'a' <= c <= 'z' else c) if ' ' <= c <= '_' or 'a' <= c <= 'z' else chr(0x7F) for c in n[:12])
    return k.ljust(12, '\0')
def clip(n):
    if len(n) < 64: return n
    h = 2166136261
    for c in n.encode(): h = ((h ^ c) * 16777619) & 0xFFFFFFFF
    h = (h >> 16) ^ (h & 0xFFFF)
    return n[:30] + '~%04x' % h + n[-28:]
names.sort(key=lambda n: (key(n), clip(n).lower()))
names = [clip(n) for n in names]
open(w + '/longn.want', 'w').write(''.join(n + '\n' for n in names))
EOP
truncate -s 64M "$W/ln.img"; /usr/sbin/mkfs.vfat -F 32 "$W/ln.img" >/dev/null
"$W/st" mkdir "$W/ln.img" /G
"$W/st" putmany "$W/ln.img" /G "$W/longn.txt" "$W/bh.prg"
"$W/st" ls "$W/ln.img" /G | sed 's/^    //; s/ [0-9]*$//' > "$W/longn.got"
check "long names: all 800, in order past the 12 characters of a key" "cmp -s '$W/longn.want' '$W/longn.got'"
# a run of the same 12 characters longer than a page (the biggest seen: 124): directory order, but each name once
python3 -I -c "
import sys
open(sys.argv[1], 'w').write('\\n'.join('Ripped Off #%03d.prg' % (i * 37 % 150) for i in range(150)))" "$W/run.txt"
"$W/st" mkdir "$W/ln.img" /R; "$W/st" putmany "$W/ln.img" /R "$W/run.txt" "$W/bh.prg"
"$W/st" ls "$W/ln.img" /R | sed 's/^    //; s/ [0-9]*$//' | sort > "$W/run.got"; sort "$W/run.txt" > "$W/run.want"; echo >> /dev/null
check "long names: a run longer than a page: every name, once" "cmp -s '$W/run.want' '$W/run.got'"
# the browser on it: a name over 63 characters runs and mounts, though a listing stops at 63
truncate -s 64M "$W/ll.img"; /usr/sbin/mkfs.vfat -F 32 "$W/ll.img" >/dev/null
LONGP=hello_with_a_really_really_long_name_for_this_test_of_opening_it_from_the_browser
LONGD=diskname_with_a_really_really_long_name_for_this_test_of_opening_it_from_the_browser
"$W/st" put "$W/ll.img" "/$LONGP.prg" "$W/bh.prg"; "$W/st" put "$W/ll.img" "/$LONGD.d64" "$W/bd.d64"
ll() { n=$1; shift; "$W/fwtwin" "$DOS" "$K" "$B" "$W/ll.img" "$W/out" 'type:load"*",8|' wait:3 'type:run|' wait:4 "$@" screen: state: > "$W/$n.log" 2>&1 || true; }
ll l1 'type:hello|' wait:6
check "long names: a PRG named over 63 characters runs" "grep -q 'HELLO FROM PICOD64' '$W/l1.log'"
ll l2 'type:diskname|' wait:6
check "long names: a D64 named over 63 characters mounts" "grep -q 'mounted \"/diskname' '$W/l2.log' && grep -q 'card: load .* OK' '$W/l2.log'"
# two long names that differ only in the middle: both listed, each opening its own file
truncate -s 64M "$W/lm.img"; /usr/sbin/mkfs.vfat -F 32 "$W/lm.img" >/dev/null
SAMEA="Last Ninja, The (1987)(System 3)(Disk 1 of 2 Side A)(a really long part)[cr Triad+CCGG].d64"
SAMEB="Last Ninja, The (1987)(System 3)(Disk 2 of 2 Side A)(a really long part)[cr Triad+CCGG].d64"
"$W/st" put "$W/lm.img" "/$SAMEA" "$W/bh.prg"; "$W/st" put "$W/lm.img" "/$SAMEB" "$W/bd.d64"
"$W/st" ls "$W/lm.img" / | sed 's/^    //; s/ [0-9]*$//' > "$W/lm.txt"
LMA=$(sed -n 1p "$W/lm.txt"); LMB=$(sed -n 2p "$W/lm.txt")
"$W/st" get "$W/lm.img" "/$LMA" "$W/lm1.out" >/dev/null 2>&1; "$W/st" get "$W/lm.img" "/$LMB" "$W/lm2.out" >/dev/null 2>&1
check "long names: two alike but for the middle: listed apart" "[ -n '$LMA' ] && [ '$LMA' != '$LMB' ]"
check "long names: and each opens its own file" "{ cmp -s '$W/lm1.out' '$W/bh.prg' && cmp -s '$W/lm2.out' '$W/bd.d64'; } || { cmp -s '$W/lm1.out' '$W/bd.d64' && cmp -s '$W/lm2.out' '$W/bh.prg'; }"
lm() { n=$1; shift; "$W/fwtwin" "$DOS" "$K" "$B" "$W/lm.img" "$W/out" 'type:load"*",8|' wait:3 'type:run|' wait:4 "$@" screen: > "$W/$n.log" 2>&1 || true; }
lm m1
check "disk numbers: found in the middle of a long name, the column shows 1A and 2A" "grep -q 'D64 *1A *|' '$W/m1.log' && grep -q 'D64 *2A *|' '$W/m1.log'"
check "disk numbers: the listing keeps them" "grep -q 'Disk 1 of 2 Side A' '$W/lm.txt' && grep -q 'Disk 2 of 2 Side A' '$W/lm.txt'"
check "long names: where a name is cut shows as -, not ?" "grep -q '(1987-(Disk 1 of 2' '$W/m1.log'"

# N with no disk in: a new D64 here, mounted, as on a 1541 "give me a fresh disk". From anything: this is
# plain channel-15, no browser. Then SAVE goes into it, and a real tool reads the result
truncate -s 64M "$W/nd.img"; /usr/sbin/mkfs.vfat -F 32 "$W/nd.img" >/dev/null
"$W/st" mkdir "$W/nd.img" /games
"$W/fwtwin" "$DOS" "$K" "$B" "$W/nd.img" "$W/out" cmd:cd:games cmd:n0:mydisk,ab err: '$' save:hello cmd:cd_ wait:2 state: cmd:n:mydisk,ab err: cmd:n: err: > "$W/n1.log" 2>&1 || true
"$W/st" get "$W/nd.img" /games/mydisk.d64 "$W/mydisk.d64" || true
check "N with no disk: makes NAME.D64 in the folder, 174,848 bytes" "[ \"\$(stat -c %s '$W/mydisk.d64' 2>/dev/null)\" = 174848 ]"
check "N with no disk: it's mounted" "grep -q 'mounted \"/games/mydisk.d64\"\|mounted /games/mydisk' '$W/n1.log' || grep -q 'card: load /games/mydisk.d64 OK' '$W/n1.log'"
check "N with no disk: the DOS reads it as a blank disk of that name" "grep -q '\"MYDISK *\" AB 2A' '$W/n1.log'"
check "N with no disk: SAVE goes in, back on the card" "c1541 -attach '$W/mydisk.d64' -list 2>/dev/null | grep -qi 'hello'"
check "N with no disk: the same name again: 63 FILE EXISTS, not a wipe" "grep -q '63, FILE EXISTS' '$W/n1.log'"
check "N with no name: 31 SYNTAX ERROR" "grep -q '31, SYNTAX ERROR' '$W/n1.log'"
cat > "$W/ndc.c" <<'EOC'
#include <stdio.h>
#include <string.h>
#include "disk.h"
#include "d64make.h"
static uint8_t d[D64_BYTES], f[D64_BYTES];
int main(int c, char **v) { FILE *x = fopen(v[1], "rb"); if (fread(f, 1, D64_BYTES, x) != D64_BYTES) return 2; uint8_t nm[] = "MYDISK"; d64_blank(d, nm, 6, 'A', 'B'); memcpy(f, f, 0); return memcmp(d, f, D64_BYTES) != 0; }
EOC
"$W/fwtwin" "$DOS" "$K" "$B" "$W/nd.img" "$W/out" cmd:cd:games cmd:n0:fresh,ab wait:2 > /dev/null 2>&1 || true
"$W/st" get "$W/nd.img" /games/fresh.d64 "$W/fresh.d64" || true
python3 -I - "$W/fresh.d64" "$W/ndb.d64" <<'EOP'
import sys
d = bytearray(open(sys.argv[1], 'rb').read()); o = 0x16500
d[o + 144:o + 144 + 16] = b'MYDISK'.ljust(16, b'\xA0')
open(sys.argv[2], 'wb').write(d)
EOP
gcc -O2 -w -I../core -o "$W/ndc" "$W/ndc.c" && check "N with no disk: byte for byte the blank disk d64_blank makes" "'$W/ndc' '$W/ndb.d64'"

# A fast-load cartridge: LOAD"*",8 starts with drive code, so PicoD64 goes to its temporary disk at once.
# The browser must be on it, even with no programs at the root, and come back to the card when it runs.
truncate -s 64M "$W/ca.img"; /usr/sbin/mkfs.vfat -F 32 "$W/ca.img" >/dev/null
"$W/st" mkdir "$W/ca.img" /D64
"$W/fwtwin" "$DOS" "$K" "$B" "$W/ca.img" "$W/out" cmdhex:4D2D5700050155 wait:3 state: 'type:load"*",8|' wait:20 'type:run|' wait:6 screen: state: > "$W/ca1.log" 2>&1 || true
check "fast-load cartridge: drive code at a root with no programs still gives a disk" "grep -q 'virtual D64 of / .* OK' '$W/ca1.log'"
check "fast-load cartridge: LOAD\"*\" finds the browser on it, and it runs" "grep -q 'PicoD64 ' '$W/ca1.log' && grep -q '> D64' '$W/ca1.log'"
check "fast-load cartridge: the browser comes back to the card, with the turbo" "tail -3 '$W/ca1.log' | grep -q 'online 0.*turbos [1-9]' && grep -q 'Turbo on' '$W/ca1.log'"

# D64s over 35 tracks: tracks 36-42 share RAM with 31-35 and come from the card as the head gets there.
# Read straight off the disk through the DOS's job queue, as a loader's own drive code would
python3 -I - "$W" <<'EOP'
import sys
w = sys.argv[1]
base = open(w + '/bd.d64', 'rb').read()[:174848]
def extra(tracks):
    out = bytearray()
    for t in range(36, tracks + 1):
        for s in range(17):
            out += bytes((t * 7 + s * 13 + i) & 255 for i in range(256))
    return bytes(out)
open(w + '/x40.d64', 'wb').write(base + extra(40))
open(w + '/x42.d64', 'wb').write(base + extra(42))
EOP
truncate -s 64M "$W/xt.img"; /usr/sbin/mkfs.vfat -F 32 "$W/xt.img" >/dev/null
"$W/st" put "$W/xt.img" /x40.d64 "$W/x40.d64"; "$W/st" put "$W/xt.img" /x42.d64 "$W/x42.d64"
cp --sparse=always "$W/xt.img" "$W/xtk.img"
xrd() { printf 'cmdhex:4D2D57060002%02x%02x cmdhex:4D2D5700000180 wait:%s dmem:0300:256:%s dmem:0000:1:%s.st ' "$1" "$2" "${4:-1.5}" "$W/$3" "$W/$3"; }
xt() { d=$1; shift; cp --sparse=always "$W/xtk.img" "$W/xt.img"; "$W/fwtwin" "$DOS" "$K" "$B" "$W/xt.img" "$W/out" "ui:mount:/$d" '$' "$@" state: 2>&1; }
xok() { python3 -I -c "
import sys
t, s, f = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
d = open(f, 'rb').read(); st = open(f + '.st', 'rb').read()
want = bytes((t * 7 + s * 13 + i) & 255 for i in range(256))
sys.exit(0 if d == want and st[0] == 1 else 1)" "$1" "$2" "$W/$3"; }
# the DOS's job queue reads to 40, so these are as a loader's would be; 41 and 42, and writing past 35, it
# won't do at all (VICE's true drive agrees): those are checked at the card side
xt x40.d64 $(xrd 38 3 r1) $(xrd 33 2 r2) $(xrd 38 3 r3) $(xrd 40 16 r4) $(xrd 18 0 r5) > "$W/x1.log" || true
check "40 tracks: track 38 reads, from the card" "xok 38 3 r1 && grep -q 'track 38 in' '$W/x1.log'"
check "40 tracks: 33, in the same place in RAM, reads" "[ \"\$(od -An -tu1 -N1 '$W/r2.st')\" -eq 1 ]"
check "40 tracks: and 38 again, and 40" "xok 38 3 r3 && xok 40 16 r4"
check "40 tracks: the DOS's own track 18 still reads" "[ \"\$(od -An -tu1 -N1 '$W/r5.st')\" -eq 1 ]"
cp --sparse=always "$W/xtk.img" "$W/xt.img"
check "40 tracks at the card: all shared tracks taking turns; a write back and read again" "'$W/st' ext '$W/xt.img' /x40.d64 40 | grep -q PASS"
check "42 tracks at the card: the same, with 41 and 42" "'$W/st' ext '$W/xt.img' /x42.d64 42 | grep -q PASS"
# a slow card: this long more between the head's arrival and the data
for ms in 10 50; do
  FWTRKMS=$ms xt x40.d64 $(xrd 38 3 s$ms 3) > "$W/xs$ms.log" || true
  check "40 tracks: read through a card taking $ms ms" "xok 38 3 s$ms"
done

# The device number (the board: knob held at power-on, kept in flash; here FWDEV): 9 answers, 8 doesn't,
# in browse mode, the built-in browser, the emulated DOS and the sniffer's CD_
d9() { n=$1; shift; cp --sparse=always "$W/brk.img" "$W/br.img"; FWDEV=9 "$W/fwtwin" "$DOS" "$K" "$B" "$W/br.img" "$W/out" "$@" screen: state: > "$W/$n.log" 2>&1 || true; }
d9 dv1 'type:load"$",8|' wait:3
check "device 9, browse mode: a real 1541's answer on 8 (VICE, the oracle)" "grep -q 'DEVICE NOT PRESENT' '$W/dv1.log'"
d9 dv1b 'type:load"*",9|' wait:3 'type:run|' wait:4 'type:demo|' wait:8 "type:$STOP" wait:2 'type:load"$",8|' wait:4
check "device 9, the emulated DOS mounted: the same on 8" "grep -q 'mounted \"/demo.d64\"' '$W/dv1b.log' && grep -q 'DEVICE NOT PRESENT' '$W/dv1b.log'"
d9 dv2 'type:load"*",9|' wait:3 'type:run|' wait:4
check "device 9: the browser loads from 9 and lists the card" "grep -q 'Type to filter' '$W/dv2.log' && grep -q 'games' '$W/dv2.log'"
d9 dv3 'type:load"*",9|' wait:3 'type:run|' wait:4 'type:demo|' wait:6 'type:|' wait:8
check "device 9: mounts a D64, runs from it" "grep -q 'B2 FROM THE D64' '$W/dv3.log'"
d9 dv4 'type:load"*",9|' wait:3 'type:run|' wait:4 'type:demo|' wait:6 'type:<' wait:4
check "device 9: CD_ on 9 unmounts" "grep -q 'online 0' '$W/dv4.log' && grep -q 'unmounts 1' '$W/dv4.log'"
cp --sparse=always "$W/brk.img" "$W/br.img"; FWDEV=11 "$W/fwtwin" "$DOS" "$K" "$B" "$W/br.img" "$W/out" 'type:load"*",11|' wait:3 'type:run|' wait:4 "type:${F3}demo|" wait:6 'type:|' wait:15 screen: > "$W/dv5.log" 2>&1 || true
check "device 11: the same, turbo off: two digits in the typed LOAD" "grep -q 'B2 FROM THE D64' '$W/dv5.log' && grep -q ',11,1' '$W/dv5.log'"

# Dir Browser 1.08 (DIRB=/path/to/the unzipped "bin" folder; graham-it.itch.io/dirbrowser, free, not bundled;
# run as a black box, by its keys from docs/readme.txt): listing, a folder, a D64 mounted and loaded from, DEL
# out, and its "Fast Load" turbo (tdb64), whose drive code loads its main program "db64" (no extension) from
# the virtual D64 (2026-11-02: that file was missing, as the virtual D64 took only *.prg).
if [ -n "$DIRB" ] && [ -f "$DIRB/db64" ]; then
  python3 -I - "$W" <<'EOP'
import sys
w = sys.argv[1]
def basic(text):
    body = bytes([0x99]) + b'"' + text + b'"'; nxt = 0x0801 + 4 + len(body) + 1
    return bytes([1, 8, nxt & 255, nxt >> 8, 10, 0]) + body + b'\0\0\0'
open(w + '/dbh.prg', 'wb').write(basic(b'HELLO FROM PICOD64'))
open(w + '/dbp.prg', 'wb').write(basic(b'PONG FROM THE GAMES FOLDER'))
open(w + '/dbb.prg', 'wb').write(basic(b'B2 FROM THE D64'))
EOP
  c1541 -format "demo disk,dd" d64 "$W/dbd.d64" >/dev/null 2>&1; c1541 "$W/dbd.d64" -write "$W/dbb.prg" b2 >/dev/null 2>&1
  truncate -s 64M "$W/db.img"; /usr/sbin/mkfs.vfat -F 32 "$W/db.img" >/dev/null
  "$W/st" put "$W/db.img" /db64 "$DIRB/db64"; "$W/st" put "$W/db.img" /tdb64 "$DIRB/tdb64"
  "$W/st" put "$W/db.img" /hello.prg "$W/dbh.prg"; "$W/st" put "$W/db.img" /demo.d64 "$W/dbd.d64"
  "$W/st" mkdir "$W/db.img" /games; "$W/st" put "$W/db.img" /games/pong.prg "$W/dbp.prg"
  db() { n=$1; shift; "$W/fwtwin" "$DOS" "$K" "$B" "$W/db.img" "$W/out" 'type:load"db64",8|' wait:2 'type:run|' wait:8 "$@" screen: > "$W/$n.log" 2>&1 || true; }
  db dbl
  check "Dir Browser: runs and lists the card" "grep -q 'HELLO.PRG' '$W/dbl.log' && grep -q 'DEMO.D64' '$W/dbl.log'"
  db dbg 'type:g' wait:1 'type:|' wait:6 'type:p' wait:1 'type:|' wait:8
  check "Dir Browser: into a folder, runs a program there" "grep -q 'PONG FROM THE GAMES FOLDER' '$W/dbg.log'"
  db dbm 'type:d~' wait:1 'type:|' wait:12 'type:b' wait:1 'type:|' wait:15
  check "Dir Browser: mounts a D64 and loads from it (the real DOS)" "grep -q 'B2 FROM THE D64' '$W/dbm.log'"
  db dbu 'type:d~' wait:1 'type:|' wait:12 'type:<' wait:8
  check "Dir Browser: DEL unmounts, back to the card" "grep -q 'GAMES' '$W/dbu.log'"
  "$W/fwtwin" "$DOS" "$K" "$B" "$W/db.img" "$W/out" 'type:load"tdb64",8,1|' wait:30 screen: > "$W/dbt.log" 2>&1 || true
  check "Dir Browser turbo: Fast Load's drive code loads db64 from the virtual D64" "grep -q 'DIR BROWSER V1.08' '$W/dbt.log' && ! grep -q 'NOT FOUND' '$W/dbt.log'"
  "$W/fwtwin" "$DOS" "$K" "$B" "$W/db.img" "$W/out" 'type:load"tdb64",8,1|' wait:30 'type:<' wait:10 'type:d~' wait:1 'type:|' wait:14 'type:b' wait:1 'type:|' wait:20 screen: > "$W/dbtm.log" 2>&1 || true
  check "Dir Browser turbo: from a mounted D64, its drive code on the cycle-exact drive" "grep -q 'B2 FROM THE D64' '$W/dbtm.log'"
  # ⚠ KNOWN OPEN (FIRMWARE.md): turbo, then a second load from a FOLDER (a second virtual D64) stalls:
  # the C64 abandons the second M-W block after the switch. Reported, not checked here yet.
else
  echo "(Dir Browser part skipped: set DIRB=/path/to/Dir Browser's bin folder)"
fi
# The drive's sounds on real runs (the synth itself: host/fw_sound_test.c). A duty per 64 us, as the board's interrupt.
sndck() { python3 -I -c "
import sys
d=open(sys.argv[1],'rb').read(); fs=15625; lo=float(sys.argv[2])
if len(d)<fs: sys.exit(1)
w=[max(d[i:i+64]) for i in range(0,len(d)-63,64)]   # 4 ms windows: sound in them (the motor's hiss is on in about half its samples)
sys.exit(0 if max(d)<=255 and max(d)>=int(sys.argv[3]) and sum(1 for v in w if v)/len(w)>=lo and d.count(0)>0 else 1)" "$@"; }
FWSND="$W/s1.raw" "$W/fwtwin" "$DOS" "$K" "$B" "$W/card.img" "$W/out" wait:1 ui:mount:/demo.d64 'load:$' wait:2 > "$W/s1.log" 2>&1 || true
check "sounds: a directory on the real DOS, motor and head steps" "sndck '$W/s1.raw' 0.3 100"
FWSND="$W/s2.raw" tb s2 'type:big|' wait:12
check "sounds: the browser's turbo is heard too" "sndck '$W/s2.raw' 0.1 100"
cp --sparse=always "$W/card.img" "$W/s3.img"; FWSND="$W/s3.raw" "$W/fwtwin" "$DOS" "$K" "$B" "$W/s3.img" "$W/out" wait:3 > "$W/s3.log" 2>&1 || true
check "sounds: nothing happening is silent" "python3 -I -c \"import sys; d=open('$W/s3.raw','rb').read(); sys.exit(0 if len(d)>15625 and max(d)==0 else 1)\""
# The knob's Sound: saved on the card; the browser's own saves keep it, and its saves' other settings survive it
cfgl() { "$W/st" get "$1" /.picod64.cfg "$W/cfg.out" >/dev/null 2>&1; head -1 "$W/cfg.out"; }
cp --sparse=always "$W/brk.img" "$W/s4.img"
"$W/fwtwin" "$DOS" "$K" "$B" "$W/s4.img" "$W/out" ui:sound:0 > "$W/s4.log" 2>&1 || true
check "sound off: saved, from no settings file" "[ \"\$(cfgl '$W/s4.img')\" = 10 ] && grep -q 'ui:sound:0 (now 0)' '$W/s4.log'"
"$W/fwtwin" "$DOS" "$K" "$B" "$W/s4.img" "$W/out" 'type:load"*",8|' wait:3 'type:run|' wait:4 "type:${F3}hello" wait:3 'type:|' wait:8 > "$W/s5.log" 2>&1 || true
check "sound off: launching saves the browser's F3 and keeps the sound setting" "[ \"\$(cfgl '$W/s4.img')\" = 00 ]"
L2=$(sed -n 2p "$W/cfg.out")
"$W/fwtwin" "$DOS" "$K" "$B" "$W/s4.img" "$W/out" ui:sound:1 > "$W/s6.log" 2>&1 || true
check "sound on again: the first line back to the old form, the rest untouched" "[ \"\$(cfgl '$W/s4.img')\" = 0 ] && [ \"\$(sed -n 2p '$W/cfg.out')\" = '$L2' ] && grep -q 'ui:sound:1 (now 1)' '$W/s6.log'"
[ $OK = 1 ] && echo "FWTWIN PASS" || { echo "FWTWIN FAIL"; exit 1; }
