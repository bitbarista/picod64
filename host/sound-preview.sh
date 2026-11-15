#!/bin/sh
# Hear the drive's sounds without a board: the real DOS and KERNAL on the firmware's twin, a
# load, a head bump and a browse-mode load, through fw/sound.h, then through a rough model of
# the piezo. The model is a guess (no response curve held): it shows what is being asked of
# the piezo, not how loud or how bright it will be. Only the board can say.
#   sh host/sound-preview.sh [outdir]        default ~/PicoD64-sound
set -e
cd "$(dirname "$0")"
OUT=${1:-$HOME/PicoD64-sound}; mkdir -p "$OUT"
DOS=${DOS1541:-$HOME/src/vice-3.9/data/DRIVES/dos1541-325302-01+901229-05.bin}
K=${KERNAL:-$HOME/.local/share/vice/C64/kernal-901227-03.bin}
B=${BASIC:-$HOME/.local/share/vice/C64/basic-901226-01.bin}
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
F="../fw/svc.c ../fw/storage.c ../fw/diskio_host.c ../fw/fatfs/ff.c ../fw/fatfs/ffunicode.c"
gcc -O2 -w -o "$W/fwtwin" fwtwin.c c64.c $F
gcc -O2 -w -o "$W/st" fw_storage_test.c $F
python3 -I -c "import random,sys; r=random.Random(5); open(sys.argv[1],'wb').write(bytes([1,8])+r.randbytes(12000))" "$W/g.prg"
c1541 -format "sounds,sd" d64 "$W/g.d64" >/dev/null 2>&1; c1541 "$W/g.d64" -write "$W/g.prg" game >/dev/null 2>&1
truncate -s 64M "$W/c.img"; /usr/sbin/mkfs.vfat -F 32 "$W/c.img" >/dev/null
"$W/st" put "$W/c.img" /g.d64 "$W/g.d64"; "$W/st" put "$W/c.img" /game.prg "$W/g.prg"
mkdir "$W/o"
run() { n=$1; shift; echo "rendering $n"; FWSND="$W/$n.raw" "$W/fwtwin" "$DOS" "$K" "$B" "$W/c.img" "$W/o" "$@" | grep -E 'TIMEOUT|FAIL' || true; }
run load  wait:0.5 ui:mount:/g.d64 'load:$' wait:1 load:game wait:3
run bump  wait:0.5 ui:mount:/g.d64 'cmd:N0:BLANK,ID' wait:1
run browse wait:0.5 load:game.prg wait:3 '$' wait:3
for n in load bump browse; do
  L=""; [ $n = bump ] && L="-t 20"
  # the duty as a signal: from silence, so the filters don't start with a pop
  python3 -I -c "import sys,array; d=open(sys.argv[1],'rb').read(); a=array.array('h',(v*200 for v in d)); open(sys.argv[2],'wb').write(a.tobytes())" "$W/$n.raw" "$W/$n.s16"
  ffmpeg -y -loglevel error -f s16le -ar 15625 -ac 1 -i "$W/$n.s16" $L \
    -af "aresample=44100,highpass=f=700:p=2,equalizer=f=4000:t=q:w=2.5:g=14,lowpass=f=9000:p=2,alimiter=limit=0.95" \
    -c:a pcm_s16le "$OUT/$n.wav"
  python3 -I - "$W/$n.raw" "$n" <<'P'
import sys
d=open(sys.argv[1],'rb').read(); fs=15625
sec=[max(d[i:i+fs//4] or [0]) for i in range(0,len(d),fs//4)]
print('  %s: %.1f s, duty peak %d, %d%% of the time silent'%(sys.argv[2],len(d)/fs,max(d),100*d.count(0)//len(d)))
P
done
echo "wav files in $OUT"
