#!/bin/sh
# F4a: the real 1541 DOS [S9] boots on our core exactly as in VICE's true-drive
# emulation (black-box oracle). VICE runs a C64 to BASIC's keyboard loop; its
# monitor then dumps drive 8's registers, 2 KiB of RAM and both VIAs. Our core
# is stopped at the same idle-loop state (PC, X) and must match:
#   - drive RAM, all 2048 bytes;
#   - VIA2 timer 1 (the DOS's IRQ timer), i.e. the CPU's phase against it.
# Not compared: VIA1 T1, which the DOS never loads; a 6522 reset does not
# clear the counters [S2 l.106-107], so its value depends on power-up.
#   sh host/f4a.sh            (opens a VICE window for a few seconds)
set -e
cd "$(dirname "$0")"
ROM=${DOS1541:-$HOME/src/vice-3.9/data/DRIVES/dos1541-325302-01+901229-05.bin}
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
gcc -O2 -Wall -Wextra -o dos_boot dos_boot.c

cat > "$W/mon.txt" <<EOF
break \$e5cd
x
device 8:
r
bsave "$W/vice.bin" 0 0000 07ff
io 1c00
quit
EOF
# the native monitor needs a tty: script(1) gives it one, fed from mon.txt
(sleep 3; cat "$W/mon.txt"; sleep 20) | timeout 90 script -qefc \
  "x64sc -default -nativemonitor -initbreak 0xfce2 -drive8type 1541 -drive8truedrive \
   -drive8idle 0 -dos1541 $ROM +sound -warp -limitcycles 40000000" /dev/null \
  | tr -d '\r' | sed 's/\x1b\[[0-9;]*m//g' > "$W/vice.out" 2>&1 || true
[ -s "$W/vice.bin" ] || { echo "FAIL: VICE produced no dump"; exit 1; }

# VICE's drive registers line:  .;ec2d ff 0b ff 45 00 00 00100001  <stopwatch>
set -- $(grep -m1 '^\.;' "$W/vice.out")
VPC=${1#.;}; VA=$2; VX=$3; VY=$4; VSP=$5; VCYC=$9
VT1=$(grep -A8 '^>8:1c00' "$W/vice.out" | sed -n 's/^Timer 1: \([0-9a-f]*\).*/\1/p' | head -1)
echo "VICE: pc=$VPC a=$VA x=$VX y=$VY sp=$VSP via2 t1=$VT1 (drive stopwatch $VCYC)"

# VICE's stopwatch counts from about the drive's reset; search +-30 cycles of it
./dos_boot "$ROM" "$W/ours.bin" $((VCYC - 30)) "$VPC" "$VX" | tee "$W/ours.txt"
OT1=$(sed -n 's/^via2 t1=//p' "$W/ours.txt")
OK=1
cmp -s "$W/ours.bin" "$W/vice.bin" && echo "drive RAM: 2048 bytes identical" \
  || { echo "FAIL: drive RAM differs"; cmp -l "$W/ours.bin" "$W/vice.bin" | head; OK=0; }
[ "$(echo $OT1 | tr A-F a-f)" = "$VT1" ] && echo "VIA2 T1 identical ($VT1)" \
  || { echo "FAIL: VIA2 T1 ours $OT1, VICE $VT1"; OK=0; }
grep -q "a=$(echo $VA | tr a-f A-F) .*y=$(echo $VY | tr a-f A-F) sp=$(echo $VSP | tr a-f A-F)" "$W/ours.txt" \
  && echo "A, Y, SP identical" || { echo "FAIL: A/Y/SP differ"; OK=0; }
[ $OK = 1 ] && echo "F4a PASS" || { echo "F4a FAIL"; exit 1; }
