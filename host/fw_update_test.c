/* -------------------------------------------------------------------------
 * fw/update.c on a real FAT image, with arrays for the flash: what each
 * picod64.uf2 on the card leads to.
 *   fw_update_test <img> <picod64.uf2>     (the build's own UF2, as picotool made it)
 * ------------------------------------------------------------------------- */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../fw/update.h"
#include "../fw/storage.h"
#include "../fw/fatfs/ff.h"

int diskio_host_open(const char *path);

static uint8_t running[UPD_MAX], stage[UPD_MAX], img[UPD_MAX], uf2[2 * UPD_MAX + 1024];
static int writes, fail_write;
static int stage_write(uint32_t off, const uint8_t *buf)
{
    if (fail_write || off % UPD_SECTOR || off + UPD_SECTOR > UPD_MAX) return 0;
    memcpy(stage + off, buf, UPD_SECTOR); writes++;
    return 1;
}
static uint8_t work[UPD_WORK];
static const upd_io_t io = { running, stage, stage_write, work };

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
/* a UF2 of img[0..len), as picotool makes one for an RP2350: the E10 block first */
static long make(long len, uint32_t fam)
{
    long o = 0; uint32_t n = (uint32_t)((len + 255) / 256);
    memset(uf2, 0, 512);
    put32(uf2, 0x0A324655u); put32(uf2 + 4, 0x9E5D5157u); put32(uf2 + 8, 0x2000 | 0x8000); put32(uf2 + 12, 0x10FFFF00u);
    put32(uf2 + 16, 256); put32(uf2 + 20, 0); put32(uf2 + 24, 2); put32(uf2 + 28, 0xE48BFF57u); put32(uf2 + 508, 0x0AB16F30u);
    o = 512;
    for (uint32_t i = 0; i < n; i++, o += 512) {
        uint8_t *b = uf2 + o;
        memset(b, 0, 512);
        put32(b, 0x0A324655u); put32(b + 4, 0x9E5D5157u); put32(b + 8, 0x2000); put32(b + 12, 0x10000000u + 256 * i);
        put32(b + 16, 256); put32(b + 20, i); put32(b + 24, n); put32(b + 28, fam);
        memcpy(b + 32, img + 256 * i, 256);
        put32(b + 508, 0x0AB16F30u);
    }
    return o;
}
static int exists(const char *p) { FILINFO fi; return f_stat(p, &fi) == FR_OK; }
static int bad;
static void check(const char *what, int ok) { printf("%s  %s\n", ok ? "ok  " : "FAIL", what); bad += !ok; }

int main(int argc, char **argv)
{
    if (argc < 3 || !diskio_host_open(argv[1]) || !st_init()) { fprintf(stderr, "usage / image / mount failed\n"); return 2; }
    char why[24]; uint32_t size; int r;
    long len = 200000;
    srand(5);
    for (long i = 0; i < len; i++) img[i] = (uint8_t)rand();
    memcpy(img + 70000, UPD_MARK, strlen(UPD_MARK));
    memcpy(running, img, UPD_MAX);
    f_unlink(UPD_FILE); f_unlink(UPD_DONE);

    check("no file: nothing done", upd_check(&io, &size, why, sizeof why) == UPD_NONE && writes == 0);

    long n = make(len, 0xE48BFF59u);
    st_write(UPD_DONE, (const uint8_t *)"old", 3);
    st_write(UPD_FILE, uf2, n);
    r = upd_check(&io, &size, why, sizeof why);
    check("the running firmware: renamed .done (the old .done replaced), nothing written", r == UPD_SAME && !exists(UPD_FILE) && exists(UPD_DONE) && writes == 0);
    check("and the next power-on finds nothing", upd_check(&io, &size, why, sizeof why) == UPD_NONE);

    running[123456] ^= 1;                           /* the running firmware differs in one byte */
    st_write(UPD_FILE, uf2, n);
    r = upd_check(&io, &size, why, sizeof why);
    check("a different firmware: staged, all of it, and the rest of its last sector 0xFF",
          r == UPD_STAGED && size == (uint32_t)((len + 255) / 256 * 256) && !memcmp(stage, img, (size_t)len) && stage[size] == 0xFF && exists(UPD_FILE));
    memcpy(running, stage, size);                   /* the board copies it over and restarts */
    writes = 0;
    check("after the copy: the same, renamed", upd_check(&io, &size, why, sizeof why) == UPD_SAME && writes == 0 && !exists(UPD_FILE));
    running[5] ^= 1;

    struct { const char *what, *why; } bads[] = {
        { "cut short (the last block lost)", "Cut short" }, { "a block out of order", "Out of order" },
        { "for an RP2040", "Not for Pico 2" }, { "not PicoD64's", "Not PicoD64" }, { "not a whole number of blocks", "Not a UF2" },
        { "a block's end magic wrong", "Not a UF2" }, { "over 1 MB", "Too big" }, { "the staging write failing", "Write failed" },
    };
    for (int k = 0; k < (int)(sizeof bads / sizeof bads[0]); k++) {
        long m = make(len, k == 2 ? 0xE48BFF56u : 0xE48BFF59u);
        if (k == 0) m -= 512;
        if (k == 1) { uint8_t t[512]; memcpy(t, uf2 + 512 * 3, 512); memcpy(uf2 + 512 * 3, uf2 + 512 * 4, 512); memcpy(uf2 + 512 * 4, t, 512); }
        if (k == 3) { memset(img + 70000, 0, 20); m = make(len, 0xE48BFF59u); memcpy(img + 70000, UPD_MARK, strlen(UPD_MARK)); }
        if (k == 4) m -= 100;
        if (k == 5) uf2[512 * 7 + 508] ^= 1;
        if (k == 6) for (long b = 512; b < m; b += 512) put32(uf2 + b + 24, UPD_MAX / 256 + 1);
        fail_write = k == 7;
        writes = 0; memset(stage, 0xEE, sizeof stage);
        st_write(UPD_FILE, uf2, m);
        r = upd_check(&io, &size, why, sizeof why);
        char t[96]; snprintf(t, sizeof t, "bad file, %s: \"%s\", left on the card%s", bads[k].what, bads[k].why, k < 7 ? ", flash untouched" : "");
        check(t, r == UPD_BAD && !strcmp(why, bads[k].why) && exists(UPD_FILE) && (k == 7 || writes == 0));
        fail_write = 0;
    }

    /* the build's own UF2 */
    FILE *f = fopen(argv[2], "rb");
    long rl = f ? (long)fread(uf2, 1, sizeof uf2, f) : 0; if (f) fclose(f);
    st_write(UPD_FILE, uf2, rl);
    memset(running, 0xFF, sizeof running);
    r = upd_check(&io, &size, why, sizeof why);
    printf("      the build's UF2: %ld bytes, image %u bytes%s%s\n", rl, size, why[0] ? ", " : "", why);
    check("the build's UF2 is taken and staged", r == UPD_STAGED && size > 100000);
    memcpy(running, stage, size);
    check("and is then the running firmware: renamed", upd_check(&io, &size, why, sizeof why) == UPD_SAME);
    puts(bad ? "FAIL" : "PASS");
    return bad != 0;
}
