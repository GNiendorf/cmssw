#!/usr/bin/env python3
"""P2.6d: parse the standalone `-v 1` timing table.

The standalone prints one 'avg' row per run:

    avg   <Hits> <MD> <LS> <T3> <T5> <pLS> <T4> <pT5> <pT3> <TC> <Reset>   <Total>   <Short>+/- <sd>   <fullavg>   <target>[s=N]

'fullavg' is wall time of the whole event loop divided by the number of events, i.e. the number
lst_timing reports as the explicit rate. The per-stage columns are per-event means summed over
streams, so at -s > 1 they add up to more than fullavg; only the -s 1 columns are attributable.

usage: p26d_timing.py <label> <log> [<label> <log> ...]
       p26d_timing.py --table <label>=<log> ...
"""
import re
import sys

STAGES = ["Hits", "MD", "LS", "T3", "T5", "pLS", "T4", "pT5", "pT3", "TC", "Reset"]


def parse(path):
    """Return list of dicts, one per 'avg' row found in the log."""
    out = []
    for line in open(path, errors="replace"):
        if not line.strip().startswith("avg"):
            continue
        if "+/-" not in line:
            continue
        left, right = line.split("+/-", 1)
        lt = left.split()
        # lt = ['avg', 11 stages, total, short]
        if len(lt) < 14:
            continue
        vals = [float(x) for x in lt[1:14]]
        rt = right.split()
        sd = float(rt[0])
        fullavg = float(rt[1])
        m = re.search(r"\[s=(\d+)\]", right)
        streams = int(m.group(1)) if m else -1
        d = dict(zip(STAGES, vals[:11]))
        d["Total"] = vals[11]
        d["Short"] = vals[12]
        d["sd"] = sd
        d["fullavg"] = fullavg
        d["streams"] = streams
        out.append(d)
    return out


def main():
    args = sys.argv[1:]
    pairs = []
    for a in args:
        if "=" in a:
            lab, path = a.split("=", 1)
            pairs.append((lab, path))
    rows = []
    for lab, path in pairs:
        for d in parse(path):
            d["label"] = lab
            rows.append(d)
    if not rows:
        print("no avg rows parsed")
        return
    hdr = f"{'config':<22}{'s':>4}{'ms/evt':>10}{'evt/s':>10}   " + "".join(f"{s:>8}" for s in STAGES)
    print(hdr)
    print("-" * len(hdr))
    for d in rows:
        rate = 1000.0 / d["fullavg"] if d["fullavg"] > 0 else 0.0
        print(
            f"{d['label']:<22}{d['streams']:>4}{d['fullavg']:>10.2f}{rate:>10.2f}   "
            + "".join(f"{d[s]:>8.2f}" for s in STAGES)
        )


if __name__ == "__main__":
    main()
