#!/usr/bin/env python3
"""P4 REFEREE -- the PAIRED dR-resolved jet-band gate.  v1 (2026-08-12).

What it is
----------
JPR's unbinned jet physics (`m3_ref/jetphys.py`, a literal transcription of
`efficiency/src/performance.cc`) plus the two things a referee needs on top of it:

  1. EVENT SPLIT.  Every number can be restricted to the jet TUNE half (file rows 0-499) or the
     jet HOLDOUT half (rows 500-999).  Arms iterate on the tune half; promotion is decided here on
     the holdout half.  Events are identified by a CONTENT KEY, not by tree row: with -s > 1 the
     writer emits events in stream-completion order, so row i of two runs of the same input file
     are DIFFERENT events (measured: our 1000-event arm starts at input row 707).  LST master's
     ntuple carries no run/lumi/evt branches, so the key has to be content-derived to compare
     against master at all:

         key = nSim * 1e9 + sum(sim_pt) + 1e-3 * sum(|sim_eta|)

     computed identically on the INPUT ntuple (which defines the split) and on any output ntuple.
     Validated: our arm's 1000 keys are set-equal to the input's, and the key-derived split agrees
     row-for-row with the (run,lumi,evt) branches our writer does provide (--validate).

  2. PAIRING.  Two arms are run on the same events, so the naive binomial error bar on a difference
     is wrong (McNemar; see a5_ref/paired.py).  Efficiency deltas are paired at the SIM level: the
     output sim collection is the input's, in input order (sim_trkNtupIdx == arange, asserted), so
     sim rows of a common event line up one-to-one.  Reported per dR band: b (ref-only), c
     (arm-only), paired sigma sqrt(b+c)/n, exact/normal two-sided McNemar p.
     Fake and dup rates have no sim-level pairing (the TC collections differ), so their deltas get
     a CLUSTERED BOOTSTRAP over events (the events are shared, which is the pairing) -- resample
     events with replacement, recompute the pooled ratio for both arms on the same resample, take
     the spread of the difference.

Definitions (unchanged from jetphys.py, so the numbers are comparable to FINDINGS_JETPHYS.md)
    eff denominator  : sim_q != 0, sim_pt > 0.9, |sim_eta| < 4.5, |sim_vz| < 30, vxy < 2.5
    jet-core denom   : the above AND its genjet has pt > 1000 and |eta| < 2.5
    eff numerator    : sim_tcIdx >= 0
    eff dR bands     : sim_genjet_deltaR (unbinned), jet-core denominator
    fake/dup denom   : tc_pt > 0.9 and |tc_eta| < 4.5  (no jet requirement, as in the tool)
    fake/dup bands   : dR(reco track, closest selected genjet), dRClosestJet() replica

usage:
    jetgate.py --split holdout REF.root [ARM.root ...] [--labels ref,arm] [--json out.json]
    jetgate.py --split tune|holdout|all ...        # default holdout
    jetgate.py --validate REF.root                 # key/split self-check, no physics
"""
import json
import math
import os
import sys

import awkward as ak
import numpy as np
import uproot

INPUT_JETS = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "..", "jet_ref", "trackingNtuple_jets_1000.root")
NTUNE = 500          # file rows [0, NTUNE) = TUNE, [NTUNE, end) = HOLDOUT

PTCUT, ETACUT, VZCUT, VPCUT = 0.9, 4.5, 30.0, 2.5
JETPT, JETETA = 1000.0, 2.5
# FINE CORE BINS (maintainer directive, COORDINATOR 17:20): ~.0025 wide below dR = .02, because
# even master's efficiency levels off by ~.02, so a single [0,.02) bin averages over the whole
# battleground and a shallow-core gain would be creditable as a deep-core one.
DRBANDS = [(0.0, 0.0025), (0.0025, 0.005), (0.005, 0.0075), (0.0075, 0.01),
           (0.01, 0.0125), (0.0125, 0.015), (0.015, 0.0175), (0.0175, 0.02),
           (0.02, 0.03), (0.03, 0.04), (0.04, 0.05),
           (0.05, 0.1), (0.1, 0.2), (0.2, 0.4), (0.4, 9e9)]
# named aggregates over those bins.  [0,.05] with fine core bins is the window of interest
# (COORDINATOR 17:20 / 17:24 / 17:27); beyond .05 stays in the table as an appendix, not truncated.
AGGS = [("<.005", (0, 2)), ("[.005,.01)", (2, 4)), ("[.01,.02)", (4, 8)),
        ("<.02", (0, 8)), ("[.02,.05)", (8, 11)), ("<.05", (0, 11)), (">.05", (11, 15))]
TYPES = {4: "T5", 5: "pT3", 7: "pT5", 8: "pLS", 9: "T4"}
NBOOT = 4000
RNG = np.random.default_rng(20260812)

SIMBR = ["sim_pt", "sim_eta", "sim_phi", "sim_vx", "sim_vy", "sim_vz", "sim_q", "sim_tcIdx",
         "sim_genjet_idx", "sim_genjet_deltaR"]
TCBR = ["tc_pt", "tc_eta", "tc_phi", "tc_isFake", "tc_isDuplicate", "tc_type"]
JETBR = ["genjet_pt", "genjet_eta", "genjet_phi"]


def band_label(lo, hi):
    f = "%.4f" if hi <= 0.02 else "%.2f"
    return "[%s,%s)" % ((f % lo).rstrip("0").rstrip(".") or "0",
                        "inf" if hi > 100 else (f % hi).rstrip("0").rstrip("."))


def content_keys(d):
    """The event key.  d must carry sim_pt, sim_eta as jagged arrays."""
    n = np.asarray(ak.num(d["sim_pt"], axis=1), dtype=np.float64)
    return n * 1.0e9 + np.asarray(ak.sum(d["sim_pt"], axis=1), dtype=np.float64) \
        + 1.0e-3 * np.asarray(ak.sum(abs(d["sim_eta"]), axis=1), dtype=np.float64)


def split_keys(path=INPUT_JETS):
    """(tune_keys, holdout_keys) in INPUT FILE ORDER."""
    t = uproot.open(path)["trackingNtuple/tree"]
    d = t.arrays(["sim_pt", "sim_eta"])
    k = content_keys(d)
    assert len(np.unique(k)) == len(k), "content keys collide in the input ntuple"
    return k[:NTUNE], k[NTUNE:]


def open_arm(path):
    t = uproot.open(path)["tree"]
    have = set(t.keys())
    br = [b for b in SIMBR + TCBR + JETBR if b in have]
    d = t.arrays(br)
    out = {b: d[b] for b in br}
    out["_key"] = content_keys(out)
    out["_n"] = t.num_entries
    out["_rle"] = None
    if {"run", "lumi", "evt"} <= have:
        r = t.arrays(["run", "lumi", "evt"], library="np")
        out["_rle"] = np.stack([r["run"], r["lumi"], r["evt"]]).T
    out["_path"] = path
    return out


def gv(arm, name, i, dtype=np.float64):
    return np.asarray(arm[name][i], dtype=dtype)


def per_event(arm, rows):
    """Everything the aggregations need, one record per selected event."""
    recs = []
    for i in rows:
        spt, seta = gv(arm, "sim_pt", i), gv(arm, "sim_eta", i)
        svx, svy, svz = gv(arm, "sim_vx", i), gv(arm, "sim_vy", i), gv(arm, "sim_vz", i)
        sq = gv(arm, "sim_q", i, np.int64)
        stc = gv(arm, "sim_tcIdx", i, np.int64)
        gj = gv(arm, "sim_genjet_idx", i, np.int64)
        sdr = gv(arm, "sim_genjet_deltaR", i)
        jpt, jeta, jphi = gv(arm, "genjet_pt", i), gv(arm, "genjet_eta", i), gv(arm, "genjet_phi", i)
        vperp = np.sqrt(svx ** 2 + svy ** 2)
        base = (sq != 0) & (spt > PTCUT) & (np.abs(seta) < ETACUT) & \
               (np.abs(svz) < VZCUT) & (vperp < VPCUT)
        core = base.copy()
        ok = (gj >= 0) & (gj < len(jpt))
        core &= ok
        idx = np.where(core)[0]
        if len(idx):
            good = (jpt[gj[idx]] > JETPT) & (np.abs(jeta[gj[idx]]) < JETETA)
            core[:] = False
            core[idx[good]] = True
        else:
            core[:] = False
        matched = stc >= 0

        tpt, teta, tphi = gv(arm, "tc_pt", i), gv(arm, "tc_eta", i), gv(arm, "tc_phi", i)
        tfake = gv(arm, "tc_isFake", i, np.int64)
        tdup = gv(arm, "tc_isDuplicate", i, np.int64)
        ttype = gv(arm, "tc_type", i, np.int64)
        fd = (tpt > PTCUT) & (np.abs(teta) < ETACUT)
        # dRClosestJet replica (verbatim, including the one-sided dphi wrap of the tool)
        m = (jpt > JETPT) & (np.abs(jeta) < JETETA)
        if m.any() and fd.sum():
            je, jp = jeta[m], jphi[m]
            dphi = np.abs(tphi[fd][:, None] - jp[None, :])
            dphi = np.where(dphi > np.pi, dphi - 2 * np.pi, dphi)
            tdr = np.sqrt(((teta[fd][:, None] - je[None, :]) ** 2 + dphi ** 2).min(axis=1))
        else:
            tdr = np.full(int(fd.sum()), 999.0)
        recs.append(dict(base=base, core=core, matched=matched, sdr=sdr, spt=spt, seta=seta,
                         fd_type=ttype[fd], fd_fake=tfake[fd], fd_dup=tdup[fd], fd_dr=tdr,
                         ntc=len(tpt), ttype=ttype, tfake=tfake, fd=fd))
    return recs


def aggregate(recs):
    a = dict(nev=len(recs), d_all=0, n_all=0, d_core=0, n_core=0, d_fake=0, n_fake=0, n_dup=0,
             tc_total=0, band_d=[0] * len(DRBANDS), band_n=[0] * len(DRBANDS),
             fband_d=[0] * len(DRBANDS), fband_f=[0] * len(DRBANDS), fband_u=[0] * len(DRBANDS),
             type_d={k: 0 for k in TYPES}, type_nf={k: 0 for k in TYPES},
             core_by_type={k: 0 for k in TYPES})
    for r in recs:
        a["d_all"] += int(r["base"].sum())
        a["n_all"] += int((r["base"] & r["matched"]).sum())
        a["d_core"] += int(r["core"].sum())
        a["n_core"] += int((r["core"] & r["matched"]).sum())
        dr, mm = r["sdr"][r["core"]], r["matched"][r["core"]]
        for b, (lo, hi) in enumerate(DRBANDS):
            s = (dr >= lo) & (dr < hi)
            a["band_d"][b] += int(s.sum())
            a["band_n"][b] += int((s & mm).sum())
        a["tc_total"] += r["ntc"]
        a["d_fake"] += int(r["fd"].sum())
        a["n_fake"] += int(r["fd_fake"].sum())
        a["n_dup"] += int(r["fd_dup"].sum())
        for b, (lo, hi) in enumerate(DRBANDS):
            s = (r["fd_dr"] >= lo) & (r["fd_dr"] < hi)
            a["fband_d"][b] += int(s.sum())
            a["fband_f"][b] += int(r["fd_fake"][s].sum())
            a["fband_u"][b] += int(r["fd_dup"][s].sum())
        for k in TYPES:
            s = r["fd_type"] == k
            a["type_d"][k] += int(s.sum())
            a["type_nf"][k] += int((s & (r["fd_fake"] == 0)).sum())
    return a


def rate(x, y):
    return float(x) / y if y else float("nan")


def mcnemar_p(b, c):
    n = b + c
    if n == 0:
        return 1.0
    if n > 800:
        z = abs(b - c) / math.sqrt(n)
        return math.erfc(z / math.sqrt(2))
    lo = min(b, c)
    p = sum(math.comb(n, k) for k in range(lo + 1)) * 0.5 ** n
    return min(1.0, 2 * p)


def boot_delta(numA, denA, numB, denB):
    """Clustered-over-events bootstrap sigma of (rateB - rateA); arrays are per-event counts."""
    nev = len(numA)
    if nev == 0:
        return float("nan"), float("nan")
    idx = RNG.integers(0, nev, size=(NBOOT, nev))
    dA = denA[idx].sum(1)
    dB = denB[idx].sum(1)
    with np.errstate(invalid="ignore", divide="ignore"):
        rA = np.where(dA > 0, numA[idx].sum(1) / np.maximum(dA, 1), np.nan)
        rB = np.where(dB > 0, numB[idx].sum(1) / np.maximum(dB, 1), np.nan)
    d = rB - rA
    sd = float(np.nanstd(d))
    obs = rate(numB.sum(), denB.sum()) - rate(numA.sum(), denA.sum())
    p = math.erfc(abs(obs) / (sd * math.sqrt(2))) if sd > 0 else float("nan")
    return sd, p


def report(tag, a):
    print("\n===== %s   (%d events) =====" % (tag, a["nev"]))
    print("  eff all-sim    %.4f  (%d/%d)" % (rate(a["n_all"], a["d_all"]), a["n_all"], a["d_all"]))
    print("  eff jet-core   %.4f  (%d/%d)   denom/evt %.1f"
          % (rate(a["n_core"], a["d_core"]), a["n_core"], a["d_core"], a["d_core"] / max(a["nev"], 1)))
    print("  fake           %.4f  (%d/%d)" % (rate(a["n_fake"], a["d_fake"]), a["n_fake"], a["d_fake"]))
    print("  dup            %.4f  (%d/%d)" % (rate(a["n_dup"], a["d_fake"]), a["n_dup"], a["d_fake"]))
    print("  TCs/evt        %.1f  (denom %.1f)"
          % (a["tc_total"] / max(a["nev"], 1), a["d_fake"] / max(a["nev"], 1)))
    print("  TC mix:  " + "  ".join(
        "%s %.1f/evt nf %.3f core %d" % (TYPES[k], a["type_d"][k] / max(a["nev"], 1),
                                         rate(a["type_nf"][k], a["type_d"][k]), a["core_by_type"][k])
        for k in TYPES))
    nev = max(a["nev"], 1)
    print("  AGGREGATES over the fine bins:")
    for nm, (i, j) in AGGS:
        cn, cd = sum(a["band_n"][i:j]), sum(a["band_d"][i:j])
        cf, cfd = sum(a["fband_f"][i:j]), sum(a["fband_d"][i:j])
        cu = sum(a["fband_u"][i:j])
        print("    dR %-11s eff %.4f (%5d/%5d)   fake %.4f (%5d/%5d, %6.2f/evt)   dup %.4f"
              % (nm, rate(cn, cd), cn, cd, rate(cf, cfd), cf, cfd, cf / nev, rate(cu, cfd)))
    print("  %-14s %8s %8s %9s | %8s %7s %8s %7s %8s"
          % ("dR band", "effN", "effD", "eff", "nTC", "TC/evt", "fake", "fk/evt", "dup"))
    for b, (lo, hi) in enumerate(DRBANDS):
        print("  %-14s %8d %8d %9.4f | %8d %7.2f %8.4f %7.2f %8.4f"
              % (band_label(lo, hi), a["band_n"][b], a["band_d"][b],
                 rate(a["band_n"][b], a["band_d"][b]), a["fband_d"][b], a["fband_d"][b] / nev,
                 rate(a["fband_f"][b], a["fband_d"][b]), a["fband_f"][b] / nev,
                 rate(a["fband_u"][b], a["fband_d"][b])))


def paired(la, ra, lb, rb):
    """ra, rb: per-event records for the SAME events in the same order."""
    assert len(ra) == len(rb)
    # sim-level alignment check
    nbad = 0
    for x, y in zip(ra, rb):
        if len(x["spt"]) != len(y["spt"]) or not np.array_equal(x["spt"], y["spt"]) \
           or not np.array_equal(x["core"], y["core"]):
            nbad += 1
    print("\n----- PAIRED  %s -> %s   (%d events, sim-misaligned events: %d) -----"
          % (la, lb, len(ra), nbad))
    if nbad:
        print("  WARNING: %d events have non-identical sim collections; eff pairing suspect" % nbad)
    core = np.concatenate([r["core"] for r in ra])
    dr = np.concatenate([r["sdr"] for r in ra])
    ma = np.concatenate([r["matched"] for r in ra])
    mb = np.concatenate([r["matched"] for r in rb])
    out = {}
    print("  EFFICIENCY (McNemar, paired on identical sims)")
    print("  %-14s %8s %8s %9s %7s %7s %8s %9s"
          % ("dR band", la, lb, "delta", "A-only", "B-only", "sigPair", "p"))
    rows = [("core-all", core)] + \
           [("AGG " + nm, core & (dr >= DRBANDS[i][0]) & (dr < DRBANDS[j - 1][1]))
            for nm, (i, j) in AGGS] + \
           [(band_label(lo, hi), core & (dr >= lo) & (dr < hi)) for lo, hi in DRBANDS]
    for nm, sel in rows:
        n = int(sel.sum())
        if n == 0:
            continue
        ea, eb = rate((sel & ma).sum(), n), rate((sel & mb).sum(), n)
        b = int((sel & ma & ~mb).sum())
        c = int((sel & ~ma & mb).sum())
        sig = math.sqrt(b + c) / n
        p = mcnemar_p(b, c)
        print("  %-14s %8.4f %8.4f %+9.4f %7d %7d %8.4f %9.3g" % (nm, ea, eb, eb - ea, b, c, sig, p))
        out["eff " + nm] = dict(a=ea, b=eb, delta=eb - ea, bonly=b, conly=c, sig=sig, p=p, n=n)
    for what, key in (("FAKE", "fd_fake"), ("DUP", "fd_dup")):
        print("  %s (clustered bootstrap over the shared events, %d resamples)" % (what, NBOOT))
        print("  %-14s %8s %8s %9s %8s %9s" % ("dR band", la, lb, "delta", "sigBoot", "p"))
        allrows = [("all", None)] + \
                  [("AGG " + nm, (DRBANDS[i][0], DRBANDS[j - 1][1])) for nm, (i, j) in AGGS] + \
                  [(band_label(lo, hi), (lo, hi)) for lo, hi in DRBANDS]
        for nm, bd in allrows:
            def counts(recs):
                num, den = [], []
                for r in recs:
                    if bd is None:
                        s = np.ones(len(r["fd_dr"]), bool)
                    else:
                        s = (r["fd_dr"] >= bd[0]) & (r["fd_dr"] < bd[1])
                    num.append(int(r[key][s].sum()))
                    den.append(int(s.sum()))
                return np.array(num, float), np.array(den, float)
            nA, dA = counts(ra)
            nB, dB = counts(rb)
            if dA.sum() == 0 and dB.sum() == 0:
                continue
            sd, p = boot_delta(nA, dA, nB, dB)
            fa, fb = rate(nA.sum(), dA.sum()), rate(nB.sum(), dB.sum())
            # per-event COUNT, paired event by event (a rate can fall while the count rises)
            dd = nB - nA
            cs = float(np.std(dd) / math.sqrt(len(dd))) if len(dd) else float("nan")
            print("  %-14s %8.4f %8.4f %+9.4f %8.4f %9.3g   | /evt %6.2f %6.2f %+6.2f +-%.2f"
                  % (nm, fa, fb, fb - fa, sd, p, nA.mean(), nB.mean(), dd.mean(), cs))
            out["%s %s" % (what.lower(), nm)] = dict(a=fa, b=fb, delta=fb - fa, sig=sd, p=p,
                                                     a_per_evt=float(nA.mean()),
                                                     b_per_evt=float(nB.mean()),
                                                     d_per_evt=float(dd.mean()), d_per_evt_sig=cs)
    return out


def rows_for(arm, keys):
    """Tree rows of this arm whose content key is in `keys`, ordered by key."""
    k = arm["_key"]
    assert len(np.unique(k)) == len(k), "content keys collide in " + arm["_path"]
    want = np.isin(k, keys)
    rows = np.where(want)[0]
    return rows[np.argsort(k[rows], kind="stable")]


def validate(path):
    tune, hold = split_keys()
    arm = open_arm(path)
    k = arm["_key"]
    print("arm %s: %d events, keys unique %s" % (path, len(k), len(np.unique(k)) == len(k)))
    print("set-equal to input keys: %s" % (set(k.tolist()) == set(tune.tolist()) | set(hold.tolist())))
    print("in tune half: %d   in holdout half: %d" % (np.isin(k, tune).sum(), np.isin(k, hold).sum()))
    if arm["_rle"] is not None:
        ti = uproot.open(INPUT_JETS)["trackingNtuple/tree"]
        r = ti.arrays(["run", "lumi", "event"], library="np")
        rle_in = np.stack([r["run"], r["lumi"], r["event"]]).T
        pos = {tuple(x): i for i, x in enumerate(rle_in.tolist())}
        rowin = np.array([pos[tuple(x)] for x in arm["_rle"].tolist()])
        kh = set(hold.tolist())
        agree = np.array([(rowin[i] >= NTUNE) == (k[i] in kh) for i in range(len(k))])
        print("(run,lumi,evt) split vs content-key split: agree on %d/%d rows" % (agree.sum(), len(k)))
        print("first tree rows map to input rows: %s ..." % rowin[:8].tolist())
    else:
        print("(no run/lumi/evt branches in this arm -- content key is the only handle)")


def main():
    av = sys.argv[1:]
    if "--validate" in av:
        validate(av[av.index("--validate") + 1])
        return
    split = "holdout"
    if "--split" in av:
        j = av.index("--split")
        split = av[j + 1]
        del av[j:j + 2]
    labels = None
    if "--labels" in av:
        j = av.index("--labels")
        labels = av[j + 1].split(",")
        del av[j:j + 2]
    outjson = None
    if "--json" in av:
        j = av.index("--json")
        outjson = av[j + 1]
        del av[j:j + 2]
    tune, hold = split_keys()
    keys = {"tune": tune, "holdout": hold, "all": np.concatenate([tune, hold])}[split]
    paths = av
    if labels is None:
        labels = [os.path.basename(p).replace(".root", "") for p in paths]
    print("SPLIT = %s   (%d events requested; jet tune = input rows 0-%d, holdout = %d-999)"
          % (split.upper(), len(keys), NTUNE - 1, NTUNE))
    arms, recs, aggs = [], [], []
    for p, l in zip(paths, labels):
        arm = open_arm(p)
        rows = rows_for(arm, keys)
        if len(rows) != len(keys):
            print("  NOTE %s: %d of %d requested events present" % (l, len(rows), len(keys)))
        r = per_event(arm, rows)
        a = aggregate(r)
        # core-by-type needs tc_type of the matching TC
        for i, row in enumerate(rows):
            rr = r[i]
            stc = gv(arm, "sim_tcIdx", row, np.int64)
            tt = gv(arm, "tc_type", row, np.int64)
            cm = np.where(rr["core"] & rr["matched"])[0]
            if len(cm):
                mt = tt[stc[cm]]
                for k in TYPES:
                    a["core_by_type"][k] += int((mt == k).sum())
        arms.append(arm)
        recs.append(r)
        aggs.append(a)
        report(l, a)
    res = {}
    for l, a in zip(labels, aggs):
        s = dict(a)
        s["eff_all"] = rate(a["n_all"], a["d_all"])
        s["eff_core"] = rate(a["n_core"], a["d_core"])
        s["fake"] = rate(a["n_fake"], a["d_fake"])
        s["dup"] = rate(a["n_dup"], a["d_fake"])
        s["eff_band"] = [rate(a["band_n"][b], a["band_d"][b]) for b in range(len(DRBANDS))]
        s["fake_band"] = [rate(a["fband_f"][b], a["fband_d"][b]) for b in range(len(DRBANDS))]
        s["dup_band"] = [rate(a["fband_u"][b], a["fband_d"][b]) for b in range(len(DRBANDS))]
        for nm, (i, j) in AGGS:
            s["eff_agg_" + nm] = rate(sum(a["band_n"][i:j]), sum(a["band_d"][i:j]))
            s["fake_agg_" + nm] = rate(sum(a["fband_f"][i:j]), sum(a["fband_d"][i:j]))
            s["fkevt_agg_" + nm] = sum(a["fband_f"][i:j]) / max(a["nev"], 1)
            s["den_agg_" + nm] = sum(a["band_d"][i:j])
        s["eff_core005"] = s["eff_agg_<.05"]
        s["fake_core005"] = s["fake_agg_<.05"]
        s["fk_evt_core005"] = s["fkevt_agg_<.05"]
        s["type_d"] = {TYPES[k]: v for k, v in a["type_d"].items()}
        s["type_nf"] = {TYPES[k]: rate(a["type_nf"][k], a["type_d"][k]) for k in TYPES}
        s["core_by_type"] = {TYPES[k]: v for k, v in a["core_by_type"].items()}
        res[l] = s
    if len(paths) > 1:
        # pair every arm against the first, on the events both have
        k0 = arms[0]["_key"]
        for j in range(1, len(paths)):
            common = np.intersect1d(np.intersect1d(k0, arms[j]["_key"]), keys)
            r0 = per_event(arms[0], rows_for(arms[0], common))
            rj = per_event(arms[j], rows_for(arms[j], common))
            res["PAIR %s->%s" % (labels[0], labels[j])] = paired(labels[0], r0, labels[j], rj)
    if outjson:
        with open(outjson, "w") as f:
            json.dump(res, f, indent=1, default=float)
        print("\nwrote", outjson)


if __name__ == "__main__":
    main()
