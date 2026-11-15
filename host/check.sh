#!/bin/sh
# Every host check, in order, stopping at the first failure. Exit 0 = all pass.
#   sh host/check.sh
# ⚠ Do not pipe these scripts: a pipe returns the LAST command's status, and
# "equiv.sh | cut" hid a divergence on 2026-10-25.
set -e
cd "$(dirname "$0")"
sh run.sh
sh equiv.sh 20000000
sh mutate.sh
sh f4c.sh
sh f6.sh
gcc -O2 -Wall -Wextra -o /tmp/fw_sound_test.$$ fw_sound_test.c && /tmp/fw_sound_test.$$ > /dev/null   # the drive sounds
gcc -O2 -DSND_TEST_NO_BUMP -o /tmp/fw_sound_bad.$$ fw_sound_test.c
if /tmp/fw_sound_bad.$$ >/dev/null; then echo "FAIL  the sound test passes without the knock"; exit 1; fi; rm -f /tmp/fw_sound_test.$$ /tmp/fw_sound_bad.$$
sh fwtwin.sh            # the board firmware (fw/) on a FAT image
echo "ALL HOST CHECKS PASS"
