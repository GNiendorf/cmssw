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
  displaced: vxy in [0,1), [1,5), [5,10), [10,30), the vxy < 2.5 cut dropped.
Note: the pointed-at TC is NOT re-cut in pt/eta (mirrors the harness numerators).

M16 (attach as the DELIVERY path) -- the reason this tool grew a second half:
tc_type alone can no longer say who produced a TC. A type-7 row is either a
CARRIED baseline pT5 or a general-attach (pLS, chain) delivery; a type-5 row is
either a carried pT3 or a (pLS, bare-T3) delivery. The prototype writes the
provenance into tc_isChain as an OutDeliv code:
  0 = carried baseline pixel row   2 = attach delivery, type 7 (pT5-class)
  1 = bare chain TC (type 4/9)     3 = attach delivery, type 5 (pT3-class)
(0 and 1 are the legacy values, so pre-M16 files read identically; a file with
no tc_isChain branch has the code synthesized from tc_type.)

The DELIVERY-CLASS table slices on (tc_type, deliv) and, critically, on the
CLASS TOTAL -- "pT5-class ALL" = every type-7 row whatever produced it. That
class total against baseline's is the replacement A/B the plan-11 gate asks for:
"per-type pT5-class efficiency contribution must be >= baseline's". It is
reported per vxy stratum, not just prompt, because LST's pixel matching is
IP-blind at both layers while ours propagates the pLS's measured helix -- the
displaced-with-pixel-seed population is upside to claim, so a class that merely
ties on prompt but loses on vxy[1,5) has NOT passed.

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

# M16 OutDeliv codes (OutputWriter.h), carried in the tc_isChain branch.
D_CARRIED, D_CHAIN, D_ATT_T5, D_ATT_T3 = 0, 1, 2, 3

# (label, tuple-of-types or None for no type filter, tuple-of-deliv-codes or None)
SLICES = [
    ("pT5 (7)", (7,), None),
    ("pT3 (5)", (5,), None),
    ("pLS (8)", (8,), None),
    ("T5 (4)", (4,), None),
    ("T4 (9)", (9,), None),
    ("chain-slice (4+9)", (4, 9), None),
    ("pixel-slice (7+5+8)", (7, 5, 8), None),
    ("all", None, None),
]

# M16 DELIVERY CLASSES. The "* ALL" rows are the class totals the replacement A/B is
# judged on; the carried/attach rows decompose them so a loss can be attributed.
DELIV_SLICES = [
    ("pT5-class ALL (7)", (7,), None),
    ("  carried pT5", (7,), (D_CARRIED,)),
    ("  attach pT5", (7,), (D_ATT_T5,)),
    ("pT3-class ALL (5)", (5,), None),
    ("  carried pT3", (5,), (D_CARRIED,)),
    ("  attach pT3", (5,), (D_ATT_T3,)),
    ("pLS-class ALL (8)", (8,), None),
    ("bare chain (4+9)", (4, 9), (D_CHAIN,)),
    ("pixel-class ALL (7+5+8)", (7, 5, 8), None),
    ("attach deliveries", None, (D_ATT_T5, D_ATT_T3)),
    ("all", None, None),
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

# M16: the finer vxy ladder used for the delivery-class table -- the same bands the
# efficiency harness plots, so a per-class number here can be read against the
# aggregate eff-vs-vxy curve without re-binning.
SIM_SELS_M16 = [
    ("prompt vxy<2.5", 0.0, SIM_VXY_CUT),
    ("vxy[0,1)", 0.0, 1.0),
    ("vxy[1,5)", 1.0, 5.0),
    ("vxy[5,10)", 5.0, 10.0),
    ("vxy[10,30)", 10.0, 30.0),
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
    has = "tc_isChain" in extras
    if has:
        deliv = arr["tc_isChain"]
    else:
        # Pre-M16 file (or the baseline ntuple): the provenance is unambiguous from the
        # type -- pixel types are carried, 4/9 are the outer-tracker collection.
        deliv = ak.where(type_mask(arr["tc_type"], (7, 5, 8)),
                         ak.zeros_like(arr["tc_type"]), ak.ones_like(arr["tc_type"]))
    return arr, deliv, has


def type_mask(tc_type, types):
    if types is None:
        return ak.ones_like(tc_type, dtype=bool)
    m = tc_type == types[0]
    for tp in types[1:]:
        m = m | (tc_type == tp)
    return m


def slice_mask(tc_type, deliv, types, delivs):
    """Combined (tc_type in types) & (deliv in delivs); None = no constraint."""
    m = type_mask(tc_type, types)
    if delivs is not None:
        d = deliv == delivs[0]
        for dv in delivs[1:]:
            d = d | (deliv == dv)
        m = m & d
    return m


def frac(num, den):
    return (num / den) if den else None


def _sim_selections(arr, bands):
    vxy = np.sqrt(arr["sim_vx"] ** 2 + arr["sim_vy"] ** 2)
    sim_base = ((arr["sim_pt"] > SIM_PT_CUT) & (abs(arr["sim_eta"]) < SIM_ETA_CUT)
                & (arr["sim_q"] != 0) & (abs(arr["sim_vz"]) < SIM_VZ_CUT))
    return {label: sim_base & (vxy >= lo) & (vxy < hi) for label, lo, hi in bands}


def _slice_metrics(arr, deliv, nevents, slices, sim_sels, denom):
    """Shared per-slice metric builder: TC-level rates + efficiency contribution.

    A slice is (tc_type in types) & (deliv in delivs). The efficiency contribution
    counts accepted sims whose sim_tcIdx (best match > 0.75) points at a TC IN the
    slice -- the pointed-at TC is NOT re-cut in pt/eta, mirroring the harness
    numerators.
    """
    tc_cut = (arr["tc_pt"] > TC_PT_CUT) & (abs(arr["tc_eta"]) < TC_ETA_CUT)
    matched = arr["sim_tcIdx"] >= 0
    sel_idx = ak.mask(arr["sim_tcIdx"], matched)
    ptype = ak.fill_none(arr["tc_type"][sel_idx], -1)
    pdeliv = ak.fill_none(deliv[sel_idx], -1)

    out = {}
    for label, types, delivs in slices:
        m = tc_cut & slice_mask(arr["tc_type"], deliv, types, delivs)
        n = int(ak.sum(m))
        s = {
            "nTC": n,
            "nTC_per_event": n / nevents if nevents else None,
            "FR": frac(int(ak.sum(arr["tc_isFake"][m])), n),
            "DR": frac(int(ak.sum(arr["tc_isDuplicate"][m])), n),
            "mean_nhitOT": frac(float(ak.sum(arr["tc_nhitOT"][m])), n),
        }
        pm = slice_mask(ptype, pdeliv, types, delivs) & (ptype >= 0)
        for sel_label, sel in sim_sels.items():
            numer = int(ak.sum(sel & matched & pm))
            s["effN " + sel_label] = numer
            s["eff " + sel_label] = frac(numer, denom[sel_label])
        out[label] = s
    return out


def compute(arr, deliv, nevents):
    """Legacy per-tc_type table (unchanged slices/bands).

    The chain-slice / pixel-slice composites are defined by the DELIVERY code, not
    tc_type, so an attach-delivered type-7 TC lands in the chain slice and the carried
    rows in the pixel slice. Per-type slices stay tc_type-based: the "pT5 (7)" row
    therefore MIXES carried pT5 rows and attach deliveries -- which is exactly why the
    M16 delivery-class table below exists.
    """
    sim_sels = _sim_selections(arr, [(l, lo, hi) for l, lo, hi, _ in SIM_SELS])
    denom = {label: int(ak.sum(sel)) for label, sel in sim_sels.items()}
    slices = []
    for label, types, _ in SLICES:
        if label.startswith("chain-slice"):
            slices.append((label, None, (D_CHAIN, D_ATT_T5, D_ATT_T3)))
        elif label.startswith("pixel-slice"):
            slices.append((label, None, (D_CARRIED,)))
        else:
            slices.append((label, types, None))
    out = _slice_metrics(arr, deliv, nevents, slices, sim_sels, denom)
    out["denom"] = denom
    return out


def compute_m16(arr, deliv, nevents):
    """M16 delivery-class table on the finer vxy ladder."""
    sim_sels = _sim_selections(arr, SIM_SELS_M16)
    denom = {label: int(ak.sum(sel)) for label, sel in sim_sels.items()}
    out = _slice_metrics(arr, deliv, nevents, DELIV_SLICES, sim_sels, denom)
    out["denom"] = denom
    return out


def pixel_identity_check(proto, pdeliv, base):
    """CARRIED-row (deliv 0, NO plot cuts) identity check; returns dict.

    Pre-attach files: kept rows must equal base rows elementwise (mode "verbatim").
    Attach files: the carried rows are an ORDERED SUBSET of the base pixel rows (the
    contention/replacement suppression drops rows attach has taken over). The check
    then verifies the subsequence embedding on exact equality of (type, pt, eta, phi,
    isFake, nhitOT) per event (mode "subset"); dup gained/lost over the embedded pairs.
    Attach DELIVERIES (deliv 2/3) are excluded here by construction -- they are new
    objects, not carried rows, and are judged in the delivery-class table.
    """
    res = {"ok": True}
    pm = type_mask(proto["tc_type"], (7, 5, 8)) & (pdeliv == D_CARRIED)
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

M16_BANDS = [label for label, _, _ in SIM_SELS_M16]


def print_m16_table(pm16, bm16, nev_p):
    """The M16 delivery-class table: one row per class, one column per vxy band.

    Judged quantity = eff-CONTRIBUTION delta (proto - base) per class per band. A
    replacement passes when the CLASS TOTAL row is >= 0 in every band, not just prompt.
    """
    print()
    print("=" * 108)
    print("M16 DELIVERY-CLASS TABLE -- efficiency contribution by class and vxy stratum"
          " (proto - base), plus TC rates")
    print("  Class totals ('* ALL') are the replacement A/B verdict rows; the indented"
          " rows decompose them by provenance.")
    print("=" * 108)
    print("sim denominators (proto): "
          + "  ".join("%s: %d" % (b, pm16["denom"][b]) for b in M16_BANDS))
    hdr = "%-25s %9s %8s %8s | " % ("class", "nTC/evt", "FR", "DR") + \
        " ".join("%14s" % b for b in M16_BANDS)
    print(hdr)
    print("-" * len(hdr))
    for label, _, _ in DELIV_SLICES:
        p, b = pm16[label], bm16[label]
        cells = []
        for band in M16_BANDS:
            pv, bv = p["eff " + band], b["eff " + band]
            if pv is None or bv is None:
                cells.append("%14s" % "n/a")
            else:
                cells.append("%14s" % ("%.4f(%+.4f)" % (pv, pv - bv)))
        print("%-25s %9.1f %8s %8s | %s"
              % (label, p["nTC_per_event"] or 0.0, fmt(p["FR"], "rate"), fmt(p["DR"], "rate"),
                 " ".join(cells)))
    print("-" * len(hdr))
    print("baseline reference (same rows, base file):")
    for label, _, _ in DELIV_SLICES:
        b = bm16[label]
        print("%-25s %9.1f %8s %8s | %s"
              % (label, b["nTC_per_event"] or 0.0, fmt(b["FR"], "rate"), fmt(b["DR"], "rate"),
                 " ".join("%14s" % fmt(b["eff " + band], "rate") for band in M16_BANDS)))

    # Verdict lines: the plan-11 gate, per pixel class, per band.
    print()
    print("M16 REPLACEMENT VERDICT (class total must be >= baseline in EVERY band):")
    for label in ("pT5-class ALL (7)", "pT3-class ALL (5)", "pLS-class ALL (8)",
                  "pixel-class ALL (7+5+8)"):
        deltas = []
        worst_band, worst = None, None
        for band in M16_BANDS:
            pv, bv = pm16[label]["eff " + band], bm16[label]["eff " + band]
            if pv is None or bv is None:
                continue
            d = pv - bv
            deltas.append((band, d))
            if worst is None or d < worst:
                worst, worst_band = d, band
        ok = worst is not None and worst >= 0
        fr_p, fr_b = pm16[label]["FR"], bm16[label]["FR"]
        print("  %-24s %-4s  worst band %-12s %+.4f | FR %s vs %s"
              % (label, "PASS" if ok else "FAIL", worst_band or "-", worst if worst is not None else 0.0,
                 fmt(fr_p, "rate"), fmt(fr_b, "rate")))


def main():
    ap = argparse.ArgumentParser(description="Per-TC-type A/B comparison (proto vs baseline LST ntuple).")
    ap.add_argument("--proto", required=True, help="hybrid ab_*.root (impersonated LST ntuple)")
    ap.add_argument("--base", default=DEFAULT_BASE, help="baseline LST ntuple (default: 300evt PU200RelVal)")
    ap.add_argument("--json", default=None, help="also write all numbers as JSON")
    args = ap.parse_args()

    proto, proto_deliv, proto_has_chain = load(args.proto, "proto")
    base, base_deliv, _ = load(args.base, "base")

    nev_p, nev_b = len(proto["tc_pt"]), len(base["tc_pt"])
    if nev_p < nev_b:
        # Short prototype run (-n N): it processed entries 0..N-1 of THIS baseline file
        # in order, so truncating the base is an exact alignment, not an approximation.
        # Lets a scan judge itself on 20-30 events without a separate base file.
        base = base[:nev_p]
        base_deliv = base_deliv[:nev_p]
        nev_b = nev_p
        print("NOTE: base truncated to the prototype's first %d events (aligned by entry order)" % nev_p)
    elif nev_p != nev_b:
        warn("event count differs: proto %d vs base %d" % (nev_p, nev_b))
    nsim_mismatch = int(ak.sum(ak.num(proto["sim_pt"]) != ak.num(base["sim_pt"])))
    if nsim_mismatch:
        loud(["SIM BLOCK MISMATCH: %d event(s) have different sim counts." % nsim_mismatch,
              "The sim block is copied from the input -- eff comparison is suspect."])

    if proto_has_chain:
        # Coherence, per OutDeliv contract: deliv 0 -> carried pixel types 7/5/8;
        # deliv 1 -> bare chains 4/9 (or 7 for a legacy -A 1/2 in-place upgrade);
        # deliv 2 -> type 7; deliv 3 -> type 5.
        bad = 0
        bad += int(ak.sum((proto_deliv == D_CARRIED) & ~type_mask(proto["tc_type"], (7, 5, 8))))
        bad += int(ak.sum((proto_deliv == D_CHAIN) & ~type_mask(proto["tc_type"], (4, 9, 7))))
        bad += int(ak.sum((proto_deliv == D_ATT_T5) & ~type_mask(proto["tc_type"], (7,))))
        bad += int(ak.sum((proto_deliv == D_ATT_T3) & ~type_mask(proto["tc_type"], (5,))))
        if bad:
            warn("proto: %d rows violate the tc_isChain(OutDeliv)/tc_type contract" % bad)
        n_a5 = int(ak.sum(proto_deliv == D_ATT_T5))
        n_a3 = int(ak.sum(proto_deliv == D_ATT_T3))
        n_up = int(ak.sum((proto_deliv == D_CHAIN) & type_mask(proto["tc_type"], (7,))))
        if n_a5 or n_a3 or n_up:
            print("NOTE: M16 attach deliveries -- %d type-7 (deliv 2), %d type-5 (deliv 3);"
                  " %d legacy in-place upgrades (deliv 1, type 7)" % (n_a5, n_a3, n_up))
    else:
        warn("proto has no tc_isChain branch (older file); delivery code synthesized from tc_type")

    pm = compute(proto, proto_deliv, nev_p)
    bm = compute(base, base_deliv, nev_b)
    pix = pixel_identity_check(proto, proto_deliv, base)
    pm16 = compute_m16(proto, proto_deliv, nev_p)
    bm16 = compute_m16(base, base_deliv, nev_b)

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
    for label, _, _ in SLICES:
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

    print_m16_table(pm16, bm16, nev_p)

    if args.json:
        payload = {
            "proto_file": args.proto,
            "base_file": args.base,
            "nevents": {"proto": nev_p, "base": nev_b},
            "denominators": {"proto": pm["denom"], "base": bm["denom"]},
            "table": table_json,
            "m16_bands": M16_BANDS,
            "m16_denominators": {"proto": pm16["denom"], "base": bm16["denom"]},
            "m16_table": {label: {"proto": pm16[label], "base": bm16[label]}
                          for label, _, _ in DELIV_SLICES},
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
