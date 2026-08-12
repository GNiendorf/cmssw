#!/usr/bin/env python3
"""M3 (b): PAIRED jet-physics comparison of two arms of the SAME 100 events.

Unpaired rates on 100 events cannot resolve a small cap effect (4k jet-core denominators -> ~1.3%
binomial error per band). The arms are the same events in the same order with the same sim-track
lists, so every sim track can be paired by (event, sim index) and the comparison becomes McNemar:
only the DISCORDANT tracks (matched in one arm and not the other) carry information, and the error
on the difference is sqrt(n01 + n10) rather than sqrt(N * p * (1-p)).

usage: jetcmp.py <A.root> <B.root> [--labels A,B] [--skip 5,85]
"""
import sys

import numpy as np
import ROOT

sys.path.insert(0, "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/m3_ref")
from jetphys import (PTCUT, ETACUT, VZCUT, VPCUT, JETPT, JETETA, DRBANDS, TYPES,  # noqa: E402
                     dr_closest_jet, gv)


def load(path, skip):
    """skip = a set of (run, lumi, evt) identity tuples to drop. Entries are visited in
    (run, lumi, evt) order, NOT file order: with -s 4 the writer emits events in stream-completion
    order, so entry i of two arms is not the same event and pairing by entry index is wrong."""
    f = ROOT.TFile.Open(path)
    t = f.Get("tree")
    out = dict(core=[], matched=[], dr=[], tc=[], nev=0,
               fake_d=0, fake_n=0, dup_n=0, tc_tot=0, typed={k: 0 for k in TYPES},
               fband_d=[0] * len(DRBANDS), fband_f=[0] * len(DRBANDS), fband_u=[0] * len(DRBANDS),
               ev=[])
    ids = []
    for i in range(t.GetEntries()):
        t.GetEntry(i)
        ids.append(((int(t.run), int(t.lumi), int(t.evt)), i))
    ids.sort()
    for ident, i in ids:
        if ident in skip:
            continue
        t.GetEntry(i)
        out["nev"] += 1
        spt, seta = gv(t, "sim_pt"), gv(t, "sim_eta")
        svx, svy, svz = gv(t, "sim_vx"), gv(t, "sim_vy"), gv(t, "sim_vz")
        sq = gv(t, "sim_q", np.int64)
        stc = gv(t, "sim_tcIdx", np.int64)
        gjidx = gv(t, "sim_genjet_idx", np.int64)
        sdr = gv(t, "sim_genjet_deltaR")
        jpt, jeta, jphi = gv(t, "genjet_pt"), gv(t, "genjet_eta"), gv(t, "genjet_phi")
        vperp = np.sqrt(svx ** 2 + svy ** 2)
        base = (sq != 0) & (spt > PTCUT) & (np.abs(seta) < ETACUT) & \
               (np.abs(svz) < VZCUT) & (vperp < VPCUT)
        core = base & (gjidx >= 0) & (gjidx < len(jpt))
        idx = np.where(core)[0]
        core = np.zeros(len(base), dtype=bool)
        if len(idx):
            good = (jpt[gjidx[idx]] > JETPT) & (np.abs(jeta[gjidx[idx]]) < JETETA)
            core[idx[good]] = True
        out["core"].append(core)
        out["matched"].append(stc >= 0)
        out["dr"].append(sdr.astype(np.float32))
        out["ev"].append(np.full(len(base), hash(ident) & 0x7fffffff, dtype=np.int64))

        tpt, teta, tphi = gv(t, "tc_pt"), gv(t, "tc_eta"), gv(t, "tc_phi")
        tct = gv(t, "tc_type", np.int64)
        tfake = gv(t, "tc_isFake", np.int64)
        tdup = gv(t, "tc_isDuplicate", np.int64)
        fd = (tpt > PTCUT) & (np.abs(teta) < ETACUT)
        out["tc_tot"] += len(tpt)
        out["fake_d"] += int(fd.sum())
        out["fake_n"] += int(tfake[fd].sum())
        out["dup_n"] += int(tdup[fd].sum())
        for k in TYPES:
            out["typed"][k] += int((fd & (tct == k)).sum())
        tdr = dr_closest_jet(teta[fd], tphi[fd], jpt, jeta, jphi)
        for b, (lo, hi) in enumerate(DRBANDS):
            sel = (tdr >= lo) & (tdr < hi)
            out["fband_d"][b] += int(sel.sum())
            out["fband_f"][b] += int(tfake[fd][sel].sum())
            out["fband_u"][b] += int(tdup[fd][sel].sum())
    f.Close()
    for k in ("core", "matched", "dr", "ev"):
        out[k] = np.concatenate(out[k])
    return out


def main():
    args = sys.argv[1:]
    skip = set()
    if "--skip" in args:
        j = args.index("--skip")
        skip = set(tuple(int(y) for y in x.split(":")) for x in args[j + 1].split(",") if x)
        del args[j:j + 2]
    labels = ["A", "B"]
    if "--labels" in args:
        j = args.index("--labels")
        labels = args[j + 1].split(",")
        del args[j:j + 2]
    A, B = load(args[0], skip), load(args[1], skip)
    assert len(A["core"]) == len(B["core"]), "sim track lists differ -- arms are not pairable"
    assert np.array_equal(A["ev"], B["ev"])
    assert np.array_equal(A["core"], B["core"]), "jet-core selection differs between arms"

    c = A["core"]
    ma, mb = A["matched"][c], B["matched"][c]
    dr = A["dr"][c]
    print("PAIRED on %d events, %d jet-core sim tracks (%s vs %s)"
          % (A["nev"], c.sum(), labels[0], labels[1]))
    print("  eff  %s %.4f   %s %.4f" % (labels[0], ma.mean(), labels[1], mb.mean()))
    n10 = int((ma & ~mb).sum())
    n01 = int((~ma & mb).sum())
    d = (n01 - n10) / len(ma)
    sig = np.sqrt(n01 + n10) / len(ma)
    print("  discordant: %s-only %d, %s-only %d  ->  delta eff %+.4f +- %.4f (%.1f sigma)"
          % (labels[0], n10, labels[1], n01, d, sig, d / sig if sig else 0))
    print("  by dR band (jet-core denominator):")
    for lo, hi in DRBANDS:
        s = (dr >= lo) & (dr < hi)
        if not s.any():
            continue
        a10 = int((ma[s] & ~mb[s]).sum())
        a01 = int((~ma[s] & mb[s]).sum())
        dd = (a01 - a10) / s.sum()
        ss = np.sqrt(a01 + a10) / s.sum()
        print("     dR [%.2f,%-5s) n=%5d  %s %.4f  %s %.4f  delta %+.4f +- %.4f  (%d/%d discordant)"
              % (lo, "inf" if hi > 100 else "%.2f" % hi, s.sum(), labels[0], ma[s].mean(),
                 labels[1], mb[s].mean(), dd, ss, a10, a01))
    print("  all-sim eff: %s %.4f  %s %.4f"
          % (labels[0], A["matched"].mean(), labels[1], B["matched"].mean()))

    def r(x, y):
        return x / y if y else float("nan")
    print("  fake  %s %.4f  %s %.4f   (delta %+.4f)"
          % (labels[0], r(A["fake_n"], A["fake_d"]), labels[1], r(B["fake_n"], B["fake_d"]),
             r(B["fake_n"], B["fake_d"]) - r(A["fake_n"], A["fake_d"])))
    print("  dup   %s %.4f  %s %.4f   (delta %+.4f)"
          % (labels[0], r(A["dup_n"], A["fake_d"]), labels[1], r(B["dup_n"], B["fake_d"]),
             r(B["dup_n"], B["fake_d"]) - r(A["dup_n"], A["fake_d"])))
    print("  TCs/evt %s %.1f  %s %.1f ; denom TCs %d vs %d"
          % (labels[0], A["tc_tot"] / A["nev"], labels[1], B["tc_tot"] / B["nev"],
             A["fake_d"], B["fake_d"]))
    print("  TC type counts (denominator):")
    for k, nm in TYPES.items():
        print("     %-4s %8d -> %8d  (%+.1f%%)"
              % (nm, A["typed"][k], B["typed"][k],
                 100.0 * (B["typed"][k] - A["typed"][k]) / max(A["typed"][k], 1)))
    print("  fake / dup vs dR(reco, closest genjet):")
    for b, (lo, hi) in enumerate(DRBANDS):
        print("     dR [%.2f,%-5s) n %6d->%6d  fake %.4f->%.4f  dup %.4f->%.4f"
              % (lo, "inf" if hi > 100 else "%.2f" % hi, A["fband_d"][b], B["fband_d"][b],
                 r(A["fband_f"][b], A["fband_d"][b]), r(B["fband_f"][b], B["fband_d"][b]),
                 r(A["fband_u"][b], A["fband_d"][b]), r(B["fband_u"][b], B["fband_d"][b])))


if __name__ == "__main__":
    main()
