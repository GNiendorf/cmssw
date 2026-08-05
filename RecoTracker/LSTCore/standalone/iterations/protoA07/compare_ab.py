#!/usr/bin/env python3
"""A/B judge for the chain-tracking prototype (plan 10.4/10.5).

Reads two createPerfNumDenHists outputs (--proto and --base) and prints a compact
metric table from the Root__TC_* histogram sets:
  - efficiency overall (ef pt numer/denom sums) and vs vxy / dxy bands,
  - fake and duplicate rate overall (fr/dr pt sums),
  - per-eta-region efficiency / FR / DR (barrel |eta|<1.1, transition 1.1-1.7,
    endcap >1.7) from the _eta histograms,
  - mean track length (tc_nhitOT) overall and per eta region from the ol_ set.

Every quantity is printed as proto, base, delta (proto - base). Optionally emits the
same numbers as JSON (--json out.json) for machine consumption by the tuning loop.

NOTE (plan 10.5): these aggregates are the fast judge for iteration triage; final
calls on any tuning decision use the full curves from lst_plot_performance.py
--compare, never a single number.
"""

import argparse
import json
import sys

import ROOT

WARNINGS = []


def warn(msg):
    WARNINGS.append(msg)
    print("WARNING: %s" % msg, file=sys.stderr)


def get_hist(tfile, name, tag):
    """Fetch a TH1 by key name; None (with a warning) if absent or not a TH1."""
    if tfile is None:
        return None
    h = tfile.Get(name)
    if not h or not h.InheritsFrom("TH1"):
        warn("%s: histogram '%s' missing" % (tag, name))
        return None
    return h


def sum_all(h):
    """Sum of bin contents including under/overflow."""
    if h is None:
        return None
    return h.Integral(0, h.GetNbinsX() + 1)


def sum_band(h, lo, hi):
    """Sum of bins whose |center| lies in [lo, hi); hi=None means unbounded above.

    Under/overflow excluded (band membership needs a bin center). abs() folds the
    signed dxy axis and is a no-op for one-sided quantities like vxy.
    """
    if h is None:
        return None
    total = 0.0
    ax = h.GetXaxis()
    for b in range(1, h.GetNbinsX() + 1):
        c = abs(ax.GetBinCenter(b))
        if c >= lo and (hi is None or c < hi):
            total += h.GetBinContent(b)
    return total


def ratio(num, den):
    if num is None or den is None or den <= 0:
        return None
    return num / den


VXY_BANDS = [(0.0, 1.0), (1.0, 5.0), (5.0, 10.0), (10.0, 30.0)]
ETA_REGIONS = [("barrel", 0.0, 1.1), ("transition", 1.1, 1.7), ("endcap", 1.7, None)]


def compute_metrics(path, tag):
    """Metric dict {name: value-or-None} for one createPerfNumDenHists file."""
    f = ROOT.TFile.Open(path)
    if not f or f.IsZombie():
        warn("%s: cannot open '%s'; all its metrics will be n/a" % (tag, path))
        f = None

    EF = "Root__TC_base_0_0_ef_"  # efficiency numer/denom set (pdgid 0, charge 0 = all)
    FR = "Root__TC_fr_"  # fake rate set (no pdgid/charge split)
    DR = "Root__TC_dr_"  # duplicate rate set
    OL = "Root__TC_ol_"  # OT track length profile set (numer = sum nhitOT, denom = nTC)

    m = {}

    # (a) overall efficiency; context counts alongside.
    ef_num_pt = get_hist(f, EF + "numer_pt", tag)
    ef_den_pt = get_hist(f, EF + "denom_pt", tag)
    m["n_sim_denom"] = sum_all(ef_den_pt)
    m["eff_overall"] = ratio(sum_all(ef_num_pt), sum_all(ef_den_pt))

    # (b)/(c) efficiency vs vxy and dxy in displacement bands.
    for var in ("vxy", "dxy"):
        hn = get_hist(f, EF + "numer_" + var, tag)
        hd = get_hist(f, EF + "denom_" + var, tag)
        for lo, hi in VXY_BANDS:
            key = "eff_%s_%g_%g" % (var, lo, hi)
            m[key] = ratio(sum_band(hn, lo, hi), sum_band(hd, lo, hi))

    # (d) overall fake / duplicate rate; TC count for context.
    fr_num_pt = get_hist(f, FR + "numer_pt", tag)
    fr_den_pt = get_hist(f, FR + "denom_pt", tag)
    dr_num_pt = get_hist(f, DR + "numer_pt", tag)
    dr_den_pt = get_hist(f, DR + "denom_pt", tag)
    m["n_tc"] = sum_all(fr_den_pt)
    m["fake_overall"] = ratio(sum_all(fr_num_pt), sum_all(fr_den_pt))
    m["dup_overall"] = ratio(sum_all(dr_num_pt), sum_all(dr_den_pt))

    # (e) per-eta-region efficiency / FR / DR from the _eta histograms.
    ef_num_eta = get_hist(f, EF + "numer_eta", tag)
    ef_den_eta = get_hist(f, EF + "denom_eta", tag)
    fr_num_eta = get_hist(f, FR + "numer_eta", tag)
    fr_den_eta = get_hist(f, FR + "denom_eta", tag)
    dr_num_eta = get_hist(f, DR + "numer_eta", tag)
    dr_den_eta = get_hist(f, DR + "denom_eta", tag)
    for region, lo, hi in ETA_REGIONS:
        m["eff_" + region] = ratio(sum_band(ef_num_eta, lo, hi), sum_band(ef_den_eta, lo, hi))
        m["fake_" + region] = ratio(sum_band(fr_num_eta, lo, hi), sum_band(fr_den_eta, lo, hi))
        m["dup_" + region] = ratio(sum_band(dr_num_eta, lo, hi), sum_band(dr_den_eta, lo, hi))

    # In-cut overall rates: the _pt histograms are N-minus-one in pt (they include
    # entries below the 0.9 GeV cut), so also report overall values from the _eta sums,
    # which have the pt cut applied.
    m["eff_overall_incut"] = ratio(sum_all(ef_num_eta), sum_all(ef_den_eta))
    m["fake_overall_incut"] = ratio(sum_all(fr_num_eta), sum_all(fr_den_eta))
    m["dup_overall_incut"] = ratio(sum_all(dr_num_eta), sum_all(dr_den_eta))

    # (f) track length: ol numer = per-eta-bin sum of tc_nhitOT, denom = TC count,
    # so sums give the mean tc_nhitOT (overall and per region).
    ol_num = get_hist(f, OL + "numer_eta", tag)
    ol_den = get_hist(f, OL + "denom_eta", tag)
    if ol_num is None or ol_den is None:
        warn("%s: ol_ (track length) set unusable; mean_nhitOT will be n/a" % tag)
    m["mean_nhitOT"] = ratio(sum_all(ol_num), sum_all(ol_den))
    for region, lo, hi in ETA_REGIONS:
        m["mean_nhitOT_" + region] = ratio(sum_band(ol_num, lo, hi), sum_band(ol_den, lo, hi))

    if f:
        f.Close()
    return m


# Display order: (key, label, format). Grouped by the 10.5 priority order:
# efficiency first (overall + displaced bands), then dup, then fake, then length.
TABLE = [
    ("eff_overall", "eff overall (pt sums)", "rate"),
    ("eff_overall_incut", "eff overall (pt>0.9)", "rate"),
    ("eff_vxy_0_1", "eff vxy [0,1)", "rate"),
    ("eff_vxy_1_5", "eff vxy [1,5)", "rate"),
    ("eff_vxy_5_10", "eff vxy [5,10)", "rate"),
    ("eff_vxy_10_30", "eff vxy [10,30)", "rate"),
    ("eff_dxy_0_1", "eff dxy [0,1)", "rate"),
    ("eff_dxy_1_5", "eff dxy [1,5)", "rate"),
    ("eff_dxy_5_10", "eff dxy [5,10)", "rate"),
    ("eff_dxy_10_30", "eff dxy [10,30)", "rate"),
    ("eff_barrel", "eff barrel |eta|<1.1", "rate"),
    ("eff_transition", "eff transition 1.1-1.7", "rate"),
    ("eff_endcap", "eff endcap >1.7", "rate"),
    ("dup_overall", "dup rate (pt sums)", "rate"),
    ("dup_overall_incut", "dup rate (pt>0.9)", "rate"),
    ("dup_barrel", "dup rate barrel", "rate"),
    ("dup_transition", "dup rate transition", "rate"),
    ("dup_endcap", "dup rate endcap", "rate"),
    ("fake_overall", "fake rate (pt sums)", "rate"),
    ("fake_overall_incut", "fake rate (pt>0.9)", "rate"),
    ("fake_barrel", "fake rate barrel", "rate"),
    ("fake_transition", "fake rate transition", "rate"),
    ("fake_endcap", "fake rate endcap", "rate"),
    ("mean_nhitOT", "mean nhitOT", "len"),
    ("mean_nhitOT_barrel", "mean nhitOT barrel", "len"),
    ("mean_nhitOT_transition", "mean nhitOT transition", "len"),
    ("mean_nhitOT_endcap", "mean nhitOT endcap", "len"),
    ("n_tc", "n TC (fr denom)", "count"),
    ("n_sim_denom", "n sim (ef denom)", "count"),
]


def fmt(value, kind):
    if value is None:
        return "n/a"
    if kind == "rate":
        return "%.4f" % value
    if kind == "len":
        return "%.3f" % value
    return "%.0f" % value


def fmt_delta(delta, kind):
    if delta is None:
        return "n/a"
    if kind == "rate":
        return "%+.4f" % delta
    if kind == "len":
        return "%+.3f" % delta
    return "%+.0f" % delta


def main():
    ap = argparse.ArgumentParser(description="A/B metric table from two createPerfNumDenHists files.")
    ap.add_argument("--proto", required=True, help="prototype hists.root")
    ap.add_argument("--base", required=True, help="baseline hists.root")
    ap.add_argument("--json", default=None, help="also write metrics as JSON to this path")
    args = ap.parse_args()

    ROOT.gROOT.SetBatch(True)
    ROOT.gErrorIgnoreLevel = ROOT.kWarning

    proto = compute_metrics(args.proto, "proto")
    base = compute_metrics(args.base, "base")

    print("A/B compare (Root__TC set): proto=%s base=%s" % (args.proto, args.base))
    header = "%-26s %12s %12s %12s" % ("metric", "proto", "base", "delta")
    print(header)
    print("-" * len(header))
    metrics_json = {}
    for key, label, kind in TABLE:
        pv = proto.get(key)
        bv = base.get(key)
        delta = (pv - bv) if (pv is not None and bv is not None) else None
        print("%-26s %12s %12s %12s" % (label, fmt(pv, kind), fmt(bv, kind), fmt_delta(delta, kind)))
        metrics_json[key] = {"proto": pv, "base": bv, "delta": delta}

    if args.json:
        payload = {
            "proto_file": args.proto,
            "base_file": args.base,
            "metrics": metrics_json,
            "warnings": WARNINGS,
        }
        with open(args.json, "w") as jf:
            json.dump(payload, jf, indent=2)
        print("wrote %s" % args.json)

    if WARNINGS:
        print("(%d warning(s) -- see stderr)" % len(WARNINGS))


if __name__ == "__main__":
    main()
