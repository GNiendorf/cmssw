#!/usr/bin/env python3
"""S3: write the refitted ATTACH-logit bars into a deploy tree's interface/ChainConfig.h.

Only the numeric initialiser of the eight named fields is rewritten (the same surgical form as
nnloop_ref/s2_work/setbars.py); every other field, every cell and every comment is untouched.
The eight are exactly the bars expressed on the attach head's logit scale, i.e. the constants the
shipped header's own banner calls "NOT independent of this file":

    attachTheta attachThetaT attachThetaE   stage-A delivery, |seed eta| bands
    attachThetaT3                           stage-B delivery (and the T3-side -RPS bar)
    rpsThetaChain                           chain-side -RPS retirement
    xcTheta xcThetaT xcThetaE               -XC bare-chain crossclean, |seed eta| bands

usage: setbars_s3.py <ChainConfig.h> <bars.json>
       setbars_s3.py <ChainConfig.h> --restore
"""
import json
import re
import sys

FIELDS = ["attachTheta", "attachThetaT", "attachThetaE", "attachThetaT3",
          "rpsThetaChain", "xcTheta", "xcThetaT", "xcThetaE"]
SHIPPED = {"attachTheta": 7.3, "attachThetaT": 7.0, "attachThetaE": 6.4,
           "attachThetaT3": 6.450, "rpsThetaChain": 6.084,
           "xcTheta": 4.5, "xcThetaT": 4.0, "xcThetaE": 4.2}


def setf(txt, name, val):
    pat = re.compile(r"(float\s+%s\s*=\s*)(-?[0-9.eE+]+f?)(;[ ]*)(//.*)?$"
                     % re.escape(name), re.M)
    m = pat.search(txt)
    assert m, "field %s not found" % name
    lit = "%.7gf" % val
    if "." not in lit and "e" not in lit:
        lit = lit[:-1] + ".f"
    old_col = m.start(4) - m.start(0) if m.group(4) else None
    pad = m.group(3)
    if old_col is not None:
        want = old_col - (len(m.group(1)) + len(lit) + 1)
        pad = ";" + " " * max(want, 1)
    rep = m.group(1) + lit + pad + (m.group(4) or "")
    return txt[:m.start(0)] + rep + txt[m.end(0):], m.group(2), lit


def main():
    path = sys.argv[1]
    if sys.argv[2] == "--restore":
        vals = SHIPPED
    else:
        rep = json.load(open(sys.argv[2]))
        vals = {k: v["new"] for k, v in rep["bars"].items()}
    txt = open(path).read()
    for f in FIELDS:
        txt, old, new = setf(txt, f, float(vals[f]))
        print("  %-14s %12s -> %s" % (f, old, new))
    open(path, "w").write(txt)
    print("wrote %s" % path)


if __name__ == "__main__":
    main()
