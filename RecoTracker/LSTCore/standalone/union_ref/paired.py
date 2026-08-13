import sys, uproot, numpy as np
from math import erfc, sqrt
# Paired McNemar on jet-core sims, SEALED HOLDOUT half (input rows 500-999).
# Pairing is by (event row, sim index): the writer emits in stream-completion order, so we
# key on the ntuple's own row ordering and require identical denominators, which is checked.
def load(p, lo, hi):
    t = uproot.open(p)["tree"]
    a = t.arrays(["sim_pt","sim_eta","sim_dR_jet","sim_tcIdx"], entry_start=lo, entry_stop=hi, library="np")
    return a
def core_mask(a, i, dRmax):
    dr = np.asarray(a["sim_dR_jet"][i]); pt = np.asarray(a["sim_pt"][i]); eta = np.abs(np.asarray(a["sim_eta"][i]))
    return (dr >= 0) & (dr < dRmax) & (pt > 1.0) & (eta < 2.4)
def matched(a, i):
    return np.array([len(x) > 0 for x in a["sim_tcIdx"][i]], bool)
A = load(sys.argv[1], 500, 1000); B = load(sys.argv[2], 500, 1000)
for label, dRmax in (("core dR<.05", .05), ("dR<.02", .02), ("dR<.005", .005)):
    n01 = n10 = na = nb = nd = 0
    for i in range(len(A["sim_pt"])):
        m = core_mask(A, i, dRmax)
        if not np.array_equal(m, core_mask(B, i, dRmax)):
            nd += 1; continue
        ma = matched(A, i)[m]; mb = matched(B, i)[m]
        na += ma.sum(); nb += mb.sum()
        n01 += int((~ma & mb).sum()); n10 += int((ma & ~mb).sum())
    tot = sum(core_mask(A, i, dRmax).sum() for i in range(len(A["sim_pt"])))
    d = (nb - na) / tot
    z = abs(n01 - n10) / sqrt(n01 + n10) if (n01 + n10) else 0.0
    p = erfc(z / sqrt(2))
    print("%-12s  A %.4f  B %.4f  delta %+.4f   gained %d / lost %d   z %.1f   p %.2e%s"
          % (label, na/tot, nb/tot, d, n01, n10, z, p, "  [%d skipped evt]" % nd if nd else ""))
