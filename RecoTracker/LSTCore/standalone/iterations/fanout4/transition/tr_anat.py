#!/usr/bin/env python3
"""tr_anat.py -- transition-band (|eta| 1.1-1.7) fake anatomy for the chain prototype.

Reads an ab_*.root produced by the fanout4/transition binary (which carries the extra
tc_dbg* diagnostic branches) and slices the chain-delivered TCs by
  -G 6 branch x nLayers x layer composition (barrel/endcap MD mix) x PS fraction
separately in barrel / transition / endcap, reporting fake rate and population share.

Usage: tr_anat.py <ab_root> [more_ab_roots...]
"""
import sys
import numpy as np
import uproot

BR = {-1: "carried", 0: "T4-IP", 1: "T4-exempt", 2: "5+IP", 3: "5+exempt"}
BANDS = [("barrel", 0.0, 1.1), ("transition", 1.1, 1.7), ("endcap", 1.7, 4.5)]


def load(path):
    t = uproot.open(path)["tree"]
    a = t.arrays(["tc_pt", "tc_eta", "tc_type", "tc_isFake", "tc_isDuplicate", "tc_isChain",
                  "tc_dbgBr", "tc_dbgNL", "tc_dbgNMD", "tc_dbgNB", "tc_dbgNPS", "tc_dbgNN",
                  "tc_dbgInLay", "tc_dbgMP", "tc_dbgMD", "tc_dbgDca"], library="np")
    d = {}
    for k, v in a.items():
        d[k] = np.concatenate([np.asarray(x) for x in v])
    return d


def band_mask(d, lo, hi):
    ae = np.abs(d["tc_eta"])
    return (ae >= lo) & (ae < hi) & (d["tc_pt"] > 0.9)


def main():
    for path in sys.argv[1:]:
        d = load(path)
        print("=" * 100)
        print("FILE %s   nTC=%d" % (path, len(d["tc_pt"])))
        pt9 = d["tc_pt"] > 0.9
        fk = d["tc_isFake"].astype(bool)
        ch = d["tc_isChain"] > 0
        print("  global fake(pt>0.9) = %.4f   (chain %.4f / carried %.4f)"
              % (fk[pt9].mean(), fk[pt9 & ch].mean(), fk[pt9 & ~ch].mean()))
        for name, lo, hi in BANDS:
            m = band_mask(d, lo, hi)
            print("  %-11s fake=%.4f  n=%7d | chain n=%7d fake=%.4f contrib=%+.4f"
                  " | carried n=%7d fake=%.4f contrib=%+.4f"
                  % (name, fk[m].mean(), m.sum(), (m & ch).sum(), fk[m & ch].mean(),
                     fk[m & ch].sum() / max(1, m.sum()), (m & ~ch).sum(), fk[m & ~ch].mean(),
                     fk[m & ~ch].sum() / max(1, m.sum())))

        # ---- branch x nLayers matrix, chain TCs only, per band ------------------------
        for name, lo, hi in BANDS:
            m = band_mask(d, lo, hi) & ch
            if m.sum() == 0:
                continue
            print("\n  --- %s: chain TCs by (branch, nLayers) ---   n=%d  fake=%.4f  fakes=%d"
                  % (name, m.sum(), fk[m].mean(), fk[m].sum()))
            tot_fake = fk[m].sum()
            rows = []
            for b in sorted(set(d["tc_dbgBr"][m].tolist())):
                for nl in sorted(set(d["tc_dbgNL"][m].tolist())):
                    s = m & (d["tc_dbgBr"] == b) & (d["tc_dbgNL"] == nl)
                    if s.sum() < 50:
                        continue
                    rows.append((fk[s].sum(), BR.get(int(b), str(b)), int(nl), int(s.sum()),
                                 fk[s].mean(), fk[s].sum() / max(1, tot_fake)))
            rows.sort(reverse=True)
            print("      %-10s %3s %8s %8s %8s %8s" % ("branch", "nL", "n", "share", "fake", "fakeshr"))
            for nf, b, nl, n, fr, shr in rows:
                print("      %-10s %3d %8d %7.1f%% %8.4f %7.1f%%"
                      % (b, nl, n, 100.0 * n / m.sum(), fr, 100.0 * shr))

        # ---- layer composition (barrel-MD fraction) for the transition band ----------
        m = band_mask(d, 1.1, 1.7) & ch
        nmd = np.maximum(1, d["tc_dbgNMD"])
        fb = d["tc_dbgNB"] / nmd
        fps = d["tc_dbgNPS"] / nmd
        print("\n  --- transition: chain TCs by barrel-MD fraction ---")
        edges = [(-0.01, 0.001, "pure endcap"), (0.001, 0.999, "MIXED bar+end"), (0.999, 1.01, "pure barrel")]
        for lo2, hi2, lab in edges:
            s = m & (fb > lo2) & (fb <= hi2)
            if s.sum() == 0:
                continue
            print("      %-14s n=%7d (%5.1f%%) fake=%.4f fakeshare=%5.1f%% meanNL=%.2f meanPSfrac=%.2f"
                  % (lab, s.sum(), 100.0 * s.sum() / m.sum(), fk[s].mean(),
                     100.0 * fk[s].sum() / max(1, fk[m].sum()), d["tc_dbgNL"][s].mean(), fps[s].mean()))
        print("\n  --- transition: mixed-chain fake anatomy by (branch, nL) ---")
        mm = m & (fb > 0.001) & (fb < 0.999)
        for b in sorted(set(d["tc_dbgBr"][mm].tolist())):
            for nl in sorted(set(d["tc_dbgNL"][mm].tolist())):
                s = mm & (d["tc_dbgBr"] == b) & (d["tc_dbgNL"] == nl)
                if s.sum() < 50:
                    continue
                print("      %-10s nL=%d n=%7d fake=%.4f fakeshare(band)=%5.1f%%"
                      % (BR.get(int(b), str(b)), nl, s.sum(), fk[s].mean(),
                         100.0 * fk[s].sum() / max(1, fk[m].sum())))

        # ---- innermost layer / nNodes for transition fakes ---------------------------
        print("\n  --- transition chain TCs by innermost MD layer ---")
        for il in sorted(set(d["tc_dbgInLay"][m].tolist())):
            s = m & (d["tc_dbgInLay"] == il)
            if s.sum() < 50:
                continue
            print("      inLay=%2d n=%7d (%5.1f%%) fake=%.4f fakeshare=%5.1f%%"
                  % (il, s.sum(), 100.0 * s.sum() / m.sum(), fk[s].mean(),
                     100.0 * fk[s].sum() / max(1, fk[m].sum())))
        print("\n  --- transition chain TCs by nNodes ---")
        for nn in sorted(set(d["tc_dbgNN"][m].tolist())):
            s = m & (d["tc_dbgNN"] == nn)
            if s.sum() < 50:
                continue
            print("      nNodes=%d n=%7d (%5.1f%%) fake=%.4f fakeshare=%5.1f%%"
                  % (nn, s.sum(), 100.0 * s.sum() / m.sum(), fk[s].mean(),
                     100.0 * fk[s].sum() / max(1, fk[m].sum())))

        # ---- margin distributions of transition fakes vs trues ----------------------
        print("\n  --- transition margins (mP, mD) fake vs true, by branch ---")
        for b in sorted(set(d["tc_dbgBr"][m].tolist())):
            s = m & (d["tc_dbgBr"] == b)
            if s.sum() < 50:
                continue
            for lab, sub in (("FAKE", s & fk), ("TRUE", s & ~fk)):
                if sub.sum() < 20:
                    continue
                mp, md = d["tc_dbgMP"][sub], d["tc_dbgMD"][sub]
                print("      %-10s %s n=%7d mP: p10=%6.2f p50=%6.2f p90=%6.2f | mD: p10=%6.2f p50=%6.2f p90=%6.2f"
                      % (BR.get(int(b), str(b)), lab, sub.sum(),
                         np.percentile(mp, 10), np.percentile(mp, 50), np.percentile(mp, 90),
                         np.percentile(md, 10), np.percentile(md, 50), np.percentile(md, 90)))
        # same, comparing to barrel/endcap for the dominant branch
        print("\n  --- 5+IP margins by band (fake vs true) ---")
        for name, lo, hi in BANDS:
            s = band_mask(d, lo, hi) & ch & (d["tc_dbgBr"] == 2)
            for lab, sub in (("FAKE", s & fk), ("TRUE", s & ~fk)):
                if sub.sum() < 20:
                    continue
                mp = d["tc_dbgMP"][sub]
                mx = np.maximum(d["tc_dbgMP"][sub], d["tc_dbgMD"][sub])
                print("      %-11s %s n=%7d mP p10/p50/p90=%6.2f/%6.2f/%6.2f  mX p10/p50/p90=%6.2f/%6.2f/%6.2f"
                      % (name, lab, sub.sum(), np.percentile(mp, 10), np.percentile(mp, 50),
                         np.percentile(mp, 90), np.percentile(mx, 10), np.percentile(mx, 50),
                         np.percentile(mx, 90)))


if __name__ == "__main__":
    main()
