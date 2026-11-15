/* -------------------------------------------------------------------------
 * PicoD64 -- "next disk" (F9, 2026-10-30): which D64 of a set follows
 * the mounted one. A set = the D64s of one folder whose names differ only in
 * one disk/side token: the last number or single letter in the name that
 * stands alone (after a separator or one of disk/side/part/d/s) -- "Demo-1",
 * "demo (Disk 2 of 3)", "Game side B", "x_s2", "demo_1a" (disk 1, side a:
 * 2026-11-03, Edge of Disgrace's 0, 1a, 1b). Numbers count up, letters a-z;
 * after the last comes the first. No set: the next D64 in the folder (wraps).
 * ------------------------------------------------------------------------- */
#include <string.h>
#include <ctype.h>
#include "diskset.h"

static int is_d64(const char *n)
{
    size_t l = strlen(n);
    return l > 4 && n[l - 4] == '.' && tolower((unsigned char)n[l - 3]) == 'd' && n[l - 2] == '6' && n[l - 1] == '4';
}

/* split name into key (token replaced by '#') and the token's value
 * (number, or 1000 + letter). 0 if no token. */
int ds_token(const char *name, char *key, int keymax, int *val)
{
    char low[DS_NAME];
    int l = (int)strlen(name);
    if (l >= DS_NAME || !is_d64(name)) return 0;
    l -= 4;                                            /* without ".d64" */
    for (int i = 0; i < l; i++) low[i] = (char)tolower((unsigned char)name[i]);
    low[l] = 0;
    /* " of N)" / " of N" after the token: "(disk 2 of 3)" -- look before it */
    int end = l;
    for (int i = l - 1; i >= 3; i--)
        if (!strncmp(low + i - 3, " of ", 4)) {       /* only "of <digits>" right at the end */
            int k = i + 1;
            while (k < l && isdigit((unsigned char)low[k])) k++;
            while (k < l && (low[k] == ')' || low[k] == ']' || low[k] == ' ')) k++;
            if (k > i + 1 && k == l && isdigit((unsigned char)low[i + 1])) end = i - 3;
            break;
        }
    /* the last stand-alone number or letter before end */
    for (int i = end - 1; i >= 0; i--) {
        int a = i, b = i + 1, v;
        int side = 0;                                  /* the a of "1a" */
        if (low[i] >= 'a' && low[i] <= 'z' && i > 0 && isdigit((unsigned char)low[i - 1]) && (b >= l || !isalnum((unsigned char)low[b]))) {
            side = low[i] - 'a' + 1;
            a = i - 1;
        }
        if (side || isdigit((unsigned char)low[i])) {
            while (a > 0 && isdigit((unsigned char)low[a - 1])) a--;
            v = 0; for (int k = a; k < (side ? b - 1 : b); k++) v = v * 10 + (low[k] - '0');
            v = v * 100 + side;                        /* 0, 1a, 1b, 2 in order */
        } else if (low[i] >= 'a' && low[i] <= 'z' && (i == 0 || !isalpha((unsigned char)low[i - 1])) &&
                   (b >= l || !isalnum((unsigned char)low[b]))) {
            v = 1000000 + low[i];
        } else continue;
        /* it must stand alone: a separator, the start, or a keyword before it; nothing alnum after */
        if (b < l && isalnum((unsigned char)low[b])) continue;
        int ok = a == 0 || strchr(" -_.(#[", low[a - 1]) != 0;
        static const char *kw[] = { "disk", "disc", "side", "part", "d", "s" };
        for (unsigned k = 0; !ok && k < sizeof kw / sizeof kw[0]; k++) {
            int kl = (int)strlen(kw[k]);
            if (a >= kl && !strncmp(low + a - kl, kw[k], (size_t)kl) && (a == kl || !isalpha((unsigned char)low[a - kl - 1]))) ok = 1;
        }
        if (!ok) continue;
        if (b - a - (side ? 1 : 0) > 3) continue;       /* a year, a long number: not a disk */
        int n = 0;
        for (int k = 0; k < a && n < keymax - 2; k++) key[n++] = low[k];
        key[n++] = '#';
        for (int k = b; k < l && n < keymax - 1; k++) key[n++] = low[k];
        key[n] = 0;
        *val = v;
        return 1;
    }
    return 0;
}

int ds_next(const char *dir, const char *cur, char *out, int outmax,
            int (*list)(const char *path, int idx, fsent_t *e), int *pos, int *count)
{
    char key[DS_NAME], k2[DS_NAME];
    int v, v2;
    fsent_t e;
    int have = ds_token(cur, key, sizeof key, &v);
    /* in a set: the next value up, else the smallest (wrap) */
    int best = -1, first = -1, bestv = 0, firstv = 0, n = 0, below = 0;
    char bestn[BR_NAME] = "", firstn[BR_NAME] = "";
    if (have) {
        for (int i = 0; list(dir, i, &e); i++) {
            if (e.dir || !ds_token(e.name, k2, sizeof k2, &v2) || strcmp(k2, key)) continue;
            n++;
            if (v2 < v) below++;
            if (v2 > v && (best < 0 || v2 < bestv)) { best = i; bestv = v2; strcpy(bestn, e.name); }
            if (first < 0 || v2 < firstv) { first = i; firstv = v2; strcpy(firstn, e.name); }
        }
    }
    if (have && n > 1) {
        const char *pick = best >= 0 ? bestn : firstn;
        strncpy(out, pick, (size_t)outmax - 1); out[outmax - 1] = 0;
        if (pos) *pos = best >= 0 ? below + 2 : 1;     /* 1-based position of the disk picked */
        if (count) *count = n;
        return 1;
    }
    /* no set: the next D64 in folder order */
    int seen = 0, idx = 0, nd = 0, curi = -1;
    for (int i = 0; list(dir, i, &e); i++) {
        if (e.dir || !is_d64(e.name)) continue;
        if (!strcmp(e.name, cur)) curi = nd;
        nd++;
    }
    if (nd < 2) return 0;
    int want = (curi + 1) % nd;
    for (int i = 0; list(dir, i, &e); i++) {
        if (e.dir || !is_d64(e.name)) continue;
        if (idx++ == want) { strncpy(out, e.name, (size_t)outmax - 1); out[outmax - 1] = 0; seen = 1; break; }
    }
    if (pos) *pos = want + 1;
    if (count) *count = nd;
    return seen;
}
