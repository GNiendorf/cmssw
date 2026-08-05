#!/usr/bin/env python3
"""okretrain step 2: tag every dumped chain as a K9 CLAIM-STAGE SURVIVOR.

The -B order key ranks only the chains that reach K9's greedy claim, i.e. the ones
that pass (a) the -G 6 three-class gate kill, (b) the K9 per-length threshold /
-U exempt threshold on chains.score, (c) the pixel-consumed drop (-RT5 1 -RT3 0).
This script replays that funnel offline with m17_gatelib (the verified replica) and
writes an npz with the per-row masks, aligned to the dump's row order.

  python3 ok_gate.py --dump chains_m18.root --out ok_surv_m18.npz [--log ab_base.log]
"""
import argparse
import os
import sys

import numpy as np

D = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, D)
from m17_gatelib import CFG_F2, funnel, score_dump  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", default=f"{D}/chains_m18.root")
    ap.add_argument("--model", default=f"{D}/chain3_mlp_m12.pt")
    ap.add_argument("--norm", default=f"{D}/chain3_norm_m12.json")
    ap.add_argument("--out", default=f"{D}/ok_surv_m18.npz")
    ap.add_argument("--log", default="", help="hybrid log to cross-check the funnel totals")
    ap.add_argument("--rt5", type=int, default=1, help="1 = -RT5 1 (partOfPT5 drop OFF)")
    ap.add_argument("--rt3", type=int, default=0, help="0 = -RT3 0 (partOfPT3 drop ON)")
    a = ap.parse_args()

    m, meta = score_dump(a.model, a.norm, a.dump)
    f = funnel(m, meta, CFG_F2, drop_pt5=(a.rt5 == 0), drop_pt3=(a.rt3 == 0))
    n = len(meta["nLayers"])
    nev = len(np.unique(meta["evt"]))
    surv = f["pixdrop"]
    print(f"rows={n} events={nev}")
    print(f"  gate killed   = {int(f['killed'].sum()):8d} ({f['killed'].mean():.4f})")
    print(f"  exempt        = {int(f['exempt'].sum()):8d} ({f['exempt'].mean():.4f})")
    print(f"  theta pass    = {int(f['theta'].sum()):8d} ({f['theta'].mean():.4f})  "
          f"{f['theta'].sum() / nev:.1f}/evt")
    print(f"  SURVIVOR      = {int(surv.sum()):8d} ({surv.mean():.4f})  {surv.sum() / nev:.1f}/evt")
    lab = meta["label"] == 1
    print(f"  label=1 all   = {lab.mean():.4f} | among survivors = {lab[surv].mean():.4f}")
    for nl in (4, 5, 6, 7):
        s = surv & (meta["nLayers"] == nl)
        if s.sum():
            print(f"    nLayers={nl}: surv={int(s.sum()):8d} trueFrac={lab[s].mean():.4f}")

    if a.log:
        import re
        LINE = re.compile(r"chains=(\d+) -> theta=(\d+) -> pixdrop=(\d+) -> claim=(\d+)")
        ref = [tuple(int(x) for x in mm.groups()) for mm in
               (LINE.search(ln) for ln in open(a.log)) if mm]
        if ref:
            rt = np.sum(ref, axis=0)
            print(f"  hybrid log totals: in={rt[0]} theta={rt[1]} pixdrop={rt[2]} claim={rt[3]}")
            print(f"  replica    totals: in={n} theta={int(f['theta'].sum())} "
                  f"pixdrop={int(surv.sum())}")
            ok = (n == rt[0] and int(f["theta"].sum()) == rt[1] and int(surv.sum()) == rt[2])
            print("  FUNNEL REPLICA EXACT" if ok else "  FUNNEL REPLICA MISMATCH")

    np.savez_compressed(a.out, surv=surv, theta=f["theta"], killed=f["killed"],
                        exempt=f["exempt"], evt=meta["evt"], mX=m["mX"], mP=m["mP"], mD=m["mD"])
    print(f"wrote {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
