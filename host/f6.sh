#!/bin/sh
# F6: browse mode against the real C64 KERNAL, a folder standing in for the SD card.
#   - the error channel at power-on; LOAD"$" of the root (a folder as DIR, files as PRG);
#   - LOAD of files byte for byte; CD:folder, its listing, CD_ (left arrow) back up;
#   - a missing name: FILE NOT FOUND (error 4 at the C64, 62 on the error channel);
#   - CD:image.d64 mounts it: the real DOS on the emulated 1541 lists it and a file
#     loads identical to c1541's extraction; CD_ unmounts, browse mode answers again.
#   - drive code (2026-10-29: serve PRGs in browse mode, go cycle-exact when drive
#     code arrives): M-R answers from the booted 1541; the first M-W hands the bus to the
#     emulated 1541 (a virtual D64 of the folder's PRGs; the conversation replayed into the
#     DOS), and the code runs there on M-E.
#   sh host/f6.sh
set -e
cd "$(dirname "$0")"
DOS=${DOS1541:-$HOME/src/vice-3.9/data/DRIVES/dos1541-325302-01+901229-05.bin}
K=${KERNAL:-$HOME/.local/share/vice/C64/kernal-901227-03.bin}
B=${BASIC:-$HOME/.local/share/vice/C64/basic-901226-01.bin}
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
gcc -O2 -Wall -Wextra -Wno-unused-function -c c64.c -o "$W/c64.o"
gcc -O2 -Wall -Wextra -Wno-unused-function -Wno-stringop-truncation -o f6 f6.c "$W/c64.o"

SD="$W/sd"; mkdir -p "$SD/games"
python3 -I - "$SD" <<'EOF'
import sys, random
r = random.Random(9); d = sys.argv[1]
open(d + "/hello.prg", "wb").write(bytes([1, 8]) + r.randbytes(1500))
open(d + "/games/pong.prg", "wb").write(bytes([1, 8]) + r.randbytes(4000))
open(d + "/games/maze.prg", "wb").write(bytes([1, 8]) + r.randbytes(300))
open(d + "/b.prg", "wb").write(bytes([1, 8]) + r.randbytes(508))
EOF
c1541 -format "demo disk,dd" d64 "$SD/demo.d64" >/dev/null 2>&1
c1541 "$SD/demo.d64" -write "$SD/b.prg" b508 >/dev/null 2>&1
rm "$SD/b.prg"
c1541 "$SD/demo.d64" -read b508 "$W/ref_b508.prg" >/dev/null 2>&1

mkdir "$W/out"
#      1    2  3              4            5  6              7       8          9  10         11  12 13        14   15
./f6 "$DOS" "$K" "$B" "$SD" "$W/out" \
     err: '$' load:hello.prg cmd:cd:games '$' load:pong.prg cmd:cd_ load:nosuch err: cmd:cd:demo.d64 '$' load:b508 err: cmd:cd_ '$' \
     > "$W/log" || true
cat "$W/log"
OK=1
check() { if eval "$2"; then echo "ok    $1"; else echo "FAIL  $1"; OK=0; fi; }
check "power-on error channel" "grep -q '\"73,PICOD64 BROWSE,00,00|\"' '$W/log'"
check "root: GAMES as DIR"      "grep -q '\"GAMES\" *DIR' '$W/log'"
check "root: DEMO.D64, HELLO.PRG" "grep -q '\"DEMO.D64\"' '$W/log' && grep -q '\"HELLO.PRG\"' '$W/log'"
check "hello.prg byte for byte" "tail -c +3 '$SD/hello.prg' | cmp -s - '$W/out/3.bin'"
check "CD:games lists it"       "grep -q '\"GAMES *\" FF 2A' '$W/log' && grep -q '\"PONG.PRG\"' '$W/log'"
check "pong.prg byte for byte"  "tail -c +3 '$SD/games/pong.prg' | cmp -s - '$W/out/6.bin'"
check "missing name: error 4"   "grep -q '^load:nosuch: done .* err=4' '$W/log'"
check "... and 62 on the channel" "grep -q '\"62, FILE NOT FOUND,00,00|\"' '$W/log'"
check "mount: the real DOS lists the D64" "grep -q 'mounted demo.d64' '$W/log' && grep -q '\"DEMO DISK *\" DD 2A' '$W/log'"
check "mount: b508 identical to c1541" "tail -c +3 '$W/ref_b508.prg' | cmp -s - '$W/out/12.bin'"
mkdir "$W/out2"
./f6 "$DOS" "$K" "$B" "$SD" "$W/out2" load:hello.prg cmdhex:4D2D52FEFF02 err: \
     cmdhex:4D2D57000506A9428D000660 peek:0600 cmdhex:4D2D450005 wait:0.5 peek:0600 '$' > "$W/log2" || true
grep -v '^ *[0-9]* "' "$W/log2"
check "M-R in browse mode: the DOS ROM's bytes" "grep -q 'hex 67 FE' '$W/log2'"
check "M-W: the emulated 1541 takes the bus"    "grep -q 'mounted (virtual: 1 PRGs of /)' '$W/log2'"
check "the conversation replayed into the DOS"  "grep -q 'replayed .* (done)' '$W/log2'"
check "M-E runs the code in the DOS"            "grep -q 'drive RAM \$0600 = \$00' '$W/log2' && grep -q 'drive RAM \$0600 = \$42' '$W/log2'"
check "the virtual D64 lists the folder's PRG"  "grep -q '\"HELLO.PRG\" *PRG' '$W/log2'"
check "CD_ unmounts; browse lists the root again" "grep -q 'unmounted' '$W/log' && [ \$(grep -c '\"PICOD64 *\" FF 2A' '$W/log') = 2 ]"

# FB64 (Jani, World of Jani; not bundled: FB64=/path/to/fb.prg to run this part). Driven by keys as a
# person would: list the card, into a folder and out (DEL), select the D64, mount it, run the program in it.
if [ -n "$FB64" ] && [ -f "$FB64" ]; then
  FBSD="$W/fbsd"; mkdir -p "$FBSD/games"
  cp "$FB64" "$FBSD/fb.prg"; cp "$SD/games/"*.prg "$FBSD/games/"
  python3 -I - "$FBSD" <<'EOF'
import sys
d = sys.argv[1]
def basic(lines):
    out = bytearray([1, 8]); addr = 0x0801
    for num, body in lines:
        nxt = addr + 4 + len(body) + 1
        out += bytes([nxt & 255, nxt >> 8, num & 255, num >> 8]) + body + b'\0'; addr = nxt
    return out + b'\0\0'
open(d + "/hello.prg", "wb").write(basic([(10, bytes([0x99]) + b'"HELLO FROM PICOD64"')]))
open(d + "/indisk.prg", "wb").write(basic([(10, bytes([0x99]) + b'"LOADED FROM THE D64"')]))
EOF
  c1541 -format "fb test disk,fb" d64 "$FBSD/demo.d64" >/dev/null 2>&1
  c1541 "$FBSD/demo.d64" -write "$FBSD/indisk.prg" indisk >/dev/null 2>&1; rm "$FBSD/indisk.prg"
  mkdir "$W/fbout"
  ./f6 "$DOS" "$K" "$B" "$FBSD" "$W/fbout" 'type:load"fb.prg",8|' wait:2 'type:run|' wait:5 screen: \
       'type:~' wait:1 'type:|' wait:6 screen: 'type:<' wait:6 \
       > "$W/fb1.log" 2>&1 || true
  ./f6 "$DOS" "$K" "$B" "$FBSD" "$W/fbout" 'type:load"fb.prg",8|' wait:2 'type:run|' wait:5 \
       'type:~' wait:1 'type:~' wait:1 'type:|' wait:12 screen: 'type:~' wait:1 'type:|' wait:15 screen: \
       > "$W/fb2.log" 2>&1 || true
  grep -v '^ *|  *|$' "$W/fb2.log" | grep -v '^type\|^load' | tail -16
  check "FB64: lists the card"            "grep -q '/GAMES' '$W/fb1.log' && grep -q 'DEMO.D64' '$W/fb1.log'"
  check "FB64: into a folder (RETURN)"     "grep -q '\"GAMES *\" FF 2A' '$W/fb1.log' && grep -q 'PONG.PRG' '$W/fb1.log'"
  check "FB64: mounts the D64, lists it"   "grep -q 'mounted demo.d64' '$W/fb2.log' && grep -q 'FB TEST DISK' '$W/fb2.log'"
  check "FB64: runs the program on the D64" "grep -q 'LOADED FROM THE D64' '$W/fb2.log'"
else
  echo "(FB64 part skipped: set FB64=/path/to/fb.prg)"
fi
# FB64's FASTLOADER version (FB64FL=/path/to/fb20fl.prg): pick a PRG in browse mode; its loader
# uploads 1541 drive code; PicoD64 goes cycle-exact and the loader loads and runs it.
if [ -n "$FB64FL" ] && [ -f "$FB64FL" ] && [ -d "$W/fbsd" ]; then
  cp "$FB64FL" "$W/fbsd/fbfl.prg"; rm -f "$W/fbsd/fb.prg"
  # the root holds /GAMES, DEMO.D64, FBFL.PRG, HELLO.PRG: four downs from the parent entry
  ./f6 "$DOS" "$K" "$B" "$W/fbsd" "$W/fbout" 'type:load"fbfl.prg",8|' wait:2 'type:run|' wait:6 \
       'type:~~~~' wait:1 'type:|' wait:20 screen: > "$W/fb3.log" 2>&1 || true
  grep -v '^ *|  *|$' "$W/fb3.log" | grep -v '^type\|^load' | tail -9
  check "FB64 FL: drive code -> cycle-exact, replayed" "grep -q 'replayed .* (done)' '$W/fb3.log'"
  check "FB64 FL: its fast loader loads and runs HELLO" "grep -q 'HELLO FROM PICOD64' '$W/fb3.log'"
fi
[ $OK = 1 ] && echo "F6 PASS" || { echo "F6 FAIL"; exit 1; }
