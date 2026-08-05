"""CRITIC ITEM 2 -- DISCORDANT-PAIR (McNemar) STATISTICS.

The M19 floors are enforced to a precision far below the 300-event sample's own binomial
noise (eff sigma ~ 59 sims; we adjudicate on ~10). But the two runs share the SAME events and
the SAME sim tracks, so the correct statistic on a DELTA is not the binomial sigma of either
rate -- it is the discordant-pair count: how many individual sim tracks flipped found ->
lost (n_lost) and lost -> found (n_gain). Under H0 (the change is neutral for any given
track) the flips are Binomial(n_lost + n_gain, 1/2), so

    delta = n_gain - n_lost,   sigma_delta = sqrt(n_gain + n_lost),   z = delta / sigma.

That is the number the maintainer needs to read a "-10 sims" debit correctly.

Selections replicated from efficiency/src/performance.cc (pt_cut 0.9, eta_cut 4.5):
  eff_overall_incut : pt > 0.9, |vz| < 30, vxy < 2.5     -> denominator of the eff floor
  vxy / dxy bands   : |eta| < 4.5, pt > 0.9, |vz| < 30   (line 1209 block)

Usage: python3 fn_mcnemar.py <refTag> <newTag>
"""
import sys, os
import ROOT

P = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/final"
PTC, ETAC, VZT, VPT = 0.9, 4.5, 30.0, 2.5
BANDS = [(0, 1), (1, 5), (5, 10), (10, 30)]


def read(tag):
    fn = "%s/f_%s.root" % (P, tag)
    f = ROOT.TFile.Open(fn)
    t = f.Get("tree")
    rows = {}
    for i in range(t.GetEntries()):
        t.GetEntry(i)
        key0 = (int(t.run), int(t.lumi), int(t.evt))
        pt = list(t.sim_pt); eta = list(t.sim_eta)
        vx = list(t.sim_vx); vy = list(t.sim_vy); vz = list(t.sim_vz)
        dxy = list(t.sim_pca_dxy); tci = list(t.sim_tcIdx)
        for j in range(len(pt)):
            vxy = (vx[j] ** 2 + vy[j] ** 2) ** 0.5
            rows[(key0, j)] = (pt[j], eta[j], vxy, vz[j], dxy[j], 1 if tci[j] >= 0 else 0)
    f.Close()
    return rows


def mcnemar(A, B, sel, label):
    keys = set(A) & set(B)
    gain = loss = both = neither = 0
    for k in keys:
        a = A[k]; b = B[k]
        if not sel(a):
            continue
        if a[5] and b[5]: both += 1
        elif a[5] and not b[5]: loss += 1
        elif b[5] and not a[5]: gain += 1
        else: neither += 1
    den = both + loss + gain + neither
    disc = gain + loss
    delta = gain - loss
    sig = disc ** 0.5 if disc else 0.0
    z = delta / sig if sig else 0.0
    print("%-22s den=%6d  ref=%6d new=%6d   d=%+4d   n_gain=%3d n_loss=%3d  discordant=%3d"
          "  sigma=%5.2f  z=%+5.2f" %
          (label, den, both + loss, both + gain, delta, gain, loss, disc, sig, z))
    return delta, gain, loss


def main(ref, new):
    A = read(ref); B = read(new)
    print("DISCORDANT-PAIR (McNemar) ANALYSIS   ref=%s  new=%s" % (ref, new))
    print("H0: a change is neutral per track -> flips ~ Binom(n_disc, 1/2); sigma_delta = sqrt(n_disc)")
    print("-" * 128)
    mcnemar(A, B, lambda a: a[0] > PTC and abs(a[3]) < VZT and a[2] < VPT,
            "eff_overall_incut")
    base = lambda a: abs(a[1]) < ETAC and a[0] > PTC and abs(a[3]) < VZT
    for lo, hi in BANDS:
        mcnemar(A, B, lambda a, lo=lo, hi=hi: base(a) and lo <= a[2] < hi,
                "eff_vxy[%g,%g)" % (lo, hi))
    for lo, hi in BANDS:
        mcnemar(A, B, lambda a, lo=lo, hi=hi: base(a) and lo <= abs(a[4]) < hi,
                "eff_dxy[%g,%g)" % (lo, hi))
    print("-" * 128)
    print("Read: |z| < 1 means the sample cannot distinguish the change from neutral on that band.")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
