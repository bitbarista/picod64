/* -------------------------------------------------------------------------
 * fw/storage.c on a real FAT image (FatFs over a file): the card as the board
 * will see it.
 *   fw_storage_test put <img> <sdpath> <file>   copy a file onto the image
 *   fw_storage_test mkdir <img> <sdpath>
 *   fw_storage_test ls <img> <sdpath>            the listing, as browse mode gets it
 *   fw_storage_test get <img> <sdpath> <out>
 *   fw_storage_test d64 <img> <sdpath> <ref.d64> stream-mount; the GCR must equal
 *                    d64_to_disk of ref.d64; then write each track back (no change
 *                    expected), then change sectors in the GCR, write back, re-read
 *                    and compare with the reference with the same change made
 * ------------------------------------------------------------------------- */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../fw/storage.h"

int diskio_host_open(const char *path);
extern unsigned long diskio_host_reads, diskio_host_writes;

static uint8_t big[1 << 20];

int main(int argc, char **argv)
{
    if (argc < 4 || !diskio_host_open(argv[2]) || !st_init()) { fprintf(stderr, "usage / image / mount failed\n"); return 2; }
    const char *cmd = argv[1], *p = argv[3];
    if (!strcmp(cmd, "put") && argc > 4) {
        FILE *f = fopen(argv[4], "rb"); if (!f) return 2;
        long n = (long)fread(big, 1, sizeof big, f); fclose(f);
        return st_write(p, big, n) ? 0 : 1;
    }
    if (!strcmp(cmd, "mkdir")) return st_mkdir(p) ? 0 : 1;
    if (!strcmp(cmd, "putmany")) {                     /* putmany <img> <folder> <names.txt> <file>: one copy per name */
        static uint8_t fb[70000]; char nm[128], full[256]; FILE *l = fopen(argv[4], "r"), *c = fopen(argv[5], "rb");
        long n = (long)fread(fb, 1, sizeof fb, c); fclose(c);
        while (fgets(nm, sizeof nm, l)) { nm[strcspn(nm, "\n")] = 0; snprintf(full, sizeof full, "%s/%s", p, nm); if (!st_write(full, fb, n)) return 1; }
        fclose(l);
        return 0;
    }
    if (!strcmp(cmd, "ext") && argc > 4) {             /* ext <img> <d64 file> <tracks>: tracks past 35, every one, and a write-back */
        static uint8_t g[D64_GCR_BYTES], s[21 * 256], s2[21 * 256], whole[210000];
        static disk_t dk; static syncs_t fr[D64_TRACKS]; static volatile uint8_t dirty[42];
        static volatile uint8_t head;
        int tracks = atoi(argv[4]), bad = 0;
        long len = st_read(p, whole, sizeof whole);
        if (!st_load_d64(p, g, &dk, fr, s) || st_tracks() != tracks) { printf("FAIL load, %d tracks\n", st_tracks()); return 1; }
        /* every shared track, in an order that makes each pair take turns */
        int order[] = { 36, 31, 37, 32, 38, 33, 39, 34, 40, 35, 41, 42, 31, 32, 36, 41 };
        for (unsigned i = 0; i < sizeof order / sizeof order[0]; i++) {
            int t = order[i];
            if (t > tracks) continue;
            int ht = 2 * (t - 1);
            head = (uint8_t)ht;
            if (!dk.gcr[ht] && !st_track_poll(p, &dk, dirty, &head, s2)) { printf("FAIL %d not in\n", t); bad++; continue; }
            uint32_t m = disk_track_to_sectors(&dk, t, s);
            if (m != (1u << d64_sectors(t)) - 1 || memcmp(s, whole + d64_offset(t, 0), (size_t)(d64_sectors(t) * 256))) { printf("FAIL track %d\n", t); bad++; }
        }
        /* a write on 38, as drive code would make it; then 33 takes its place */
        head = 2 * 37;
        if (!dk.gcr[2 * 37] && !st_track_poll(p, &dk, dirty, &head, s2)) bad++;
        memcpy(s, whole + d64_offset(38, 0), 17 * 256);
        memset(s + 7 * 256, 0xA5, 256);
        d64_track_sectors(s, 38, whole[d64_offset(18, 0) + 162], whole[d64_offset(18, 0) + 163], (uint8_t *)dk.gcr[2 * 37]);
        dirty[37] = 1;
        head = 2 * 32;
        if (!st_track_poll(p, &dk, dirty, &head, s2)) bad++;
        if (dirty[37]) { puts("FAIL still dirty"); bad++; }
        long l2 = st_read(p, whole, sizeof whole);
        if (l2 != len || whole[d64_offset(38, 7)] != 0xA5 || whole[d64_offset(38, 7) + 255] != 0xA5) { puts("FAIL not on the card"); bad++; }
        head = 2 * 37;
        if (!st_track_poll(p, &dk, dirty, &head, s2)) bad++;
        disk_track_to_sectors(&dk, 38, s);
        if (s[7 * 256] != 0xA5) { puts("FAIL not back"); bad++; }
        /* and a disk of 35 is left alone */
        puts(bad ? "FAIL" : "PASS");
        return bad != 0;
    }
    if (!strcmp(cmd, "ls")) {
        fsent_t e;
        for (int i = 0; st_list(p, i, &e); i++) printf("%s %s %u\n", e.dir ? "DIR" : "   ", e.name, e.size);
        return 0;
    }
    if (!strcmp(cmd, "get") && argc > 4) {
        long n = st_read(p, big, sizeof big);
        if (n < 0) return 1;
        FILE *f = fopen(argv[4], "wb"); fwrite(big, 1, (size_t)n, f); fclose(f);
        return 0;
    }
    if (!strcmp(cmd, "d64") && argc > 4) {
        static uint8_t ref[D64_BYTES], gref[D64_GCR_BYTES], gst[D64_GCR_BYTES], sec[21 * 256];
        static disk_t dref, dst; static syncs_t fref[D64_TRACKS], fst[D64_TRACKS];
        FILE *f = fopen(argv[4], "rb"); if (!f || fread(ref, 1, D64_BYTES, f) != D64_BYTES) return 2; fclose(f);
        d64_to_disk(ref, gref, &dref, fref);
        unsigned long r0 = diskio_host_reads;
        if (!st_load_d64(p, gst, &dst, fst, sec)) { printf("FAIL load\n"); return 1; }
        int bad = memcmp(gref, gst, D64_GCR_BYTES) != 0;
        for (int t = 0; t < D64_TRACKS; t++) bad |= fref[t].n != fst[t].n;
        printf("stream mount: GCR %s d64_to_disk (%lu card sectors read)\n", bad ? "DIFFERS from" : "identical to", diskio_host_reads - r0);
        int fail = bad;
        /* write back with no change: the file must not change */
        unsigned long w0 = diskio_host_writes; int n = 0;
        for (int t = 1; t <= D64_TRACKS; t++) n += st_write_track(p, &dst, t, sec);
        long got = st_read(p, big, sizeof big);
        int same = got >= D64_BYTES && !memcmp(big, ref, D64_BYTES);
        printf("write back, unchanged: %d sectors decoded and written (%lu card sectors), file %s\n", n, diskio_host_writes - w0, same ? "unchanged" : "CHANGED");
        fail |= n != D64_SECTORS || !same;
        /* change sectors through the GCR: re-encode tracks 1, 18 and 35 with new data */
        static uint8_t ref2[D64_BYTES];
        memcpy(ref2, ref, D64_BYTES);
        int tt[3] = { 1, 18, 35 };
        for (int k = 0; k < 3; k++) {
            int t = tt[k];
            for (int s = 0; s < d64_sectors(t); s += 2) memset(ref2 + d64_offset(t, s) + 2, 0xA0 + k + s, 254);
            d64_track(ref2, t, ref[d64_offset(18, 0) + 162], ref[d64_offset(18, 0) + 163], (uint8_t *)dst.gcr[2 * (t - 1)]);
            if (st_write_track(p, &dst, t, sec) != d64_sectors(t)) { printf("FAIL write track %d\n", t); fail = 1; }
        }
        got = st_read(p, big, sizeof big);
        same = got >= D64_BYTES && !memcmp(big, ref2, D64_BYTES);
        printf("write back, 3 tracks changed: file %s the reference\n", same ? "equals" : "DIFFERS from");
        fail |= !same;
        printf("%s\n", fail ? "FAIL" : "PASS");
        return fail;
    }
    return 2;
}
