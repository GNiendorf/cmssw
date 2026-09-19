#!/usr/bin/env python3
"""Rung 4 exploration: DNN working-point scans (track level), flag options, r-z and dBeta ratios of true pairs.
usage: explore1.py <t4.npz> <t5.npz>   READ-ONLY (stdout)"""
import sys

import numpy as np

import rules
from scan45 import Tracks, load, BINS


def main():
    d4, d5 = load(sys.argv[1]), load(sys.argv[2])
    T = Tracks(d4, d5)
    r4, r5 = d4["rec"], d5["rec"]
    p4 = rules.t4_parts(r4); p5 = rules.t5_parts(r5)
    cur5 = rules.allpass(p5)
    nt4 = ~T.true4; nt5 = d5["tk"] < 0
    base4 = rules.allpass(p4)
    hdr = "| rule | " + " | ".join(b for b, _, _ in BINS) + " | not-true records passing (x current) |\n|---|" + "---|" * (len(BINS) + 1)

    print("## T4 DNN working point, flag OFF at the T4 builder, everything else current; T5 current")
    print(hdr)
    noflag = dict(p4); noflag["flag"] = np.ones(len(r4), bool)
    for sd, sf in ((1, 1), (0.5, 1), (0.2, 1), (0.1, 1), (0.05, 1), (0.02, 1), (0.01, 1), (0, 1), (1, 0), (0.1, 0.5), (0.1, 0.2), (0.05, 0.1), (0.02, 0.1), (0.02, 0.05), (0.01, 0.02), (0, 0.1), (0, 0.02), (0, 0)):
        q = dict(noflag)
        wf = 1 - (1 - r4["wpFake"]) * sf
        q["dnn"] = (r4["scores"][:, 2] > sd * r4["wpDisp"]) & (r4["scores"][:, 0] < wf)
        ps = rules.allpass(q)
        print(T.row("disp WP x %g, fake WP -> 1-(1-wp) x %g" % (sd, sf), ps, cur5)[:-1] + " %.2f |" % ((ps & nt4).sum() / max((base4 & nt4).sum(), 1)))

    print("\n## T4 DNN: alternative forms (flag OFF)")
    print(hdr)
    s = r4["scores"]
    for lab, m in (("fake < wpFake only", s[:, 0] < r4["wpFake"]), ("fake < 0.9", s[:, 0] < 0.9), ("fake < 0.95", s[:, 0] < 0.95), ("fake < 0.98", s[:, 0] < 0.98), ("fake < 0.99", s[:, 0] < 0.99),
                   ("fake < 0.995", s[:, 0] < 0.995), ("fake < 0.999", s[:, 0] < 0.999), ("bypass", np.ones(len(r4), bool))):
        q = dict(noflag); q["dnn"] = m
        ps = rules.allpass(q)
        print(T.row(lab, ps, cur5)[:-1] + " %.2f |" % ((ps & nt4).sum() / max((base4 & nt4).sum(), 1)))

    print("\n## the direction flag at the T4 builder (DNN current / DNN bypassed)")
    print(hdr)
    for dn in ("current", "bypass"):
        for fl in ("either", "both", "off"):
            q = rules.t4_parts(r4, flag=fl)
            if dn == "bypass":
                q["dnn"] = np.ones(len(r4), bool)
            ps = rules.allpass(q)
            print(T.row("DNN %s, flag %s" % (dn, fl), ps, cur5)[:-1] + " %.2f |" % ((ps & nt4).sum() / max((base4 & nt4).sum(), 1)))
    f0 = (r4["t3Flags"][:, 0] & 2) != 0; f1 = (r4["t3Flags"][:, 1] & 2) != 0
    ok4 = np.all(d4["lp"] >= 0.8, axis=1)
    tm = T.true4 & ok4
    print("\nflag content of T4 pair records: true (criterion): none %.3f one %.3f both %.3f | not true: none %.3f one %.3f both %.3f"
          % ((~f0 & ~f1)[tm].mean(), (f0 ^ f1)[tm].mean(), (f0 & f1)[tm].mean(), (~f0 & ~f1)[nt4].mean(), (f0 ^ f1)[nt4].mean(), (f0 & f1)[nt4].mean()))
    rest = p4["charge"] & p4["dbeta"] & p4["rz"]
    print("among records passing charge+dBeta+r-z: true none/one/both %d / %d / %d ; not true %d / %d / %d"
          % ((tm & rest & ~f0 & ~f1).sum(), (tm & rest & (f0 ^ f1)).sum(), (tm & rest & f0 & f1).sum(), (nt4 & rest & ~f0 & ~f1).sum(), (nt4 & rest & (f0 ^ f1)).sum(), (nt4 & rest & f0 & f1).sum()))
    rest = rest & p4["dnn"]
    print("among records passing charge+dBeta+r-z+DNN: true none/one/both %d / %d / %d ; not true %d / %d / %d"
          % ((tm & rest & ~f0 & ~f1).sum(), (tm & rest & (f0 ^ f1)).sum(), (tm & rest & f0 & f1).sum(), (nt4 & rest & ~f0 & ~f1).sum(), (nt4 & rest & (f0 ^ f1)).sum(), (nt4 & rest & f0 & f1).sum()))

    print("\n## T4 r-z: chi2 / cut of TRUE pairs (criterion denominator), quantiles 50 / 90 / 99 / 99.5 / max, and share on the linear fallback")
    cut = rules.t4_rz_cut(r4)
    vx = np.where(T.true4, T.vxy[np.maximum(d4["tk"], 0)], -1)
    for b, lo, hi in BINS:
        m = tm & (vx >= lo) & (vx < hi)
        x = r4["rzChi2"][m] / cut[m]
        x = np.where(np.isfinite(x), x, 1e9)
        print("| %s | %d | %s | linear %.4f | refused %.4f |" % (b, m.sum(), " / ".join("%.2f" % v for v in np.quantile(x, [0.5, 0.9, 0.99, 0.995, 1.0])), r4["rzLinear"][m].mean(), (x >= 1).mean()))
    print("\nper position-eta bin of MD1 (true pairs, vxy 2.5-52.4): bin, cut, n, refused, q99.5 of chi2")
    e = rules.eta1(r4)
    m0 = tm & (vx >= 2.5) & (vx < 52.4) & (r4["rzLinear"] == 0)
    for b in range(25):
        m = m0 & (np.minimum((e / 0.1).astype(int), 24) == b)
        if m.sum() < 30:
            continue
        print("| %d | %.1f | %d | %.4f | %.1f | nottrue pass %.3f |" % (b, rules.T4_RZ_ETA[b], m.sum(), (r4["rzChi2"][m] >= rules.T4_RZ_ETA[b]).mean(), np.quantile(r4["rzChi2"][m], 0.995),
              (r4["rzChi2"][nt4 & (np.minimum((e / 0.1).astype(int), 24) == b)] < rules.T4_RZ_ETA[b]).mean()))

    print("\n## T4 dBeta: dBeta^2 / cut^2 of TRUE pairs, quantiles 50 / 90 / 99 / 99.5, refused; by |d0| of the inner circle")
    d0 = np.abs(np.hypot(r4["innerCenter"][:, 0], r4["innerCenter"][:, 1]) - r4["radii"][:, 0])
    ratio = r4["dBeta"] ** 2 / np.maximum(r4["dBetaCut2"], 1e-30)
    ratio = np.where(np.isfinite(ratio), ratio, 1e9)
    for lo, hi in ((0, 0.5), (0.5, 2), (2, 4), (4, 8), (8, 16), (16, 60)):
        m = tm & (d0 >= lo) & (d0 < hi)
        if m.sum() < 20:
            continue
        print("| d0 %g-%g | %d | %s | refused %.4f | DNN refuses %.4f | r-z refuses %.4f | flag %.4f |" % (lo, hi, m.sum(), " / ".join("%.2f" % v for v in np.quantile(ratio[m], [0.5, 0.9, 0.99, 0.995])),
              (~p4["dbeta"])[m].mean(), (~p4["dnn"])[m].mean(), (~p4["rz"])[m].mean(), (~p4["flag"])[m].mean()))


if __name__ == "__main__":
    main()
