#!/usr/bin/env python3
"""The walk through every screen that build.sh runs on both builds: writes script.h and script.json.
Row numbers follow the sample card in sim.c; if the list changes, change this too (diff_native prints the screens
with any argument). A new disk and Collection are here because they use %02d, which the broken build gets wrong."""
import json
S = []
def s(op, arg=0, p=0, note=None): S.append((op, arg, p, note))
def W(ms, p=0, n=None): s('W', ms, p, n)
def press(n=None): W(60, 1); W(60, 0, n)
def hold(n=None): W(800, 1); W(60, 0, n)
def turn(k, d=1, n=None):
    for _ in range(k): s('T', d); W(30)
    if n: W(10, 0, n)
s('R', 0); s('P', 0); W(1100, 0, 'no ROM'); s('K', 0); W(1100, 0, 'no card'); s('K', 1); s('R', 1); W(1100, 0, 'status')
turn(1, 1, 'files'); turn(9, 1, 'row 9'); turn(9, -1)
turn(1, -1, 'settings row'); press('settings'); turn(1, 1, 'new disk row'); press('new disk'); W(2100, 0, 'its status')
press('on the disk'); press('pressed again'); W(2100)
press(); turn(6, -1, 'eject row'); press('ejected'); W(2100, 0, 'browse')
press(); press('collection'); turn(30, 1, 'far down'); hold('hold'); W(2100)
press(); turn(2); press('games'); turn(6); press('inserted'); W(2100, 0, 'disk')
hold('next'); W(1700); W(2100, 0, '2 of 3'); hold(); W(1700); hold(); W(1700); W(2100, 0, 'round')
press('on the disk'); turn(3, 1, 'long name'); W(500); W(1500, 0, 'sliding'); W(3000); W(6000)
turn(1, 1, 'settings row'); press('settings'); turn(1, -1, 'sound row'); press('sound off'); turn(1, 1, 'back row'); press('list'); hold('status')
press(); W(16000, 0, 'idle')
s('K', 0); W(100, 0, 'card out'); turn(1); W(100, 0, 'its message'); s('K', 1); W(2100)
s('P', 1); W(300, 1, 'held'); W(100, 0, 'device'); turn(2, 1, 'device 10'); turn(3, -1); press(); W(1200, 0, 'after')
s('F', 1); s('P', 0); W(1200, 0, 'plain font'); turn(1, 1, 'its list'); s('F', 0); s('P', 0); W(1200)
with open('script.h', 'w') as f:
    f.write('typedef struct{char op;int arg;int pushed;const char*note;}step_t;\nstatic const step_t script[]={\n')
    for o, a, p, n in S: f.write("{'%s',%d,%d,%s},\n" % (o, a, p, '"%s"' % n if n else '0'))
    f.write('{0,0,0,0}};\n')
json.dump([{'op': o, 'arg': a, 'pushed': p} for o, a, p, n in S], open('script.json', 'w'))
