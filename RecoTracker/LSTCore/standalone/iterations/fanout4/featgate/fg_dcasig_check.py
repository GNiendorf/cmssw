#!/usr/bin/env python3
"""Direct test of the (c) significance proxy, independent of any AUC.

The design claim is that dcaSig = dcaXY / sqrt(cf_fullFitChi2PerHit + SIGMA0^2) FLATTENS
the |eta| dependence of the true-prompt DCA distribution -- i.e. that the chain's own
fit-residual RMS is the right per-chain scale for the transition-region degradation.
Prints the per-|eta|-band p50/p90 of both forms over TRUE PROMPT chains, plus the
barrel-MD fraction, over the frozen test-60 rows.
"""

import argparse
import os

import numpy as np

import train_chain3_fg as T

BANDS = [(0.0, 0.8), (0.8, 1.1), (1.1, 1.4), (1.4, 1.6), (1.6, 2.0), (2.0, 3.0)]
SIGMA0 = T.SIGMA0_CM


def main():
    d = os.path.dirname(os.path.abspath(__file__))
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--input", default=f"{d}/chains_fg_300evt.root")
    args = p.parse_args()

    meta, X, names = T.load_dump(args.input, {"maxBridgeChi2"})
    chi = X[:, names.index("cf_fullFitChi2PerHit")].astype(np.float64)   # raw cm^2
    dca = meta["dcaXY"].astype(np.float64)
    sig = dca / np.sqrt(chi + SIGMA0 * SIGMA0)
    tp = (meta["label"] == 1) & (meta["simVxy"] < 1.0)
    ae = np.abs(meta["chainEta"])

    print(f"TRUE-PROMPT chains, {args.input} (all events); SIGMA0 = {SIGMA0} cm\n")
    print(f"  {'|eta| band':>12} {'n':>8} {'bFrac p50':>10} "
          f"{'dcaXY p50':>10} {'dcaXY p90':>10} {'dcaSig p50':>11} {'dcaSig p90':>11}")
    ref = None
    rows = []
    for lo, hi in BANDS:
        m = tp & (ae >= lo) & (ae < hi)
        if m.sum() < 50:
            continue
        r = (f"{lo:.1f}-{hi:.1f}", int(m.sum()), np.median(meta["barrelMDFrac"][m]),
             np.median(dca[m]), np.quantile(dca[m], 0.9),
             np.median(sig[m]), np.quantile(sig[m], 0.9))
        rows.append(r)
        print(f"  {r[0]:>12} {r[1]:>8d} {r[2]:>10.3f} {r[3]:>10.4f} {r[4]:>10.4f} "
              f"{r[5]:>11.3f} {r[6]:>11.3f}")
        if lo == 0.0:
            ref = r
    print("\n  spread across bands (max/min of the band medians):")
    print(f"    dcaXY  p50: {max(r[3] for r in rows) / min(r[3] for r in rows):.2f}x")
    print(f"    dcaSig p50: {max(r[5] for r in rows) / min(r[5] for r in rows):.2f}x")
    print(f"    (pure-barrel reference band {ref[0]}: dcaXY {ref[3]:.4f} cm, dcaSig {ref[5]:.2f})")


if __name__ == "__main__":
    main()
