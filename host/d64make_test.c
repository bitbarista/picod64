/* core/d64make.h against VICE's c1541 (d64make_test.sh): build a D64 of random PRGs and
 * write each file out too, so c1541's extraction can be compared byte for byte.
 *   d64make_test <out.d64> <outdir> */
#include <stdio.h>
#include <stdlib.h>
#include "../core/d64make.h"
static uint8_t d[D64_BYTES], f[40000];
int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    static const uint8_t nm[] = "VIRTUAL";
    d64_blank(d, nm, 7, 'F', 'F');
    uint32_t x = 3;
    static const long sizes[] = { 1, 252, 253, 254, 255, 508, 9000, 30000, 2, 100, 20000, 39000 };
    for (int i = 0; i < 12; i++) {
        long n = sizes[i] + 2;
        f[0] = 0x01; f[1] = 0x08;
        for (long k = 2; k < n; k++) { x = x * 1103515245u + 12345u; f[k] = (uint8_t)(x >> 16); }
        char name[17]; int nl = snprintf(name, sizeof name, "FILE%02d", i);
        int ok = d64_add_prg(d, (const uint8_t *)name, nl, f, n);
        printf("%s %ld bytes: %s\n", name, n, ok ? "added" : "DISK FULL");
        char p[512]; snprintf(p, sizeof p, "%s/%s.prg", argv[2], name);
        FILE *o = fopen(p, "wb"); fwrite(f, 1, (size_t)n, o); fclose(o);
    }
    FILE *o = fopen(argv[1], "wb"); fwrite(d, 1, sizeof d, o); fclose(o);
    return 0;
}
