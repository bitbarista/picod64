/* fw/stackmark.h: a stack's high-water mark */
#include <stdio.h>
#include "../fw/stackmark.h"
int main(void)
{
    static uint32_t s[256];
    int fails = 0;
    sm_paint(s, s + 256);
    if (sm_used(s, s + 256) != 0) fails++;              /* untouched */
    s[255] = 1;
    if (sm_used(s, s + 256) != 4) fails++;              /* the top word */
    s[100] = 1;
    if (sm_used(s, s + 256) != 156 * 4) fails++;        /* the lowest changed counts, gaps or not */
    s[0] = 1;
    if (sm_used(s, s + 256) != 1024) fails++;           /* all of it */
    puts(fails ? "FAIL" : "PASS");
    return fails != 0;
}
