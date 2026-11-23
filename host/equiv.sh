#!/bin/sh
# Event-driven core vs the per-cycle reference (host/ref/): every bus cycle's
# hash, block by block, must be identical on every workload.
#   sh host/equiv.sh [cycles]
set -e
cd "$(dirname "$0")"
N=${1:-20000000}
gcc -O2 -Wall -Wextra -Wno-unused-function -DREF -o equiv_ref equiv.c
gcc -O2 -Wall -Wextra -Wno-unused-function       -o equiv_new equiv.c
gcc -O2 -Wall -Wextra -Wno-unused-function -DLA_FORCED -o equiv_laf equiv.c
gcc -O2 -Wall -Wextra -Wno-unused-function -DDRIVE_EAGER -o equiv_eag equiv.c   # lazy disk off (2026-11-07)
(cd ../bench && mkdir -p build && acme -DTIMER=10000 -o build/wl_dos.bin workload.asm >/dev/null \
  && acme -DTIMER=150 -o build/wl_stress.bin workload.asm >/dev/null \
  && acme -o build/wl_timers.bin wl_timers.asm >/dev/null && acme -o build/wl_scan.bin wl_scan.asm >/dev/null && acme -o build/wl_step.bin wl_step.asm >/dev/null && acme -o build/wl_stepwait.bin wl_stepwait.asm >/dev/null && acme -o build/wl_write.bin wl_write.asm >/dev/null && acme -DATNIRQ=0 -o build/wl_iec.bin wl_iec.asm >/dev/null && acme -DATNIRQ=1 -o build/wl_iecatn.bin wl_iec.asm >/dev/null)
fail=0
# the SYNC-run framing against the bitmaps it replaced, every bit of 147 tracks (some with a
# SYNC across the track's end, which no drive workload has)
gcc -O2 -Wall -Wextra -Wno-unused-function -o maps_test maps_test.c
if ./maps_test > /tmp/ff_maps.$$; then printf "%-7s IDENTICAL: %s\n" maps "$(cat /tmp/ff_maps.$$)"
else printf "%-7s DIVERGES: %s\n" maps "$(cat /tmp/ff_maps.$$)"; fail=1; fi
rm -f /tmp/ff_maps.$$
for w in dos stress timers scan iec iecatn step stepwait stepfull write swap; do
  M=; [ $w = step ] || [ $w = stepwait ] || [ $w = write ] && M=multi
  B=$w; [ $w = swap ] && { M=swap; B=scan; }       # swap: wl_scan, with disk swaps
  [ $w = stepfull ] && { M=full; B=stepwait; }    # tracks no framing holds (equiv.c full_tracks)
  ./equiv_ref ../bench/build/wl_$B.bin $N $M > /tmp/ff_eq_ref.$$
  ./equiv_new ../bench/build/wl_$B.bin $N $M > /tmp/ff_eq_new.$$
  # the forced-lookahead build must agree too
  ./equiv_laf ../bench/build/wl_$B.bin $N $M > /tmp/ff_eq_laf.$$
  cmp -s /tmp/ff_eq_new.$$ /tmp/ff_eq_laf.$$ || { printf "%-7s DIVERGES with the lookahead forced\n" $w; fail=1; }
  # and with the lazy disk off: the eager path the lazy one replaces, kept checked (mutate.sh's short-cell rezone)
  ./equiv_eag ../bench/build/wl_$B.bin $N $M > /tmp/ff_eq_eag.$$
  cmp -s /tmp/ff_eq_ref.$$ /tmp/ff_eq_eag.$$ || { printf "%-7s DIVERGES with the lazy disk off\n" $w; fail=1; }
  if cmp -s /tmp/ff_eq_ref.$$ /tmp/ff_eq_new.$$; then
    printf "%-7s IDENTICAL over %s cycles: %s\n" $w $N "$(tail -1 /tmp/ff_eq_new.$$)"
  else
    printf "%-7s DIVERGES. first differing block:\n" $w
    diff /tmp/ff_eq_ref.$$ /tmp/ff_eq_new.$$ | head -4
    fail=1
  fi
done
# F4b: the real DOS reads all 683 sectors of a test disk, stepping over every
# track: the stepper and track changes, which the bench workloads never use.
ROM=${DOS1541:-$HOME/src/vice-3.9/data/DRIVES/dos1541-325302-01+901229-05.bin}
if [ -f "$ROM" ]; then
  gcc -O2 -Wall -Wextra -Wno-unused-function -DREF -o f4b_ref f4b.c
  gcc -O2 -Wall -Wextra -Wno-unused-function       -o f4b f4b.c
  ./f4b_ref "$ROM" rand > /tmp/ff_eq_ref.$$ || true
  ./f4b     "$ROM" rand > /tmp/ff_eq_new.$$ || true
  if cmp -s /tmp/ff_eq_ref.$$ /tmp/ff_eq_new.$$ && grep -q "sectors OK: 683 of 683" /tmp/ff_eq_new.$$ \
     && grep -q "read back OK: 683 of 683, failures 0; decoded from GCR: 683 sectors, identical" /tmp/ff_eq_new.$$ \
     && grep -q "swap: DOS noticed (WPSW) yes; new disk read OK: 70 of 70, failures 0" /tmp/ff_eq_new.$$; then
    printf "%-7s IDENTICAL, 683 of 683 sectors read, written, read back; a disk swap noticed and read, by the real DOS\n" f4b
  else
    printf "%-7s DIVERGES or fails: %s\n" f4b "$(tail -1 /tmp/ff_eq_new.$$)"
    diff /tmp/ff_eq_ref.$$ /tmp/ff_eq_new.$$ | head -4
    fail=1
  fi
else
  echo "f4b     SKIPPED: no DOS ROM at $ROM"; fail=1
fi
# D64 error bytes: the DOS reads a disk with errors built in, a whole track with no SYNC among them (its wait for
# a SYNC there reads PB for 20 ms at a time: core/drive1541.h head_sync_to_pb's short cut), through both models
if [ -f "$ROM" ]; then
  gcc -O2 -Wall -Wextra -Wno-unused-function -DREF -o errb_ref errbytes_test.c
  gcc -O2 -Wall -Wextra -Wno-unused-function       -o errb errbytes_test.c
  timeout 300 ./errb_ref "$ROM" | grep '^T' > /tmp/ff_eq_ref.$$ || true
  timeout 300 ./errb     "$ROM" > /tmp/ff_eq_all.$$ || true; grep '^T' /tmp/ff_eq_all.$$ > /tmp/ff_eq_new.$$ || true
  if cmp -s /tmp/ff_eq_ref.$$ /tmp/ff_eq_new.$$ && grep -q "PASS  683 sectors read by the DOS, 0 wrong" /tmp/ff_eq_all.$$; then
    printf "%-7s IDENTICAL, 683 sectors of a disk with error bytes (a track with no SYNC), by the real DOS\n" errb
  else
    printf "%-7s DIVERGES or fails\n" errb; diff /tmp/ff_eq_ref.$$ /tmp/ff_eq_new.$$ | head -4; fail=1
  fi
  rm -f errb errb_ref /tmp/ff_eq_all.$$
fi
rm -f /tmp/ff_eq_ref.$$ /tmp/ff_eq_new.$$ /tmp/ff_eq_laf.$$ /tmp/ff_eq_eag.$$
exit $fail
