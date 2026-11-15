#!/bin/sh
# Builds panel-demo.html (the front panel demo) from fw/ui.c itself, and the pictures from render_case.py. Needs clang and wasm-ld (package lld-21; WASMLD= its path if not on PATH).
# Then checks the browser build against a native one with glibc, frame for frame, and that the check can fail.
set -e
cd "$(dirname "$0")"
LD=${WASMLD:-wasm-ld}
OUT=${OUT:-${TMPDIR:-/tmp}/paneldemo}; mkdir -p "$OUT"
EX="sim_power sim_tick sim_turn sim_card sim_rom sim_font_file sim_card_reset sim_online sim_screen sim_booted sim_swapping sim_sound sim_device sim_card_in sim_mounted sim_dir sim_made sim_fb"
wasm() { # out flags
  o=$1; shift
  clang --target=wasm32 -O2 -ffreestanding -fno-builtin -nostdlibinc -Ilibc -w "$@" -c sim.c -o "$OUT/sim.o"
  clang --target=wasm32 -O2 -ffreestanding -fno-builtin -nostdlibinc -Ilibc -w "$@" -c libc/libc.c -o "$OUT/libc.o"
  args=""; for e in $EX; do args="$args --export=$e"; done
  "$LD" --no-entry --strip-all --gc-sections -z stack-size=262144 $args "$OUT/sim.o" "$OUT/libc.o" -o "$o"
}
wasm "$OUT/sim.wasm"
python3 gen_script.py
gcc -O1 -w -I. -o "$OUT/diff_native" diff_native.c
"$OUT/diff_native" > "$OUT/native.txt"
node diff.mjs "$OUT/sim.wasm" script.json > "$OUT/wasm.txt"
cmp "$OUT/native.txt" "$OUT/wasm.txt"
echo "ok   browser build = native build, $(wc -l < "$OUT/native.txt") steps"
wasm "$OUT/broken.wasm" -DBREAK_PAD
node diff.mjs "$OUT/broken.wasm" script.json > "$OUT/broken.txt"
if cmp -s "$OUT/native.txt" "$OUT/broken.txt"; then echo "FAIL the comparison can't see a broken build"; exit 1; fi
echo "ok   a deliberately broken build is caught"
python3 - "$OUT/sim.wasm" <<'P'
import base64,sys
h=open('page.html').read(); assert h.count('@@WASM@@')==1
uri=lambda f:'data:image/png;base64,'+base64.b64encode(open('img/'+f,'rb').read()).decode()
for k,v in (('@@TOP@@',uri('case-top.png')),('@@KNOB@@',uri('knob.png')),('@@VIEW@@',uri('case-view.png')),('@@GEOM@@',open('img/geom.json').read()),('@@WASM@@',base64.b64encode(open(sys.argv[1],'rb').read()).decode())):
    assert h.count(k)==1,k; h=h.replace(k,v)
open('panel-demo.html','w').write(h)
P
echo "ok   panel-demo.html $(wc -c < panel-demo.html) bytes"
