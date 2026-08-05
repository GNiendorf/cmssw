#!/usr/bin/env python3
"""M17 step 1 verification: the re-dumped chains + the RESIDENT gate must reproduce the
ctl_noatt hybrid funnel (in -> theta -> pixdrop) EXACTLY, per event and in total.

Reference = the per-event `chains=A -> theta=B -> pixdrop=C` lines of a hybrid log.
"""
import argparse
import os
import re
import sys

import numpy as np

D = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, D)
from m17_gatelib import CFG_F2, funnel, score_dump  # noqa: E402

LINE = re.compile(r"^evt (\d+) \(run (\d+) lumi (\d+) event (\d+)\): pixKept=\d+ chains=(\d+)"
                  r" -> theta=(\d+) -> pixdrop=(\d+) -> claim=(\d+)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", default=f"{D}/chains_m17_300evt.root")
    ap.add_argument("--model", default=f"{D}/chain3_mlp_m12.pt")
    ap.add_argument("--norm", default=f"{D}/chain3_norm_m12.json")
    ap.add_argument("--log", default=f"{D}/ab_ctl_noatt.log")
    ap.add_argument("--rt5", type=int, default=1, help="1 = -RT5 1 (partOfPT5 drop OFF)")
    ap.add_argument("--rt3", type=int, default=0, help="0 = -RT3 0 (partOfPT3 drop ON)")
    a = ap.parse_args()

    ref = []
    for ln in open(a.log):
        mm = LINE.match(ln)
        if mm:
            ref.append(tuple(int(x) for x in mm.groups()[3:]))  # evtno, chains, theta, pixdrop, claim
    print(f"reference: {len(ref)} events from {os.path.basename(a.log)}")

    m, meta = score_dump(a.model, a.norm, a.dump)
    f = funnel(m, meta, CFG_F2, drop_pt5=(a.rt5 == 0), drop_pt3=(a.rt3 == 0))
    evt = meta["evt"]
    order = []
    seen = {}
    for e in evt:
        if e not in seen:
            seen[e] = len(order)
            order.append(e)

    bad = 0
    tot = np.zeros(3, dtype=np.int64)
    for i, (evtno, nch, nth, npd, _ncl) in enumerate(ref):
        sel = evt == order[i]
        got = (int(sel.sum()), int(f["theta"][sel].sum()), int(f["pixdrop"][sel].sum()))
        tot += got
        if got != (nch, nth, npd):
            bad += 1
            if bad <= 10:
                print(f"  MISMATCH evt {i} (event {evtno}): got {got} want {(nch, nth, npd)}")
    print(f"TOTALS replica in={tot[0]} theta={tot[1]} pixdrop={tot[2]}")
    rt = np.sum([r[1:4] for r in ref], axis=0)
    print(f"TOTALS hybrid  in={rt[0]} theta={rt[1]} pixdrop={rt[2]}")
    print(f"per-event mismatches: {bad}/{len(ref)}")
    print("FUNNEL REPLICA EXACT" if bad == 0 and tuple(tot) == tuple(rt) else "FUNNEL REPLICA FAILED")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
