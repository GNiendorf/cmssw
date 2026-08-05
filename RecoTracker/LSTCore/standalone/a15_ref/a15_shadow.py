#!/usr/bin/env python3
"""a15_shadow.py -- offline shadow evaluation of OWNERSHIP-MAP dedup rules.

Reads a chainproto hybrid output written with PROTO_DUMP_TCHITS=1 and replays a
post-hoc ownership sweep over the DELIVERED TCs, exactly the shape the codebase
already uses (-CC / K9): one claimed-unit map, best-first order, a candidate that
finds >= K of its own units already claimed is dropped, otherwise it claims them.
No candidate-vs-candidate comparison, no proximity criterion.

It reports what such a rule would do to the HEADLINE numbers (dup, fake, nTC) and
what it would cost (sims that lose their last matcher = the efficiency risk), so a
rule can be chosen before spending a 25-minute A/B.

Units here are OT HIT ROWS (that is what the dump carries); the in-code -CCG 1
default is MD rows, which is strictly coarser. A hit-granularity shadow therefore
UNDER-estimates the sharing a MD-granularity rule would see.
"""
import argparse
import sys
from collections import defaultdict

import ROOT

sys.path.insert(0, "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/a15_ref")
from a15_dup import read_events, dup_flags, region_of  # noqa: E402

# Sweep order priority: higher goes first and therefore claims first.
ORDER_PRIO = {"cPT5": 60, "aPT5": 55, "chT5": 50, "cPT3": 45, "aPT3": 40, "chT4": 35,
              "cPLS": 10, "zPLS": 9, "PT5": 60, "T5": 50, "PT3": 45, "T4": 35, "PLS": 10}


def run_rule(events, victims, need, order_key=None, frac=None):
    """victims: classes the rule may DROP. need: shared-unit count that kills.
    frac: if set, kill instead when shared/own >= frac (the K9 claim-budget shape)."""
    n0 = d0 = f0c = 0
    n1 = d1 = f1c = 0
    simsLost = 0
    dropped_by_cls = defaultdict(int)
    for ev in events:
        fl0, s2t0 = dup_flags(ev.sims)
        for i in range(ev.n):
            if not ev.inc[i]:
                continue
            n0 += 1
            d0 += fl0[i]
            f0c += (len(ev.sims[i]) == 0)
        order = sorted(range(ev.n),
                       key=order_key or (lambda i: (-ORDER_PRIO.get(ev.cls[i], 0),
                                                    -ev.nhit[i], i)))
        claimed = set()
        keep = [True] * ev.n
        for i in order:
            u = ev.ot[i]
            if not u:
                continue  # bare seeds own no OT units; untouched by this rule
            if ev.cls[i] in victims:
                sh = len(u & claimed)
                kill = (sh / len(u) >= frac) if frac is not None else (sh >= need)
                if kill:
                    keep[i] = False
                    dropped_by_cls[ev.cls[i]] += 1
                    continue
            claimed |= u
        for s, tcs in s2t0.items():
            if all(not keep[i] for i in tcs):
                simsLost += 1
        fl1, _ = dup_flags(ev.sims, keep)
        for i in range(ev.n):
            if not keep[i] or not ev.inc[i]:
                continue
            n1 += 1
            d1 += fl1[i]
            f1c += (len(ev.sims[i]) == 0)
    return {"dup0": d0 / max(n0, 1), "dup1": d1 / max(n1, 1),
            "fake0": f0c / max(n0, 1), "fake1": f1c / max(n1, 1),
            "nTC0": n0, "nTC1": n1, "simsLost": simsLost,
            "dropped": dict(dropped_by_cls)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--file", required=True)
    ap.add_argument("--kind", default="proto")
    ap.add_argument("-n", type=int, default=-1)
    args = ap.parse_args()
    ROOT.gROOT.SetBatch(True)
    ROOT.gErrorIgnoreLevel = ROOT.kError
    events, have = read_events(args.file, args.kind, args.n)
    if not have:
        sys.exit("need the tc_hitOT branch (PROTO_DUMP_TCHITS=1)")
    nev = len(events)
    print("SHADOW OWNERSHIP SWEEP over %d events (units = OT hit rows)" % nev)
    print("  %-46s %8s %9s %8s %9s %8s %9s"
          % ("rule (victim classes / kill at >= K shared units)", "dup", "d(dup)",
             "fake", "d(fake)", "TCdrop", "simsLost"))
    allcls = set()
    for ev in events:
        allcls.update(ev.cls)
    ot_cls = sorted(c for c in allcls if c not in ("cPLS", "zPLS"))
    rules = []
    for k in (1, 2, 3):
        rules.append(("ALL OT classes %s, K=%d" % (",".join(ot_cls), k), set(ot_cls), k))
    for c in ot_cls:
        rules.append(("only %s, K=1" % c, {c}, 1))
        rules.append(("only %s, K=2" % c, {c}, 2))
    for f in (0.30, 0.40, 0.50, 0.60, 0.75):
        rules.append(("ALL OT classes, shared/own >= %.2f" % f, set(ot_cls), 0, f))
    for f in (0.40, 0.50, 0.60):
        rules.append(("only chT5+chT4, shared/own >= %.2f" % f, {"chT5", "chT4"}, 0, f))
    rules = [(r[0], r[1], r[2], r[3] if len(r) > 3 else None) for r in rules]
    for name, vic, k, f in rules:
        r = run_rule(events, vic, k, frac=f)
        print("  %-46s %8.5f %+9.5f %8.5f %+9.5f %8d %9d"
              % (name, r["dup1"], r["dup1"] - r["dup0"], r["fake1"],
                 r["fake1"] - r["fake0"], r["nTC0"] - r["nTC1"], r["simsLost"]))
        print("      dropped by class: %s" % r["dropped"])


if __name__ == "__main__":
    main()
