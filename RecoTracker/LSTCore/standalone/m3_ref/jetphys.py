#!/usr/bin/env python3
"""M3 (a)/(b): jet physics of an LST ntuple, computed OFFLINE and UNBINNED.

Why not createPerfNumDenHists: its deltaR axis is 50 bins over [0, 0.1] and 27.7% of jet-core sim
tracks sit beyond 0.1, so everything outside the core would be silently dropped into an overflow
bin. Every definition below is a literal transcription of efficiency/src/performance.cc so the
numbers are the official ones, just without the histogram binning:

  eff denominator (TC_base_0_0, the 'all' category):
      q != 0, sim_pt > 0.9, |sim_eta| < 4.5, |sim_vz| < 30, sqrt(vx^2+vy^2) < 2.5
      -J adds:  genjet_pt[sim_genjet_idx] > 1000  and  |genjet_eta[sim_genjet_idx]| < 2.5
  eff numerator:   sim_tcIdx >= 0
  fake/dup denominator: tc_pt > 0.9 and |tc_eta| < 4.5   (no jet requirement, as in the tool)
  fake numerator:  tc_isFake ; dup numerator: tc_isDuplicate
  dR to the closest selected genjet for a reco track: dRClosestJet() replica (same pt/eta jet cuts)

usage: jetphys.py <out.root> [<out2.root> ...] [--json out.json] [--skip 5,85]
       --skip drops those EVENT INDICES (0-based, file order) from every arm, which is how the
       cap-off arm (whose overflow events carry no chain candidates) is compared like for like.
"""
import json
import sys

import numpy as np
import ROOT

PTCUT = 0.9
ETACUT = 4.5
VZCUT = 30.0
VPCUT = 2.5
JETPT = 1000.0
JETETA = 2.5
DRBANDS = [(0.0, 0.02), (0.02, 0.05), (0.05, 0.1), (0.1, 0.2), (0.2, 0.4), (0.4, 9e9)]
TYPES = {4: "T5", 5: "pT3", 7: "pT5", 8: "pLS", 9: "T4"}


def dr_closest_jet(eta, phi, jpt, jeta, jphi):
    m = (jpt > JETPT) & (np.abs(jeta) < JETETA)
    if not m.any() or len(eta) == 0:
        return np.full(len(eta), 999.0, dtype=np.float64)
    je, jp = jeta[m], jphi[m]
    dphi = np.abs(phi[:, None] - jp[None, :])
    dphi = np.where(dphi > np.pi, dphi - 2 * np.pi, dphi)   # verbatim: the tool does NOT abs() again
    d2 = (eta[:, None] - je[None, :]) ** 2 + dphi ** 2
    return np.sqrt(d2.min(axis=1))


def gv(t, name, dtype=np.float64):
    return np.asarray(getattr(t, name), dtype=dtype)


def analyze(path, skip):
    f = ROOT.TFile.Open(path)
    t = f.Get("tree")
    n = t.GetEntries()
    acc = dict(
        nev=0, nskip=0,
        d_all=0, n_all=0, d_core=0, n_core=0,
        d_fake=0, n_fake=0, n_dup=0,
        tc_total=0, type_d={k: 0 for k in TYPES}, type_n_match={k: 0 for k in TYPES},
        core_by_type={k: 0 for k in TYPES},
        band_d=[0] * len(DRBANDS), band_n=[0] * len(DRBANDS),
        fband_d=[0] * len(DRBANDS), fband_f=[0] * len(DRBANDS), fband_u=[0] * len(DRBANDS),
        core_dr=[],
    )
    for i in range(n):
        t.GetEntry(i)
        if i in skip:
            acc["nskip"] += 1
            continue
        acc["nev"] += 1
        spt, seta, sphi = gv(t, "sim_pt"), gv(t, "sim_eta"), gv(t, "sim_phi")
        svx, svy, svz = gv(t, "sim_vx"), gv(t, "sim_vy"), gv(t, "sim_vz")
        sq = gv(t, "sim_q", np.int64)
        stc = gv(t, "sim_tcIdx", np.int64)
        gjidx = gv(t, "sim_genjet_idx", np.int64)
        sdr = gv(t, "sim_genjet_deltaR")
        jpt, jeta, jphi = gv(t, "genjet_pt"), gv(t, "genjet_eta"), gv(t, "genjet_phi")
        vperp = np.sqrt(svx ** 2 + svy ** 2)

        base = (sq != 0) & (spt > PTCUT) & (np.abs(seta) < ETACUT) & \
               (np.abs(svz) < VZCUT) & (vperp < VPCUT)
        ok = (gjidx >= 0) & (gjidx < len(jpt))
        core = base & ok
        if core.any():
            core = core.copy()
            idx = np.where(core)[0]
            good = (jpt[gjidx[idx]] > JETPT) & (np.abs(jeta[gjidx[idx]]) < JETETA)
            core[:] = False
            core[idx[good]] = True
        matched = stc >= 0
        acc["d_all"] += int(base.sum())
        acc["n_all"] += int((base & matched).sum())
        acc["d_core"] += int(core.sum())
        acc["n_core"] += int((core & matched).sum())

        # eff vs UNBINNED deltaR of the sim track to its own genjet, jet-core denominator
        dr = sdr[core]
        mm = matched[core]
        acc["core_dr"].append(dr.astype(np.float32))
        for b, (lo, hi) in enumerate(DRBANDS):
            sel = (dr >= lo) & (dr < hi)
            acc["band_d"][b] += int(sel.sum())
            acc["band_n"][b] += int((sel & mm).sum())

        # which TC type matched each core sim track
        tct = gv(t, "tc_type", np.int64)
        cm = np.where(core & matched)[0]
        if len(cm):
            mt = tct[stc[cm]]
            for k in TYPES:
                acc["core_by_type"][k] += int((mt == k).sum())

        # fake / dup
        tpt, teta, tphi = gv(t, "tc_pt"), gv(t, "tc_eta"), gv(t, "tc_phi")
        tfake = gv(t, "tc_isFake", np.int64)
        tdup = gv(t, "tc_isDuplicate", np.int64)
        acc["tc_total"] += len(tpt)
        fd = (tpt > PTCUT) & (np.abs(teta) < ETACUT)
        acc["d_fake"] += int(fd.sum())
        acc["n_fake"] += int(tfake[fd].sum())
        acc["n_dup"] += int(tdup[fd].sum())
        for k in TYPES:
            sel = fd & (tct == k)
            acc["type_d"][k] += int(sel.sum())
            acc["type_n_match"][k] += int((sel & (tfake == 0)).sum())
        tdr = dr_closest_jet(teta[fd], tphi[fd], jpt, jeta, jphi)
        for b, (lo, hi) in enumerate(DRBANDS):
            sel = (tdr >= lo) & (tdr < hi)
            acc["fband_d"][b] += int(sel.sum())
            acc["fband_f"][b] += int(tfake[fd][sel].sum())
            acc["fband_u"][b] += int(tdup[fd][sel].sum())
    f.Close()
    acc["core_dr"] = np.concatenate(acc["core_dr"]) if acc["core_dr"] else np.zeros(0, np.float32)
    return acc


def rate(a, b):
    return float(a) / b if b else float("nan")


def report(tag, a):
    print("\n===== %s  (%d events used, %d skipped) =====" % (tag, a["nev"], a["nskip"]))
    print("  eff  all-sim   %.4f  (%d/%d)" % (rate(a["n_all"], a["d_all"]), a["n_all"], a["d_all"]))
    print("  eff  jet-core  %.4f  (%d/%d)   denom/evt %.1f"
          % (rate(a["n_core"], a["d_core"]), a["n_core"], a["d_core"],
             a["d_core"] / max(a["nev"], 1)))
    print("  fake           %.4f  (%d/%d)" % (rate(a["n_fake"], a["d_fake"]), a["n_fake"], a["d_fake"]))
    print("  dup            %.4f  (%d/%d)" % (rate(a["n_dup"], a["d_fake"]), a["n_dup"], a["d_fake"]))
    print("  TCs/evt        %.1f   (denom TCs/evt %.1f)"
          % (a["tc_total"] / max(a["nev"], 1), a["d_fake"] / max(a["nev"], 1)))
    print("  TC type mix (denom count, /evt, non-fake frac, jet-core sim tracks won):")
    for k, nm in TYPES.items():
        d = a["type_d"][k]
        print("     %-4s %8d  %7.1f/evt  nonfake %.4f   core-eff share %d"
              % (nm, d, d / max(a["nev"], 1), rate(a["type_n_match"][k], d), a["core_by_type"][k]))
    dr = a["core_dr"]
    if len(dr):
        print("  jet-core sim deltaR distribution: median %.4f  p90 %.4f  p99 %.4f  max %.4f"
              % (np.median(dr), np.percentile(dr, 90), np.percentile(dr, 99), dr.max()))
        print("    fraction beyond the -J histogram axis (dR > 0.1): %.4f" % (dr > 0.1).mean())
    print("  eff vs deltaR (unbinned bands, jet-core denominator):")
    for b, (lo, hi) in enumerate(DRBANDS):
        print("     dR [%.2f,%s)  eff %.4f   (%d/%d)"
              % (lo, "inf" if hi > 100 else "%.2f" % hi,
                 rate(a["band_n"][b], a["band_d"][b]), a["band_n"][b], a["band_d"][b]))
    print("  fake / dup vs dR(reco track, closest selected genjet):")
    for b, (lo, hi) in enumerate(DRBANDS):
        print("     dR [%.2f,%s)  fake %.4f  dup %.4f   (n=%d)"
              % (lo, "inf" if hi > 100 else "%.2f" % hi,
                 rate(a["fband_f"][b], a["fband_d"][b]), rate(a["fband_u"][b], a["fband_d"][b]),
                 a["fband_d"][b]))


def main():
    args = [x for x in sys.argv[1:]]
    skip = set()
    if "--skip" in args:
        j = args.index("--skip")
        skip = set(int(x) for x in args[j + 1].split(",") if x != "")
        del args[j:j + 2]
    outjson = None
    if "--json" in args:
        j = args.index("--json")
        outjson = args[j + 1]
        del args[j:j + 2]
    out = {}
    for p in args:
        a = analyze(p, skip)
        tag = p.split("/")[-1]
        report(tag, a)
        s = {k: v for k, v in a.items() if k != "core_dr"}
        s["eff_all"] = rate(a["n_all"], a["d_all"])
        s["eff_core"] = rate(a["n_core"], a["d_core"])
        s["fake"] = rate(a["n_fake"], a["d_fake"])
        s["dup"] = rate(a["n_dup"], a["d_fake"])
        s["dr_pctl"] = {p2: float(np.percentile(a["core_dr"], p2)) for p2 in (50, 90, 99, 100)} \
            if len(a["core_dr"]) else {}
        out[tag] = s
    if outjson:
        with open(outjson, "w") as f:
            json.dump(out, f, indent=1)
        print("\nwrote", outjson)


if __name__ == "__main__":
    main()
