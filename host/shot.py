#!/usr/bin/env python3
"""Render a host/fwtwin shot: dump as a C64 screen, in the real character set and colours.
   shot.py <dump> <chargen.bin> <out.png> [scale]"""
import sys
from PIL import Image
PAL = ["000000","FFFFFF","883932","67B6BD","8B3F96","55A049","40318D","BFCE72","8B5429","574200","B86962","505050","787878","94E089","7869C4","9F9F9F"]
PAL = [tuple(int(c[i:i+2],16) for i in (0,2,4)) for c in PAL]
d = open(sys.argv[1],"rb").read(); cg = open(sys.argv[2],"rb").read(); out = sys.argv[3]; sc = int(sys.argv[4]) if len(sys.argv)>4 else 3
scr, col, d018, border, bg = d[:1000], d[1000:2000], d[2000], d[2001]&15, d[2002]&15
base = 0x800 if d018 & 2 else 0
im = Image.new("RGB",(320+64,200+64),PAL[border]); px = im.load()
for r in range(25):
    for c in range(40):
        ch = scr[r*40+c]; fg = PAL[col[r*40+c]&15]
        for y in range(8):
            b = cg[base+ch*8+y]
            for x in range(8):
                px[32+c*8+x,32+r*8+y] = fg if b&(0x80>>x) else PAL[bg]
im.resize((im.width*sc,im.height*sc),Image.NEAREST).save(out)
