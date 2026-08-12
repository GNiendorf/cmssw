#!/usr/bin/env python3
"""S2: write refitted bar VALUES into a deploy tree's interface/ChainConfig.h.

Only the numeric initialiser of named fields is rewritten; the struct, the cells, the
comments and every other field are untouched.  m3Theta4D2 / m3Theta5 / m3Theta6 / m3ThetaD /
zdRI / zdR / zdR5 / zdR6 / dcaSplit / dcaSplit2 / t4FarMaxResid are NOT in the list and are
therefore left exactly as shipped -- the far cell stays a free pass and the OR-rescue
structure is unchanged.

Usage: setbars.py <ChainConfig.h> <barfit.json> <variant>
       setbars.py <ChainConfig.h> --restore     (put the shipped values back)
"""
import json
import re
import sys

FIELDS = ["m3Theta4", "zdM4", "m3Theta4D", "zdM4D", "m3ThetaRI",
          "m3ThetaRB", "m3ThetaRT", "m3ThetaR", "c25Theta", "c25ThetaD"]
SHIPPED = {"m3Theta4": 2.0, "zdM4": -0.5, "m3Theta4D": -2.5, "zdM4D": 1.2,
           "m3ThetaRI": -0.5, "m3ThetaRB": -1.2, "m3ThetaRT": -1.2, "m3ThetaR": -1.8,
           "c25Theta": 2.0, "c25ThetaD": -1.5}


def setf(txt, name, val):
    """Rewrite one field's literal, KEEPING the trailing comment in its original column.

    The comments in ChainConfig.h are hand-aligned and they document what each bar cuts on;
    letting a longer literal shove them right makes the delivered diff much harder to review.
    """
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
        vals = rep["chainconfig"][sys.argv[3]]
    txt = open(path).read()
    for f in FIELDS:
        txt, old, new = setf(txt, f, float(vals[f]))
        print("  %-12s %12s -> %s" % (f, old, new))
    open(path, "w").write(txt)
    print("wrote %s" % path)


if __name__ == "__main__":
    main()
