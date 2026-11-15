/* fw/diskset.c: "next disk" on folders of real-world-style names */
#include <stdio.h>
#include <string.h>
#include "../fw/diskset.h"

static const char **names; static int nn;
static int list(const char *path, int idx, fsent_t *e)
{
    (void)path;
    if (idx >= nn) return 0;
    strcpy(e->name, names[idx]); e->dir = 0; e->size = 174848;
    return 1;
}

static int fails;
static void expect(const char **folder, int n, const char *cur, const char *want, int wpos, int wcount)
{
    names = folder; nn = n;
    char out[64] = ""; int pos = 0, count = 0;
    int r = ds_next("/", cur, out, sizeof out, list, &pos, &count);
    int ok = want ? (r && !strcmp(out, want) && pos == wpos && count == wcount) : !r;
    printf("%s  %-28s -> %-28s %d/%d\n", ok ? "ok  " : "FAIL", cur, r ? out : "(none)", pos, count);
    if (!ok) { fails++; if (want) printf("      wanted %s %d/%d\n", want, wpos, wcount); }
}

int main(void)
{
    static const char *a[] = { "Edge of Disgrace-1.d64", "Edge of Disgrace-2.d64", "Edge of Disgrace-3.d64", "other.d64" };
    expect(a, 4, "Edge of Disgrace-1.d64", "Edge of Disgrace-2.d64", 2, 3);
    expect(a, 4, "Edge of Disgrace-3.d64", "Edge of Disgrace-1.d64", 1, 3);
    static const char *b[] = { "Game (Disk 1 of 2).d64", "Game (Disk 2 of 2).d64", "Readme.prg" };
    expect(b, 3, "Game (Disk 1 of 2).d64", "Game (Disk 2 of 2).d64", 2, 2);
    expect(b, 3, "Game (Disk 2 of 2).d64", "Game (Disk 1 of 2).d64", 1, 2);
    static const char *c[] = { "elite side a.d64", "elite side b.d64" };
    expect(c, 2, "elite side a.d64", "elite side b.d64", 2, 2);
    static const char *d[] = { "demo_s1.d64", "demo_s2.d64", "demo_s10.d64" };
    expect(d, 3, "demo_s2.d64", "demo_s10.d64", 3, 3);              /* numeric, not alphabetical */
    static const char *e[] = { "Mayhem disk1.d64", "Mayhem disk2.d64", "Turrican 2.d64" };
    expect(e, 3, "Mayhem disk2.d64", "Mayhem disk1.d64", 1, 2);       /* "Turrican 2" is not in the set */
    static const char *f[] = { "1994 demo.d64", "1995 demo.d64", "alpha.d64" };
    expect(f, 3, "1994 demo.d64", "1995 demo.d64", 2, 3);            /* years: no set, folder order */
    static const char *g[] = { "solo.d64" };
    expect(g, 1, "solo.d64", 0, 0, 0);                                /* nothing to go to */
    static const char *h[] = { "Arkanoid.d64", "Boulder Dash.d64", "Commando.d64" };
    expect(h, 3, "Commando.d64", "Arkanoid.d64", 1, 3);              /* no set: folder order, wraps */
    static const char *j[] = { "edge_0.d64", "edge_1a.d64", "edge_1b.d64" };   /* Edge of Disgrace's names */
    expect(j, 3, "edge_0.d64", "edge_1a.d64", 2, 3);
    expect(j, 3, "edge_1a.d64", "edge_1b.d64", 3, 3);
    expect(j, 3, "edge_1b.d64", "edge_0.d64", 1, 3);
    static const char *k[] = { "alpha_1a.d64", "alpha_1b.d64", "alpha_2a.d64", "beta_1a.d64", "beta_1b.d64" };
    expect(k, 5, "alpha_1b.d64", "alpha_2a.d64", 3, 3);
    expect(k, 5, "alpha_2a.d64", "alpha_1a.d64", 1, 3);               /* stays in its game: not on to beta */
    expect(k, 5, "beta_1b.d64", "beta_1a.d64", 1, 2);
    printf("%s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
