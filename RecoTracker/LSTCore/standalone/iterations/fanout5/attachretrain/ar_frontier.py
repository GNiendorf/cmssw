#!/usr/bin/env python3
"""ar_frontier.py <tag> [<tag> ...] -- compare the deployed BEST-PER-TARGET frontiers.

The [ATTACHCM] BEST-PER-TARGET block is threshold-INDEPENDENT (it is accumulated from
every scored pair's best-per-target logit, before any -a cut), so ONE run per head gives
that head's whole precision-vs-yield curve. Two heads are compared by reading the curves
at matched YIELD -- the numeric -a value is meaningless across heads because each head
has its own logit scale.

Prints, for a grid of yields (attachable targets per event), the precision each head
reaches there. A head that is strictly above another at every yield is strictly better.
"""
import os
import re
import sys

P = os.path.dirname(os.path.abspath(__file__))


def curve(tag):
    """[(cut, n, same, bad, prec, per_evt)] ascending in cut."""
    out = []
    with open(f"{P}/ar_{tag}.log", errors="ignore") as fh:
        for ln in fh:
            m = re.match(r"\[ATTACHCM\]   cut\s+([-+][\d.]+) : n=(\d+)\s+same=(\d+)\s+"
                         r"bad=(\d+)\s+prec=([\d.]+) \(([\d.]+)/evt\)", ln)
            if m:
                out.append((float(m.group(1)), int(m.group(2)), int(m.group(3)),
                            int(m.group(4)), float(m.group(5)), float(m.group(6))))
    return out


def at_yield(c, y):
    """Interpolate the curve to a target yield (per evt). Curve is DECREASING in yield
    as the cut rises, so walk from the loose end."""
    best = None
    for cut, n, same, bad, prec, pe in c:
        if pe >= y:
            best = (cut, prec, pe, same, bad)
        else:
            break
    return best


tags = sys.argv[1:]
curves = {t: curve(t) for t in tags}
tags = [t for t in tags if curves[t]]
if not tags:
    print("no [ATTACHCM] frontier found -- was AR_CM=1 set?")
    sys.exit(1)

print("### Deployed precision at MATCHED YIELD (best-per-target frontier)\n")
print("Yield = attachable targets/evt at that head's cut. The -a numbers are NOT")
print("comparable across heads; the yield is.\n")
grid = [900, 800, 700, 650, 600, 550, 500, 485, 450, 400, 300, 200, 150]
cols = " | ".join(f"{t} (cut / prec)" for t in tags)
print(f"| yield/evt | {cols} |")
print("|" + "---|" * (len(tags) + 1))
for y in grid:
    cells = []
    for t in tags:
        r = at_yield(curves[t], y)
        cells.append("-" if r is None else f"{r[0]:+.1f} / {r[1]:.4f}")
    print(f"| {y} | " + " | ".join(cells) + " |")

print("\n### Full curves\n")
for t in tags:
    print(f"**{t}**: cut range {curves[t][0][0]:+.1f}..{curves[t][-1][0]:+.1f}, "
          f"peak precision {max(c[4] for c in curves[t]):.4f}")
