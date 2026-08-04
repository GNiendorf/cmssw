#!/usr/bin/env python3
"""Per-TC-type A/B comparison for the chain-tracking prototype (plan 10.5 drill-down).

Reads two LST-ntuple-shaped files directly (tree "tree"): --proto (hybrid ab_*.root)
and --base (the input LSTNtuple baseline). Unlike compare_ab.py, which judges the
createPerfNumDenHists aggregates, this tool slices every metric by tc_type so the
chain slice (types 4/9) can be judged against baseline's bare-OT T5/T4 slice
(same role) instead of the pixel-dominated aggregate, where pixel matching
strongly suppresses FR and would make the chains look artificially bad.

Slices: 7=pT5, 5=pT3, 8=pLS, 4=T5, 9=T4, chain-slice=4+9, pixel-slice=7+5+8, all.

Per slice, with plot-level TC cuts (tc_pt > 0.9, |tc_eta| < 4.5):
  nTC total / per event, FR (tc_isFake fraction), DR (tc_isDuplicate fraction),
  mean tc_nhitOT.
Per slice, the EFFICIENCY CONTRIBUTION: number of accepted sims whose sim_tcIdx
(best match, frac > 0.75) points at a TC of that slice's type. Sim selections
(all require sim_pt > 0.9, |sim_eta| < 4.5, sim_q != 0, |sim_vz| < 30):
  prompt   : vxy < 2.5   (the standard TrackingParticleSelector vertex cut)
  displaced: vxy in [1,5) and [5,30), the vxy < 2.5 cut dropped.
Note: the pointed-at TC is NOT re-cut in pt/eta (mirrors the harness numerators).

PIXEL-SLICE SANITY CHECK: hybrid mode carries baseline pixel rows (7/5/8)
verbatim, in input order, with tc_isFake copied from the input matching. So the
pixel slice's counts, kinematics, tc_type, tc_nhitOT and tc_isFake MUST be
identical between proto and base -- any difference there is a bug and is flagged
loudly. The ONE quantity that can legitimately differ is tc_isDuplicate: the
duplicate flag is recomputed over the MERGED TC set (pixel rows + chain TCs), so
a pixel TC gains the dup flag when a chain TC now shares its sim (and can lose
it if the baseline T5/T4 that made it a dup has no chain counterpart). The
per-slice DR deltas for 7/5/8 in the table are therefore real, but they measure
chain<->pixel overlap, not a pixel-side regression.

Usage:
  python3 compare_types.py --proto ab_X.root [--base <300evt ntuple>] [--json out.json]
"""

import argparse
import json
import sys

import awkward as ak
import numpy as np
import uproot

DEFAULT_BASE = ("/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/"
                "standalone/LSTNtuple_PU200RelVal_300evt.root")

TC_BRANCHES = ["tc_pt", "tc_eta", "tc_phi", "tc_type", "tc_isFake", "tc_isDuplicate", "tc_nhitOT"]
SIM_BRANCHES = ["sim_pt", "sim_eta", "sim_q", "sim_vx", "sim_vy", "sim_vz", "sim_tcIdx"]

# (label, tuple-of-types or None for no type filter)
SLICES = [
    ("pT5 (7)", (7,)),
    ("pT3 (5)", (5,)),
    ("pLS (8)", (8,)),
    ("T5 (4)", (4,)),
    ("T4 (9)", (9,)),
    ("chain-slice (4+9)", (4, 9)),
    ("pixel-slice (7+5+8)", (7, 5, 8)),
    ("all", None),
]

# Plot-level TC cuts and standard sim acceptance (see efficiency/src/performance.cc:
# pt_cut 0.9, eta_cut 4.5, vtx_z_thresh 30, vtx_perp_thresh 2.5, q != 0 for the
# pdgid=0/q=0 "all" category).
TC_PT_CUT = 0.9
TC_ETA_CUT = 4.5
SIM_PT_CUT = 0.9
SIM_ETA_CUT = 4.5
SIM_VZ_CUT = 30.0
SIM_VXY_CUT = 2.5

SIM_SELS = [
    ("prompt vxy<2.5", 0.0, SIM_VXY_CUT, True),   # (label, vxy lo, vxy hi, is_standard)
    ("disp vxy[1,5)", 1.0, 5.0, False),
    ("disp vxy[5,30)", 5.0, 30.0, False),
]

WARNINGS = []


def warn(msg):
    WARNINGS.append(msg)
    print("WARNING: %s" % msg, file=sys.stderr)


def loud(lines):
    bar = "!" * 78
    print(bar)
    for ln in lines:
        print("!! %s" % ln)
    print(bar)


def load(path, tag):
    f = uproot.open(path)
    if "tree" not in [k.split(";")[0] for k in f.keys()]:
        sys.exit("ERROR: %s file '%s' has no 'tree' (corrupt/unclosed ROOT file?)" % (tag, path))
    t = f["tree"]
    avail = set(k.split(";")[0] for k in t.keys())
    need = TC_BRANCHES + SIM_BRANCHES
    missing = [b for b in need if b not in avail]
    if missing:
        sys.exit("ERROR: %s file '%s' missing branches: %s" % (tag, path, missing))
    extras = [b for b in ("tc_isChain",) if b in avail]
    arr = t.arrays(need + extras)
    return arr, ("tc_isChain" in extras)


def type_mask(tc_type, types):
    if types is None:
        return ak.ones_like(tc_type, dtype=bool)
    m = tc_type == types[0]
    for tp in types[1:]:
        m = m | (tc_type == tp)
    return m


def frac(num, den):
    return (num / den) if den else None


def compute(arr, nevents, use_ischain=False):
    """Per-slice metric dict for one file.

    use_ischain (M7 attach): when the file carries tc_isChain, the chain-slice /
    pixel-slice composites are defined by tc_isChain (1 / 0) instead of tc_type, so
    K8-attached chains (written as tc_type 7, tc_isChain 1) land in the chain slice
    and the (suppressed) kept-baseline rows in the pixel slice. Per-type slices stay
    tc_type-based: the "pT5 (7)" row then MIXES baseline pT5 rows and attached chains.
    """
    tc_cut = (arr["tc_pt"] > TC_PT_CUT) & (abs(arr["tc_eta"]) < TC_ETA_CUT)

    # sim -> type of best-matched TC (None where unmatched); safe on empty-TC events.
    matched = arr["sim_tcIdx"] >= 0
    ptype = ak.fill_none(arr["tc_type"][ak.mask(arr["sim_tcIdx"], matched)], -1)
    ich = arr["tc_isChain"] if use_ischain else None
    pich = ak.fill_none(ich[ak.mask(arr["sim_tcIdx"], matched)], -1) if use_ischain else None

    vxy = np.sqrt(arr["sim_vx"] ** 2 + arr["sim_vy"] ** 2)
    sim_base = ((arr["sim_pt"] > SIM_PT_CUT) & (abs(arr["sim_eta"]) < SIM_ETA_CUT)
                & (arr["sim_q"] != 0) & (abs(arr["sim_vz"]) < SIM_VZ_CUT))
    sim_sels = {}
    for label, lo, hi, _ in SIM_SELS:
        sim_sels[label] = sim_base & (vxy >= lo) & (vxy < hi)

    out = {"denom": {label: int(ak.sum(sel)) for label, sel in sim_sels.items()}}
    for label, types in SLICES:
        if use_ischain and label.startswith("chain-slice"):
            m_type = ich == 1
        elif use_ischain and label.startswith("pixel-slice"):
            m_type = ich == 0
        else:
            m_type = type_mask(arr["tc_type"], types)
        m = tc_cut & m_type
        n = int(ak.sum(m))
        s = {
            "nTC": n,
            "nTC_per_event": n / nevents if nevents else None,
            "FR": frac(int(ak.sum(arr["tc_isFake"][m])), n),
            "DR": frac(int(ak.sum(arr["tc_isDuplicate"][m])), n),
            "mean_nhitOT": frac(float(ak.sum(arr["tc_nhitOT"][m])), n),
        }
        if use_ischain and label.startswith("chain-slice"):
            pm = pich == 1
        elif use_ischain and label.startswith("pixel-slice"):
            pm = pich == 0
        elif types is not None:
            pm = type_mask(ptype, types)
        else:
            pm = ptype >= 0
        for sel_label, sel in sim_sels.items():
            numer = int(ak.sum(sel & matched & pm))
            s["effN " + sel_label] = numer
            s["eff " + sel_label] = frac(numer, out["denom"][sel_label])
        out[label] = s
    return out


def pixel_identity_check(proto, base, proto_has_chain=False):
    """Pixel-slice (7/5/8, NO plot cuts) identity check; returns dict.

    Pre-attach files: kept rows must equal base rows elementwise (mode "verbatim").
    M7 attach files (tc_isChain present): kept rows = tc_isChain==0 rows, which are an
    ORDERED SUBSET of base pixel rows (K8 suppression drops the pT5/pLS rows of
    attached pLS). The check then verifies the subsequence embedding on exact equality
    of (type, pt, eta, phi, isFake, nhitOT) per event (mode "subset"); dup gained/lost
    is computed over the embedded pairs.
    """
    res = {"ok": True}
    pm = type_mask(proto["tc_type"], (7, 5, 8))
    if proto_has_chain:
        pm = pm & (proto["tc_isChain"] == 0)  # exclude K8-attached chains written as type 7
    bm = type_mask(base["tc_type"], (7, 5, 8))
    ncp = ak.num(proto["tc_pt"][pm])
    ncb = ak.num(base["tc_pt"][bm])
    res["n_pixel_proto"] = int(ak.sum(ncp))
    res["n_pixel_base"] = int(ak.sum(ncb))
    res["events_with_count_mismatch"] = int(ak.sum(ncp != ncb))
    grown = int(ak.sum(ncp > ncb))
    if grown:
        res["ok"] = False
        loud(["PIXEL-SLICE COUNT MISMATCH: %d event(s) have MORE kept pixel rows in" % grown,
              "proto than base. Kept rows are a (possibly suppressed) subset of base",
              "pixel rows -- growth is a bug. Elementwise check skipped."])
        return res

    subset_mode = res["events_with_count_mismatch"] > 0
    res["mode"] = "subset" if subset_mode else "verbatim"
    res["suppressed_rows"] = res["n_pixel_base"] - res["n_pixel_proto"]

    def flat(arr, mask, b):
        return ak.to_numpy(ak.flatten(arr[b][mask]))

    if not subset_mode:
        # Exact-copy fields: any difference at all is a bug.
        for b in ("tc_type", "tc_isFake", "tc_nhitOT"):
            nbad = int(np.sum(flat(proto, pm, b) != flat(base, bm, b)))
            res["mismatch_" + b] = nbad
            if nbad:
                res["ok"] = False
        for b in ("tc_pt", "tc_eta", "tc_phi"):
            d = np.abs(flat(proto, pm, b) - flat(base, bm, b))
            res["maxabsdiff_" + b] = float(d.max()) if d.size else 0.0
            if d.size and d.max() != 0.0:
                res["ok"] = False
        if not res["ok"]:
            loud(["PIXEL-SLICE CONTENT MISMATCH (see pixel-check block below):",
                  "kinematics / tc_type / tc_isFake / tc_nhitOT of pixel rows differ",
                  "between proto and base. These are copied verbatim by the hybrid",
                  "writer; ONLY tc_isDuplicate may legitimately change. BUG."])
        pd = flat(proto, pm, "tc_isDuplicate")
        bd = flat(base, bm, "tc_isDuplicate")
        res["pixel_dup_gained"] = int(np.sum((pd == 1) & (bd == 0)))
        res["pixel_dup_lost"] = int(np.sum((pd == 0) & (bd == 1)))
        return res

    # Subset mode: greedy two-pointer subsequence embedding per event on exact field
    # equality (fields are verbatim copies, so float comparison is exact).
    fields = ["tc_type", "tc_pt", "tc_eta", "tc_phi", "tc_isFake", "tc_nhitOT"]
    bad_events = 0
    dup_gained = dup_lost = 0
    nev = len(proto["tc_pt"])
    for ie in range(nev):
        pv = [np.asarray(proto[f][ie][pm[ie]]) for f in fields]
        bv = [np.asarray(base[f][ie][bm[ie]]) for f in fields]
        pdup = np.asarray(proto["tc_isDuplicate"][ie][pm[ie]])
        bdup = np.asarray(base["tc_isDuplicate"][ie][bm[ie]])
        nb = len(bv[0])
        j = 0
        ok_evt = True
        for k in range(len(pv[0])):
            while j < nb and not all(pv[f][k] == bv[f][j] for f in range(len(fields))):
                j += 1
            if j >= nb:
                ok_evt = False
                break
            if pdup[k] and not bdup[j]:
                dup_gained += 1
            elif bdup[j] and not pdup[k]:
                dup_lost += 1
            j += 1
        if not ok_evt:
            bad_events += 1
    res["subset_embed_failures"] = bad_events
    res["pixel_dup_gained"] = dup_gained
    res["pixel_dup_lost"] = dup_lost
    if bad_events:
        res["ok"] = False
        loud(["PIXEL-SLICE SUBSET MISMATCH: %d event(s) where the kept pixel rows are" % bad_events,
              "NOT an ordered subsequence of the base pixel rows (exact-field match).",
              "Kept rows must be base rows minus the K8-suppressed ones. BUG."])
    return res


def fmt(v, kind):
    if v is None:
        return "n/a"
    if kind == "rate":
        return "%.4f" % v
    if kind == "len":
        return "%.3f" % v
    if kind == "perevt":
        return "%.1f" % v
    return "%d" % v


def fmt_delta(v, kind):
    if v is None:
        return "n/a"
    if kind == "rate":
        return "%+.4f" % v
    if kind == "len":
        return "%+.3f" % v
    if kind == "perevt":
        return "%+.1f" % v
    return "%+d" % v


METRIC_ROWS = ([("nTC", "count"), ("nTC_per_event", "perevt"), ("FR", "rate"), ("DR", "rate"),
                ("mean_nhitOT", "len")]
               + [x for label, _, _, _ in SIM_SELS
                  for x in (("effN " + label, "count"), ("eff " + label, "rate"))])


def main():
    ap = argparse.ArgumentParser(description="Per-TC-type A/B comparison (proto vs baseline LST ntuple).")
    ap.add_argument("--proto", required=True, help="hybrid ab_*.root (impersonated LST ntuple)")
    ap.add_argument("--base", default=DEFAULT_BASE, help="baseline LST ntuple (default: 300evt PU200RelVal)")
    ap.add_argument("--json", default=None, help="also write all numbers as JSON")
    args = ap.parse_args()

    proto, proto_has_chain = load(args.proto, "proto")
    base, _ = load(args.base, "base")

    nev_p, nev_b = len(proto["tc_pt"]), len(base["tc_pt"])
    if nev_p != nev_b:
        warn("event count differs: proto %d vs base %d" % (nev_p, nev_b))
    nsim_mismatch = int(ak.sum(ak.num(proto["sim_pt"]) != ak.num(base["sim_pt"])))
    if nsim_mismatch:
        loud(["SIM BLOCK MISMATCH: %d event(s) have different sim counts." % nsim_mismatch,
              "The sim block is copied from the input -- eff comparison is suspect."])

    if proto_has_chain:
        # Coherence: isChain==1 rows must be types 4/9 (bare chains) or 7 (K8-attached
        # chains); isChain==0 rows must be kept baseline pixel types 7/5/8.
        bad_ch = int(ak.sum((proto["tc_isChain"] == 1) & ~type_mask(proto["tc_type"], (4, 9, 7))))
        bad_px = int(ak.sum((proto["tc_isChain"] == 0) & ~type_mask(proto["tc_type"], (7, 5, 8))))
        if bad_ch or bad_px:
            warn("proto: tc_isChain/tc_type skew: %d chain rows outside types 4/9/7, "
                 "%d baseline rows outside types 7/5/8" % (bad_ch, bad_px))
        n_att = int(ak.sum((proto["tc_isChain"] == 1) & type_mask(proto["tc_type"], (7,))))
        if n_att:
            print("NOTE: %d K8-attached chain TCs (tc_type 7, tc_isChain 1); chain-/pixel-slice"
                  " composites use tc_isChain, per-type slices stay tc_type-based" % n_att)
    else:
        warn("proto has no tc_isChain branch (older file); chain slice = types 4+9 by construction")

    pm = compute(proto, nev_p, use_ischain=proto_has_chain)
    bm = compute(base, nev_b)
    pix = pixel_identity_check(proto, base, proto_has_chain)

    print("Per-type A/B compare")
    print("  proto: %s  (%d events)" % (args.proto, nev_p))
    print("  base:  %s  (%d events)" % (args.base, nev_b))
    print("TC cuts: pt > %g, |eta| < %g. Sim acceptance: pt > %g, |eta| < %g, q != 0, |vz| < %g;"
          % (TC_PT_CUT, TC_ETA_CUT, SIM_PT_CUT, SIM_ETA_CUT, SIM_VZ_CUT))
    print("  prompt adds vxy < %g; displaced rows use vxy bands with the vxy cut dropped." % SIM_VXY_CUT)
    print("Sim denominators (proto | base): "
          + "  ".join("%s: %d | %d" % (label, pm["denom"][label], bm["denom"][label])
                      for label, _, _, _ in SIM_SELS))
    print()
    header = "%-21s %-22s %12s %12s %12s" % ("slice", "metric", "proto", "base", "delta")
    print(header)
    print("-" * len(header))
    table_json = {}
    for label, _ in SLICES:
        table_json[label] = {}
        for i, (mk, kind) in enumerate(METRIC_ROWS):
            pv, bv = pm[label][mk], bm[label][mk]
            delta = (pv - bv) if (pv is not None and bv is not None) else None
            print("%-21s %-22s %12s %12s %12s"
                  % (label if i == 0 else "", mk, fmt(pv, kind), fmt(bv, kind), fmt_delta(delta, kind)))
            table_json[label][mk] = {"proto": pv, "base": bv, "delta": delta}
        print("-" * len(header))

    print()
    print("Pixel-slice sanity check (types 7/5/8, no plot cuts, mode: %s):" % pix.get("mode", "verbatim"))
    print("  rows: proto %d, base %d; events with count mismatch: %d"
          % (pix["n_pixel_proto"], pix["n_pixel_base"], pix.get("events_with_count_mismatch", -1)))
    if pix.get("mode") == "subset":
        print("  K8-suppressed rows : %d (kept rows are base rows minus the attached pLS's"
              % pix.get("suppressed_rows", -1))
        print("                       pT5/pLS rows); subsequence-embedding failures: %d"
              % pix.get("subset_embed_failures", -1))
    if "mismatch_tc_type" in pix:
        print("  exact-copy fields  : mismatches type=%d isFake=%d nhitOT=%d"
              % (pix["mismatch_tc_type"], pix["mismatch_tc_isFake"], pix["mismatch_tc_nhitOT"]))
        print("  kinematics maxdiff : pt=%g eta=%g phi=%g"
              % (pix["maxabsdiff_tc_pt"], pix["maxabsdiff_tc_eta"], pix["maxabsdiff_tc_phi"]))
    if "pixel_dup_gained" in pix:
        print("  tc_isDuplicate     : %d pixel rows gained the dup flag, %d lost it (LEGITIMATE:"
              % (pix["pixel_dup_gained"], pix["pixel_dup_lost"]))
        print("                       dup flags are recomputed over the merged pixel+chain TC set,")
        print("                       so chain TCs sharing a sim with a pixel TC add dup flags;")
        print("                       counts/kinematics/tc_isFake CANNOT legitimately change.)")
    ok_msg = ("OK -- kept pixel rows are an exact subset of base (K8 suppression; only dup flags moved)"
              if pix.get("mode") == "subset"
              else "OK -- pixel slice carried verbatim (only dup flags moved)")
    print("  verdict: %s" % (ok_msg if pix["ok"] else "*** MISMATCH -- SEE FLAGS ABOVE ***"))

    if args.json:
        payload = {
            "proto_file": args.proto,
            "base_file": args.base,
            "nevents": {"proto": nev_p, "base": nev_b},
            "denominators": {"proto": pm["denom"], "base": bm["denom"]},
            "table": table_json,
            "pixel_check": pix,
            "warnings": WARNINGS,
        }
        with open(args.json, "w") as jf:
            json.dump(payload, jf, indent=2)
        print("wrote %s" % args.json)

    if WARNINGS:
        print("(%d warning(s) -- see stderr)" % len(WARNINGS))


if __name__ == "__main__":
    main()
