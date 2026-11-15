#!/bin/sh
# F4c/F4d: a C64 running the real KERNAL LOADs from, and SAVEs to, our 1541
# running the real DOS.
#   - a test disk is made by VICE's c1541 (files whose sizes sit on block
#     boundaries, and two long ones);
#   - every file LOADed must equal c1541's own extraction, byte for byte;
#   - F4d: SAVEs, a LOAD back, a scratch; then a second session formats the
#     disk (N0:) and SAVEs onto it. c1541 must read back exactly what was
#     SAVEd, list what the DOS wrote, and its -validate must change nothing;
#   - disk swap: a second c1541 disk (ID bb). Swapped as a person does (write-
#     protect sensor sequence), LOAD"$" must list it and its file must load; swapped
#     silently (a control), the DOS must NOT notice (error 4, as on a real 1541);
#   - the core and the per-cycle reference must give identical results;
#   - with VICE=1, LOAD"$" must equal VICE's true-drive emulation running the
#     same driver (opens a VICE window for a few seconds).
#   sh host/f4c.sh            VICE=1 sh host/f4c.sh
set -e
cd "$(dirname "$0")"
DOS=${DOS1541:-$HOME/src/vice-3.9/data/DRIVES/dos1541-325302-01+901229-05.bin}
K=${KERNAL:-$HOME/.local/share/vice/C64/kernal-901227-03.bin}
B=${BASIC:-$HOME/.local/share/vice/C64/basic-901226-01.bin}
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
gcc -O2 -Wall -Wextra -Wno-unused-function -c c64.c -o "$W/c64.o"
gcc -O2 -Wall -Wextra -Wno-unused-function       -o f4c     f4c.c "$W/c64.o"
gcc -O2 -Wall -Wextra -Wno-unused-function -DREF -o f4c_ref f4c.c "$W/c64.o"

# the test disk: PRG = 2-byte load address + body; 254 bytes fill one block
FILES="one b252 b253 b254 b508 mid big"
python3 -I - "$W" <<'EOF'
import sys, random
r = random.Random(41)
for name, n in [("one", 1), ("b252", 252), ("b253", 253), ("b254", 254), ("b508", 508), ("mid", 9000), ("big", 30000)]:
    open(f"{sys.argv[1]}/{name}.prg", "wb").write(bytes([0x01, 0x08]) + r.randbytes(n))
EOF
c1541 -format "f4c test,ff" d64 "$W/test.d64" >/dev/null 2>&1
python3 -I -c "import random,sys; open(sys.argv[1],'wb').write(bytes([1,8])+random.Random(5).randbytes(700))" "$W/bfile.prg"
c1541 -format "second disk,bb" d64 "$W/b.d64" >/dev/null 2>&1
c1541 "$W/b.d64" -write "$W/bfile.prg" bfile >/dev/null 2>&1
c1541 "$W/b.d64" -read bfile "$W/ref_bfile.prg" >/dev/null 2>&1
for f in $FILES; do
  c1541 "$W/test.d64" -write "$W/$f.prg" $f >/dev/null 2>&1
  c1541 "$W/test.d64" -read $f "$W/ref_$f.prg" >/dev/null 2>&1
  tail -c +3 "$W/ref_$f.prg" > "$W/ref_$f.bin"
done

# session 1: commands 1..8 load; 9, 10 save; 11 loads 9 back; 12 scratches b252
S1="\$ $FILES save:new1:600:7 save:new2:5000:8 load:new1 cmd:s0:b252"
# session 2: format, then save
S2="cmd:n0:fresh,ab save:after:3000:9"
# session 3: swap as a person does; session 4: swap silently (the control)
S3="\$ swap:$W/b.d64 \$ load:bfile"
S4="\$ swapq:$W/b.d64 \$"
OK=1
for m in new ref; do
  mkdir "$W/$m" "$W/${m}2"
  B_=f4c; [ $m = ref ] && B_=f4c_ref
  ./$B_ "$DOS" "$K" "$B" "$W/test.d64" "$W/$m"  $S1 > "$W/$m.txt"  || { echo "FAIL: session 1 ($m)"; OK=0; }
  ./$B_ "$DOS" "$K" "$B" "$W/test.d64" "$W/${m}2" $S2 > "$W/${m}2.txt" || { echo "FAIL: session 2 ($m)"; OK=0; }
  mkdir "$W/${m}3" "$W/${m}4"
  ./$B_ "$DOS" "$K" "$B" "$W/test.d64" "$W/${m}3" $S3 > "$W/${m}3.txt" || { echo "FAIL: session 3 ($m)"; OK=0; }
  ./$B_ "$DOS" "$K" "$B" "$W/test.d64" "$W/${m}4" $S4 > "$W/${m}4.txt" || true   # the control: errors expected
done
sed 's/ bus=.*//' "$W/new.txt" "$W/new2.txt" "$W/new3.txt" "$W/new4.txt"
# results (everything but the bus hash) must match between the models
cat "$W/new.txt" "$W/new2.txt" "$W/new3.txt" "$W/new4.txt" | sed 's/ bus=.*//' > "$W/a"
cat "$W/ref.txt" "$W/ref2.txt" "$W/ref3.txt" "$W/ref4.txt" | sed 's/ bus=.*//' > "$W/b"
cmp -s "$W/a" "$W/b" && cmp -s "$W/new/final.d64" "$W/ref/final.d64" && cmp -s "$W/new2/final.d64" "$W/ref2/final.d64" \
  && echo "core = reference: identical results and identical disks" || { echo "FAIL: core and reference differ"; diff "$W/a" "$W/b"; OK=0; }
i=2
for f in $FILES; do
  if cmp -s "$W/ref_$f.bin" "$W/new/$i.bin"; then echo "$f: identical to c1541 ($(stat -c %s "$W/new/$i.bin") bytes)"
  else echo "FAIL: $f differs from c1541"; OK=0; fi
  i=$((i + 1))
done

# F4d: what the drive wrote, as c1541 sees it
c1541 "$W/new/final.d64" -read new1 "$W/v1.prg" -read new2 "$W/v2.prg" >/dev/null 2>&1 || true
c1541 "$W/new2/final.d64" -read after "$W/v3.prg" >/dev/null 2>&1 || true
for p in "v1.prg new/9.prg" "v2.prg new/10.prg" "v3.prg new2/2.prg"; do
  set -- $p
  cmp -s "$W/$1" "$W/$2" && echo "SAVEd $(basename $2): c1541 reads it back exactly" || { echo "FAIL: SAVEd $2 differs"; OK=0; }
done
tail -c +3 "$W/v1.prg" | cmp -s - "$W/new/11.bin" && echo "LOAD of a SAVEd file: identical" || { echo "FAIL: LOAD back"; OK=0; }
c1541 "$W/new/final.d64" -list 2>/dev/null | grep -q '"b252"' && { echo "FAIL: b252 not scratched"; OK=0; } || echo "scratch: b252 gone"
c1541 "$W/new2/final.d64" -list 2>/dev/null | grep -q '^0 "fresh  *" ab 2a' && echo "format: header \"fresh\", ID ab" || { echo "FAIL: format header"; OK=0; }
for d in new new2; do
  cp "$W/$d/final.d64" "$W/val.d64"; c1541 "$W/val.d64" -validate >/dev/null 2>&1 || true
  cmp -s "$W/val.d64" "$W/$d/final.d64" && echo "c1541 -validate changes nothing ($d)" || { echo "FAIL: validate changed $d"; OK=0; }
done

# disk swap
hdr() { python3 -I -c "
import sys; b=open(sys.argv[1],'rb').read(); i=4; t=bytearray()
while b[i]: t.append(b[i]); i+=1
print(t.decode('latin1'))" "$1"; }
hdr "$W/new3/3.bin" | grep -q '"SECOND DISK *" BB' && echo "swap: LOAD\"\$\" lists the new disk" || { echo "FAIL: swap: new disk not listed"; OK=0; }
tail -c +3 "$W/ref_bfile.prg" | cmp -s - "$W/new3/4.bin" && echo "swap: its file loads, identical to c1541" || { echo "FAIL: swap: file"; OK=0; }
grep -q '^\$: done .* err=4' "$W/new4.txt" && echo "silent swap (control): the DOS does not notice (error 4), as a real 1541" \
  || { echo "FAIL: the silent swap was noticed: the sensor is not what the DOS uses"; OK=0; }

if [ "${VICE:-0}" = 1 ]; then
  DRV="a9 01 a2 00 a0 c1 20 bd ff a9 01 a2 08 a0 00 20 ba ff a9 00 a2 00 a0 10 20 d5 ff 8e 00 c2 8c 01 c2 8d 02 c2 08 68 8d 03 c2 a5 90 8d 04 c2 4c 2e c0"
  END=$(printf '%x' $((0x1000 + $(stat -c %s "$W/new/1.bin") - 1)))
  cat > "$W/mon.txt" <<EOF
break \$e5cd
x
> c000 $DRV
> c100 24
r pc = c000
del
break \$c02e
x
bsave "$W/vdir.bin" 0 1000 $END
quit
EOF
  (sleep 3; cat "$W/mon.txt"; sleep 25) | timeout 90 script -qefc \
    "x64sc -default -nativemonitor -initbreak 0xfce2 -drive8type 1541 -drive8truedrive -drive8idle 0 \
     -dos1541 $DOS -8 $W/test.d64 +sound -warp -limitcycles 80000000" /dev/null > /dev/null 2>&1 || true
  cmp -s "$W/vdir.bin" "$W/new/1.bin" && echo "LOAD\"\$\": identical to VICE's true drive" \
    || { echo "FAIL: LOAD\"\$\" differs from VICE"; OK=0; }
fi
[ $OK = 1 ] && echo "F4c/F4d PASS" || { echo "F4c/F4d FAIL"; exit 1; }
