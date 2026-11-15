#!/bin/bash
# Flash bench/build/ff_bench.uf2 and capture one full report.
#   bash bench/flash_run.sh [logfile]        UF2=bench/build/ff_f5.uf2 bash bench/flash_run.sh
# If the bench firmware is running, it is sent 'B' (reboot to BOOTSEL), so no
# button is needed. Otherwise hold BOOTSEL and plug in; this waits for the drive.
# ⚠ Delays are plain sleep. "read -t" on /dev/zero and "timeout tail -f"
# both returned at once when run in the background, so waits never waited.
LOG=${1:-/dev/stdout}
UF2=${UF2:-"$(dirname "$0")/build/ff_bench.uf2"}
MNT=${RP2350_MNT:-/run/media/$USER/RP2350}
wait_for() { n=0; until [ -e "$1" ]; do n=$((n+1)); [ $n -gt ${2:-240} ] && return 1; sleep 0.5; done; }
# ⚠ The firmware only reads serial between sweeps, so one 'B' sent mid-sweep
# was lost (2026-10-25): keep sending it until the drive appears.
echo "waiting for $MNT from $(date +%T)" >> "$LOG"
n=0
until [ -d "$MNT" ]; do
  n=$((n+1)); [ $n -gt 240 ] && break
  if [ -e /dev/ttyACM0 ] && [ $((n % 4)) = 1 ]; then
    stty -F /dev/ttyACM0 raw -echo 115200 2>/dev/null; printf B > /dev/ttyACM0 2>/dev/null
  fi
  sleep 0.5
done
wait_for "$MNT" 1 || { echo "no RP2350 drive after 2 min: hold BOOTSEL and plug in" >> "$LOG"; exit 1; }
sleep 1
cp "$UF2" "$MNT/" && sync
echo "flashed $(date +%T)" >> "$LOG"
sleep 3          # let it re-enumerate as serial
wait_for /dev/ttyACM0 60 || { echo "NO SERIAL" >> "$LOG"; exit 1; }
sleep 1
stty -F /dev/ttyACM0 raw -echo 115200
# the report ends with "(end of report)"; stop there
timeout ${REPORT_TIMEOUT:-300} sh -c "cat /dev/ttyACM0 | tr -d '\r' | sed '/^(end of report)/q'" >> "$LOG"
