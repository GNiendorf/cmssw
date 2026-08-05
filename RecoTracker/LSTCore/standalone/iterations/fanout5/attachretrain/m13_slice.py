#!/usr/bin/env python3
"""M13 recon: decompose the w7 aggregate fake rate into chain-slice and pixel-slice.

READ-ONLY. Reads ab_m12_w7.root (impersonated LST ntuple) and base300_identity.root
(identity baseline over the same 300 events). Reproduces the harness FR definition
exactly (TC_fr: denom = every TC with pt > 0.9, numer = tc_isFake > 0; the eta-hist
sums used by compare_ab.py include under/overflow, so no eta cut applies).
"""
import numpy as np
import uproot

PROTO = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype/"
PTCUT = 0.9

COLS = ["tc_pt", "tc_eta", "tc_type", "tc_isFake", "tc_isDuplicate", "tc_nhitOT", "tc_isChain"]


def load(path, chain_branch=True):
    t = uproot.open(path + ":tree")
    cols = list(COLS) if chain_branch else [c for c in COLS if c != "tc_isChain"]
    d = t.arrays(cols, library="np")
    out = {k: np.concatenate([np.asarray(v) for v in d[k]]) for k in cols}
    if not chain_branch:
        out["tc_isChain"] = np.zeros(len(out["tc_pt"]), dtype=np.int32)
    return out


def rate(num, den):
    return float(num) / den if den else float("nan")


def report(tag, d):
    m = d["tc_pt"] > PTCUT
    fk = d["tc_isFake"] > 0
    ch = d["tc_isChain"] > 0
    print("\n===== %s =====" % tag)
    print("TCs total %d  in-cut(pt>0.9) %d  fakes-in-cut %d  FR %.4f"
          % (len(m), m.sum(), (m & fk).sum(), rate((m & fk).sum(), m.sum())))
    D = m.sum()
    for nm, sl in [("chain-slice", m & ch), ("pixel-slice", m & ~ch)]:
        n = sl.sum()
        nf = (sl & fk).sum()
        print("  %-12s n=%7d (%.4f of denom)  fakes=%6d  slice-FR=%.4f  "
              "contribution to aggregate = %.4f (%.1f%% of all fakes)"
              % (nm, n, n / D, nf, rate(nf, n), rate(nf, D), 100.0 * nf / (m & fk).sum()))
    # by tc_type
    print("  --- by tc_type (in-cut) ---")
    for ty in sorted(set(d["tc_type"].tolist())):
        sl = m & (d["tc_type"] == ty)
        nf = (sl & fk).sum()
        nch = (sl & ch).sum()
        print("    type %2d: n=%7d (chain %6d) fakes=%6d FR=%.4f contrib=%.4f"
              % (ty, sl.sum(), nch, nf, rate(nf, sl.sum()), rate(nf, D)))
    # eta regions
    print("  --- eta regions (in-cut): FR of chain-slice / pixel-slice / all ---")
    ae = np.abs(d["tc_eta"])
    for nm, lo, hi in [("barrel  |eta|<1.1", 0.0, 1.1), ("transition 1.1-1.7", 1.1, 1.7),
                       ("endcap  >1.7", 1.7, 99.0)]:
        r = m & (ae >= lo) & (ae < hi)
        rc, rp = r & ch, r & ~ch
        print("    %-19s all n=%7d FR=%.4f | chain n=%6d FR=%.4f frac_of_reg=%.3f "
              "| pixel n=%7d FR=%.4f | chain contrib to region FR = %.4f"
              % (nm, r.sum(), rate((r & fk).sum(), r.sum()), rc.sum(),
                 rate((rc & fk).sum(), rc.sum()), rate(rc.sum(), r.sum()),
                 rp.sum(), rate((rp & fk).sum(), rp.sum()),
                 rate((rc & fk).sum(), r.sum())))
    return d


w7 = load(PROTO + "ab_m12_w7.root", True)
base = load(PROTO + "base300_identity.root", False)
report("w7 (ab_m12_w7.root)", w7)
report("BASELINE (base300_identity.root)", base)

# ---- counterfactual arithmetic: what does the pixel slice pin the aggregate to? ----
mW = w7["tc_pt"] > PTCUT
fW = w7["tc_isFake"] > 0
cW = w7["tc_isChain"] > 0
mB = base["tc_pt"] > PTCUT
fB = base["tc_isFake"] > 0

Dw, Nw = mW.sum(), (mW & fW).sum()
Dp, Np = (mW & ~cW).sum(), (mW & ~cW & fW).sum()
Dc, Nc = (mW & cW).sum(), (mW & cW & fW).sum()
Db, Nb = mB.sum(), (mB & fB).sum()

print("\n===== AGGREGATE DECOMPOSITION =====")
print("baseline FR                       = %.4f  (%d/%d)" % (rate(Nb, Db), Nb, Db))
print("w7       FR                       = %.4f  (%d/%d)" % (rate(Nw, Dw), Nw, Dw))
print("  pixel slice FR (own)            = %.4f  (%d/%d)" % (rate(Np, Dp), Np, Dp))
print("  chain slice FR (own)            = %.4f  (%d/%d)" % (rate(Nc, Dc), Nc, Dc))
print("  pixel contribution to aggregate = %.4f" % rate(Np, Dw))
print("  chain contribution to aggregate = %.4f" % rate(Nc, Dw))
print("\ncounterfactuals:")
print("  chain slice PERFECT (0 chain fakes)  -> aggregate FR = %.4f" % rate(Np, Dw))
print("  chain slice at baseline FR %.4f      -> aggregate FR = %.4f"
      % (rate(Nb, Db), rate(Np + rate(Nb, Db) * Dc, Dw)))
print("  chain slice at pixel-slice FR %.4f   -> aggregate FR = %.4f"
      % (rate(Np, Dp), rate(Np + rate(Np, Dp) * Dc, Dw)))
print("  chain fakes needed to hit baseline %.4f aggregate: %.0f (have %d, must remove %.0f = %.1f%%)"
      % (rate(Nb, Db), rate(Nb, Db) * Dw - Np, Nc, Nc - (rate(Nb, Db) * Dw - Np),
         100.0 * (Nc - (rate(Nb, Db) * Dw - Np)) / Nc))

# pixel-slice comparison vs baseline pixel TCs (baseline is all-pixel + its own OT objs)
print("\n--- pixel-slice composition vs baseline, by tc_type (in-cut) ---")
tys = sorted(set(w7["tc_type"].tolist()) | set(base["tc_type"].tolist()))
print("  %-6s %10s %10s %10s %10s" % ("type", "w7_n", "w7_fake", "base_n", "base_fake"))
for ty in tys:
    sw = mW & ~cW & (w7["tc_type"] == ty)
    sb = mB & (base["tc_type"] == ty)
    print("  %-6d %10d %10d %10d %10d" % (ty, sw.sum(), (sw & fW).sum(), sb.sum(), (sb & fB).sum()))
