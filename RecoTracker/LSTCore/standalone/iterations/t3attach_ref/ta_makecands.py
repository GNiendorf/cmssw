#!/usr/bin/env python3
"""Reference writer for the M20 map-candidate file (PixelAttachCand.h format).

Emits BOTH encodings from the same pair list so a production dumper can copy whichever is
convenient. This is also the exercise file for the -CF 2 plumbing test: for each listed
event it enumerates the cross product t3Row in [0,T3MAX) x plsRow in [0,PLSMAX), which is
a well-defined candidate set whose size the run summary can be checked against.

Usage: ta_makecands.py <out_prefix> <T3MAX> <PLSMAX> <run:lumi:evt> [...]
Writes <out_prefix>.txt and <out_prefix>.bin
"""
import struct, sys

out, t3max, plsmax = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
keys = [tuple(int(x) for x in k.split(':')) for k in sys.argv[4:]]

with open(out + '.txt', 'w') as ft, open(out + '.bin', 'wb') as fb:
    ft.write('# M20 map-candidate file (text encoding). "E run lumi event nPairs" then\n')
    ft.write('# one "<t3Row> <plsRow>" line per candidate pair.\n')
    fb.write(b'T3PAIRS1')
    for run, lumi, evt in keys:
        n = t3max * plsmax
        ft.write('E %d %d %d %d\n' % (run, lumi, evt, n))
        fb.write(struct.pack('<4i', run, lumi, evt, n))
        for t in range(t3max):
            for p in range(plsmax):
                ft.write('%d %d\n' % (t, p))
                fb.write(struct.pack('<2i', t, p))
print('wrote %s.txt and %s.bin : %d events x %d pairs' % (out, out, len(keys), t3max * plsmax))
