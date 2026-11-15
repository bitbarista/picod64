#!/bin/sh
# Proves host/equiv.sh CAN fail: plants one bug at a time in the event-driven
# core and requires equiv.sh to catch every one. A check that cannot fail
# proves nothing. Sources are restored after each mutant, and verified at the end.
#   sh host/mutate.sh
# ⚠ ON A PRIVATE COPY. It used to mutate the live core/ and restore it after each
# mutant, so an edit made while it ran was silently overwritten (2026-10-29: the
# drive snapshot functions vanished). Now it copies core/, host/ and bench/ to a
# scratch tree and mutates only that; the real sources are never written.
HERE="$(cd "$(dirname "$0")" && pwd)"
W=$(mktemp -d)
cp -r "$HERE/../core" "$HERE/../host" "$HERE/../bench" "$W/"
cd "$W/host"
T=$(mktemp -d); cp ../core/via6522.h ../core/drive1541.h ../core/disk.h "$T/"
restore() { cp "$T/via6522.h" "$T/drive1541.h" "$T/disk.h" ../core/; }
trap 'rm -rf "$T" "$W"' EXIT INT TERM
missed=0
# ⚠ A MUTANT IS ONLY "CAUGHT" IF THE BASELINE PASSES. Against a failing
# baseline every mutant looks caught (2026-10-25), so check first.
sh equiv.sh ${N:-3000000} > "$T/base" 2>&1 || { echo "  *** baseline fails equivalence; mutants mean nothing"; cat "$T/base"; exit 2; }
mut() {  # file old new label
  python3 -I -c "
import sys; p,o,n=sys.argv[1:4]; s=open(p).read(); assert o in s, 'mutation site gone: '+o; open(p,'w').write(s.replace(o,n,1))" "$1" "$2" "$3" || { echo "  *** $4: site not found"; missed=1; restore; return; }
  printf "%-32s " "$4"
  if sh equiv.sh ${N:-3000000} > "$T/out" 2>&1; then echo "NOT CAUGHT"; missed=1
  else echo "caught: $(grep DIVERGES "$T/out" | awk '{print $1}' | tr '\n' ' ')"; fi
  restore
}
V=../core/via6522.h; D=../core/drive1541.h; K=../core/disk.h
mut $V "v->t1_ev = T1_RELOAD; v->t1_next = now + 1;" "v->t1_ev = T1_RELOAD; v->t1_next = now + 2;" "T1 free-run period N+3"
mut $V "v->t1_next = now + v->t1_bval + 1u;" "v->t1_next = now + v->t1_bval + 2u;" "T1 underflow one cycle late"
mut $V "v->t1_bval = via_t1(v, now); v->t1_base = now;" "" "T1 no rebase on reschedule"
mut $V "case 0xB: v->acr = d; via_sched_t1(v, now); break;" "case 0xB: v->acr = d; break;" "ACR write not rescheduling"
mut $V "v->t2_next = now + v->t2_bval + 1u;" "v->t2_next = now + v->t2_bval;" "T2 one cycle early"
mut $V "return (uint16_t)(v->t2_bval - (now - v->t2_base));" "return (uint16_t)(v->t2_bval - (now - v->t2_base) + 1);" "T2 read off by one"
mut $V "v->t1_base = now; v->t1_bval = v->t1l; via_sched_t1(v, now);
        } else" "v->t1_base = now; v->t1_bval = (uint16_t)(v->t1l + 1); via_sched_t1(v, now);
        } else" "T1 reload value off by one"
mut $D "    D.h.bitpos = (uint32_t)((uint64_t)D.h.bitpos * D.trk_bits / old_bits);" "" "step: angle not kept"
mut $D "    head_unmap(&D.h);
    if (D.wmode) head_remap();" "    if (D.wmode) head_remap();" "step: framing not rebuilt"
mut $D "    D.h.canon = 0; D.h.since = 0;" "    D.h.since = 0;" "step: old framing maps kept"
mut $D "    if (h->sync && h->since >= 10 && D.s1 != HEAD_NONE) h->canon = 1;" "    if (h->sync && D.s1 != HEAD_NONE) h->canon = 1;" "step: canon on old track's bits"
mut $D "    else if (ph == ((D.phase + 3) & 3)) { if (ht > 0) ht--; }" "    else if (ph == ((D.phase + 3) & 3)) { if (ht > 1) ht--; }" "stepper stop one half-track in"
mut $D "        while (!head_step(&D.h, cell, 1)) { }" "        while (!head_step(&D.h, cell, 0)) { }" "write: bits not put on the track"
mut $D "        D.h.wsr = drive_pa_out();" "" "write: shift register not reloaded"
mut $D "    if (w) head_unmap(&D.h);           /* the fast path's framing, made explicit */
    else head_remap();" "    if (w) head_unmap(&D.h);" "write: maps not rebuilt after"
mut $D "        head_commit(now); via_write(&D.via2, (uint8_t)a, v, now); drive_mode(); head_find_next();" "        via_write(&D.via2, (uint8_t)a, v, now); drive_mode(); head_find_next();" "write: mode change not committed"
mut $D "static void drive_wps(int light) { D.via2.pb_in = (uint8_t)((D.via2.pb_in & ~0x10) | (light ? 0x10 : 0)); }" "static void drive_wps(int light) { (void)light; }" "swap: sensor never changes"
mut $D "    head_commit(now);
    head_unmap(&D.h);
    if (D.wmode) head_remap();         /* leaving a track written on */
    uint32_t old_bits = D.trk_bits;
    D.disk = dk" "    head_unmap(&D.h);
    if (D.wmode) head_remap();         /* leaving a track written on */
    uint32_t old_bits = D.trk_bits;
    D.disk = dk" "swap: head not committed first"
mut $D "        uint32_t c = P <= q ? q + 8 : q + 8 * ((P - q + 7) >> 3);" "        uint32_t c = P <= q ? q + 8 : q + 8 * ((P - q + 8) >> 3);" "runs: next byte skips one"
mut $K "            uint32_t f = a + 9, l = n + lead0 - 1;" "            uint32_t f = a + 9, l = n - 1;" "runs: SYNC across the wrap cut"
# the lazy disk's skip (2026-11-11): every part of the state it leaves must be checked
mut $D "    D.bytes_read += cnt;" "    D.bytes_read += cnt + 1;" "skip: byte count off by one"
mut $D "    D.via2.pa_in = byte_ending_at(e);" "    D.via2.pa_in = byte_ending_at(e ? e - 1 : e);" "skip: the wrong byte latched"
mut $D "    if (via_ca2_high(&D.via2) && (int32_t)(D.h.at - D.v_over) > 0) D.so_pending = 1;" "" "skip: its SO lost"
mut $D "    uint32_t j = off + 1, k = (j * cell - D.h.acc + 15) >> 4;" "    uint32_t j = off + 2, k = (j * cell - D.h.acc + 15) >> 4;" "skip: one bit late"
mut $K "        if (p + 7 > R - 1) byte &= (0xFF00u >> (R - p)) & 0xFF;" "" "span rebuild: last byte not masked"
mut $K "while (L >= 0 && disk_tbit(trk, (uint32_t)L)) L--;" "" "span rebuild: window not widened left"
mut $K "    if (in && !syncs_add(&w, a, R - 1)) { disk_frame_track(trk, n, r); return; }" "" "span rebuild: run up to R dropped"
mut $D "disk_frame_span(D.trk, D.trk_bits, r, D.wfirst, c);" "disk_frame_span(D.trk, D.trk_bits, r, D.wfirst + 1, c);" "span rebuild: written span off by one"
mut $D "    h->bitcnt = h->sync ? 0 : (uint8_t)((P - D.syncs->last[i]) & 7);" "    h->bitcnt = h->sync ? 0 : (uint8_t)((P - D.syncs->last[i] + 1) & 7);" "runs: unmap bit count off by one"
mut $D "head_commit(now); via_write(&D.via2, (uint8_t)a, v, now); drive_stepper(); head_find_next();" "via_write(&D.via2, (uint8_t)a, v, now); drive_stepper(); head_find_next();" "zone change: no commit at old cell"
mut $D "        if ((a & 15) == 0) head_sync_to_pb(now);" "" "SYNC not brought up to date"
mut $D "    uint32_t k = (j * cell - D.hnext.acc + 15) >> 4;" "    uint32_t k = (j * cell - D.hnext.acc + 16) >> 4;" "byte timing: ceil off by one"
mut $D "    if (D.hnext.acc >= cell) {" "    if (0) {" "short-cell rezone not handled"
mut $D "        sync = (uint8_t)is_sync_bit(last);" "        sync = (uint8_t)is_sync_bit(last + 1 == D.trk_bits ? 0 : last + 1);" "SYNC read one bit ahead"
# ⚠ the wrap byte takes only the top bit of trk[0] ($FF); trk[1] ($FF) and
# trk[nb-1] ($E7) have the same top bit, so wrapping to either was an
# EQUIVALENT mutant. Not wrapping at all (reading 0) is the real bug.
mut $D "    uint16_t w = (uint16_t)((D.trk[b] << 8) | D.trk[b + 1 < nb ? b + 1 : 0]);" "    uint16_t w = (uint16_t)((D.trk[b] << 8) | (b + 1 < nb ? D.trk[b + 1] : 0));" "byte value wrong at track wrap"
mut $D "D.irq = (uint8_t)(via_irq(&D.via1) | via_irq(&D.via2));" "D.irq = (uint8_t)via_irq(&D.via2);" "VIA1 IRQ dropped"
mut $D "    return via_ca2_high(&D.via2);" "    return 1;" "SO ignores SOE"
mut $D "    D.head_next = D.h.at + (uint32_t)((8 * head_cell() - D.h.acc + 15) >> 4);" "    D.head_next = D.h.at + (uint32_t)((8 * head_cell() - D.h.acc + 31) >> 4);" "lookahead bound one cycle late"
restore
cmp -s "$T/via6522.h" ../core/via6522.h && cmp -s "$T/drive1541.h" ../core/drive1541.h || { echo "  *** SOURCES NOT RESTORED"; exit 2; }
[ $missed = 0 ] && echo "PASS: every mutant caught" || { echo "FAIL: a mutant survived"; exit 1; }
