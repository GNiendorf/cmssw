#!/usr/bin/env python3
"""rd_pairs.py -- RECON A part 2: purity wall + lever ceilings for the |eta| 1.5-3 dup window.

(1) Family-level excess ledger per |eta| slab (proto vs LST), so the "chain-vs-chain"
    / "OT-vs-pLS" / "pLS-vs-pLS" split is exact instead of eyeballed from signatures.
(2) Purity wall of accepted chain-family pairs: same-sim fraction vs shared-OT-hit
    count, vs shared/min(nhits) ratio, vs band; plus the MD-completeness of the
    overlap and the real/fake composition of the collateral.
(3) Offline replica of the -DD/-DDF/-DDP/-DDK post-arbitration structural dedup,
    scanned over configurations that are NOT expressible as flags (eta gates), so the
    ceiling of an eta-gated dedup can be quoted next to the flag-expressible ones.
    Reports, for each config: dup rate, mean nhitOT b/t/e, sims that lose their last
    TC, and displaced sims (|dxy| 5-10) whose best TC is killed.

Usage: rd_pairs.py --proto rd_fl.root [--base <LSTNtuple>]
"""
import argparse
from collections import Counter, defaultdict

import numpy as np
import uproot

SA = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
BASE = f"{SA}/LSTNtuple_PU200RelVal_300evt.root"
PT_CUT = 0.9
FRAC = 0.75
CHAINISH = (1, 2, 3)   # tc_isChain delivery codes that are chain-derived


def slab(eta):
    a = abs(eta)
    if a < 1.1:
        return "barrel<1.1"
    if a < 1.5:
        return "1.1-1.5"
    if a < 2.0:
        return "1.5-2.0"
    if a < 2.5:
        return "2.0-2.5"
    if a < 3.0:
        return "2.5-3.0"
    return ">3.0"


SLABS = ["barrel<1.1", "1.1-1.5", "1.5-2.0", "2.0-2.5", "2.5-3.0", ">3.0"]


def pfam(ty, isch):
    """coarse family: OT-only track, pixel+OT track, or bare pixel seed"""
    if isch in CHAINISH:
        return "OT"          # chain (bare or attach-upgraded)
    if ty == 8:
        return "pLS"
    return "OT"              # carried pT5 / pT3 also carry OT hits


def bfam(ty):
    return "pLS" if ty == 8 else "OT"


def mdset(h):
    return set((min(h[i], h[i + 1]), max(h[i], h[i + 1])) for i in range(0, len(h) - 1, 2))


def load_proto(path):
    t = uproot.open(path)["tree"]
    return t.arrays(["tc_pt", "tc_eta", "tc_type", "tc_nhitOT", "tc_simIdxAll", "tc_isFake",
                     "tc_isChain", "tc_hitOT", "tc_isDuplicate", "sim_pt", "sim_eta",
                     "sim_pca_dxy", "sim_vx", "sim_vy", "sim_tcIdx"], library="np")


def load_base(path):
    t = uproot.open(path)["tree"]
    return t.arrays(["tc_pt", "tc_eta", "tc_type", "tc_nhitOT", "tc_simIdxAll",
                     "tc_simIdxAllFrac", "tc_isDuplicate"], library="np")


# ---------------------------------------------------------------- (1) family ledger
def family_ledger(a, is_proto):
    """excess TCs per (slab, family-pair)"""
    tab = defaultdict(Counter)
    n = len(a["tc_pt"])
    for i in range(n):
        pt, eta, ty = a["tc_pt"][i], a["tc_eta"][i], a["tc_type"][i]
        sia = a["tc_simIdxAll"][i]
        if is_proto:
            isch = a["tc_isChain"][i]
            fam = [pfam(int(ty[k]), int(isch[k])) for k in range(len(ty))]
            frac = None
        else:
            fam = [bfam(int(ty[k])) for k in range(len(ty))]
            frac = a["tc_simIdxAllFrac"][i]
        s2 = defaultdict(list)
        for k in range(len(ty)):
            sl = sia[k]
            if frac is not None:
                sl = [int(s) for s, f in zip(sl, frac[k]) if f > FRAC]
            else:
                sl = [int(s) for s in sl]
            for s in sl:
                s2[s].append(k)
        for s, rows in s2.items():
            kr = [r for r in rows if pt[r] > PT_CUT]
            if len(kr) < 2:
                continue
            key = "+".join(sorted(Counter(fam[r] for r in kr).elements()))
            tab[slab(np.mean([abs(eta[r]) for r in kr]))][key] += len(kr) - 1
    return tab


# ---------------------------------------------------------------- (2) purity wall
def purity_wall(a):
    pw = defaultdict(lambda: defaultdict(Counter))   # slab -> sh -> {same,diff,...}
    ratio_tab = defaultdict(Counter)                 # (slab, ratio bucket) -> {same,diff}
    md_tab = Counter()
    coll_kind = defaultdict(Counter)                 # slab -> kind of the different-sim pairs
    lay_same = defaultdict(Counter)                  # slab -> (minLay,maxLay) of same-sim pairs
    for i in range(len(a["tc_pt"])):
        pt, eta, nh = a["tc_pt"][i], a["tc_eta"][i], a["tc_nhitOT"][i]
        isch, hits, sia = a["tc_isChain"][i], a["tc_hitOT"][i], a["tc_simIdxAll"][i]
        isfk = a["tc_isFake"][i]
        rows = [k for k in range(len(pt)) if int(isch[k]) in CHAINISH and pt[k] > PT_CUT]
        hset = {k: set(hits[k]) for k in rows}
        sims = {k: set(int(s) for s in sia[k]) for k in rows}
        h2c = defaultdict(list)
        for k in rows:
            for h in hset[k]:
                h2c[h].append(k)
        cand = set()
        for h, ks in h2c.items():
            if len(ks) < 2:
                continue
            for x in range(len(ks)):
                for y in range(x + 1, len(ks)):
                    cand.add((min(ks[x], ks[y]), max(ks[x], ks[y])))
        for (x, y) in cand:
            sh = len(hset[x] & hset[y])
            same = bool(sims[x] & sims[y])
            sl = slab(0.5 * (abs(eta[x]) + abs(eta[y])))
            pw[sl][min(sh, 6)]["same" if same else "diff"] += 1
            r = sh / max(min(len(hset[x]), len(hset[y])), 1)
            rb = "<0.20" if r < 0.20 else ("0.20-0.30" if r < 0.30 else
                                           ("0.30-0.40" if r < 0.40 else
                                            ("0.40-0.50" if r < 0.50 else ">=0.50")))
            ratio_tab[(sl, rb)]["same" if same else "diff"] += 1
            md_tab[(min(sh, 6), len(mdset(hits[x]) & mdset(hits[y])) > 0)] += 1
            if not same:
                kind = ("real" if not isfk[x] else "fake") + "+" + ("real" if not isfk[y] else "fake")
                coll_kind[sl]["+".join(sorted(kind.split("+")))] += 1
            else:
                lay_same[sl][(min(nh[x], nh[y]) // 2, max(nh[x], nh[y]) // 2)] += 1
    return pw, ratio_tab, md_tab, coll_kind, lay_same


# ---------------------------------------------------------------- (3) -DD replica
def dd_sim(a, minShared, ddf, ddk, ddp, etaGate):
    """Offline replica of the post-arbitration structural dedup, plus the resulting
    dup rate / track length / eff-risk. etaGate = min |eta| for the pair to interact
    (0.0 = the flag-expressible global behaviour)."""
    tot = dup = 0
    olnum = defaultdict(float)
    olden = defaultdict(int)
    killed_tot = 0
    lost_sims = 0
    at_risk_d510 = 0
    at_risk_any = 0
    for i in range(len(a["tc_pt"])):
        pt, eta, nh = a["tc_pt"][i], a["tc_eta"][i], a["tc_nhitOT"][i]
        ty, isch = a["tc_type"][i], a["tc_isChain"][i]
        hits, sia = a["tc_hitOT"][i], a["tc_simIdxAll"][i]
        n = len(pt)
        chain = [k for k in range(n) if int(isch[k]) in CHAINISH]
        hset = {k: set(hits[k]) for k in chain}
        # DDP 1: carried pixel owners (pT5 / pT3 rows, which carry OT hits) are kept and
        # un-killable participants of the walk.
        kept = []
        if ddp:
            for k in range(n):
                if int(isch[k]) == 0 and int(ty[k]) in (5, 7) and len(hits[k]) > 0:
                    kept.append(k)
                    hset[k] = set(hits[k])
        order = sorted(chain, key=(lambda k: (-int(nh[k]), k)) if ddk == 1 else (lambda k: k))
        killed = set()
        # inverted index over KEPT chains: only kept entries that actually share a hit
        # can ever pass the >= minShared test, so the walk stays near-linear.
        kidx = defaultdict(list)
        for q in kept:                      # DDP seeds
            for h in hset[q]:
                kidx[h].append(q)
        for r in order:
            cnt = Counter()
            for h in hset[r]:
                for q in kidx[h]:
                    cnt[q] += 1
            drop = False
            for q, sh in cnt.items():
                if sh < minShared:
                    continue
                if etaGate > 0.0 and 0.5 * (abs(eta[r]) + abs(eta[q])) < etaGate:
                    continue
                if ddf > 0.0 and sh < ddf * max(min(len(hset[r]), len(hset[q])), 1):
                    continue
                drop = True
                break
            if drop:
                killed.add(r)
            else:
                kept.append(r)
                for h in hset[r]:
                    kidx[h].append(r)
        killed_tot += len(killed)
        surv = [k for k in range(n) if k not in killed]
        s2 = defaultdict(list)
        for k in surv:
            for s in sia[k]:
                s2[int(s)].append(k)
        flagged = set()
        for s, rows in s2.items():
            if len(rows) > 1:
                flagged.update(rows)
        for k in surv:
            if pt[k] > PT_CUT:
                tot += 1
                sl = "b" if abs(eta[k]) < 1.1 else ("t" if abs(eta[k]) < 1.7 else "e")
                olnum[sl] += nh[k]
                olden[sl] += 1
                if k in flagged:
                    dup += 1
        # sims orphaned by the kill
        allsims = set()
        for k in range(n):
            for s in sia[k]:
                allsims.add(int(s))
        lost_sims += len(allsims - set(s2.keys()))
        # displaced risk: accepted sims whose BEST tc was killed
        stc = a["sim_tcIdx"][i]
        dxy = a["sim_pca_dxy"][i]
        for ai in range(len(stc)):
            t = int(stc[ai])
            if t >= 0 and t in killed:
                at_risk_any += 1
                if 5.0 <= abs(dxy[ai]) < 10.0:
                    at_risk_d510 += 1
    return dict(tot=tot, dup=dup, rate=dup / max(tot, 1), killed=killed_tot,
                lost=lost_sims, risk=at_risk_any, risk_d510=at_risk_d510,
                nb=olnum["b"] / max(olden["b"], 1), nt=olnum["t"] / max(olden["t"], 1),
                ne=olnum["e"] / max(olden["e"], 1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--proto", required=True)
    ap.add_argument("--base", default=BASE)
    ap.add_argument("--skipbase", action="store_true")
    args = ap.parse_args()
    A = load_proto(args.proto)

    print("=" * 104)
    print("(1) FAMILY-PAIR EXCESS LEDGER per |eta| slab -- excess TCs (n_group-1), pt>0.9")
    print("=" * 104)
    PT = family_ledger(A, True)
    BT = family_ledger(load_base(args.base), False) if not args.skipbase else None
    keys = ["OT+OT", "OT+pLS", "pLS+pLS", "OT+OT+OT", "OT+OT+pLS", "OT+pLS+pLS", "pLS+pLS+pLS"]
    print("%-12s | %s" % ("slab", " ".join("%23s" % k for k in keys)))
    print("%-12s | %s" % ("", " ".join("%23s" % "proto / LST  = delta" for k in keys)))
    for sl in SLABS:
        cells = []
        for k in keys:
            p = PT[sl][k]
            b = BT[sl][k] if BT else 0
            cells.append("%6d /%6d =%+6d" % (p, b, p - b))
        print("%-12s | %s" % (sl, " ".join("%23s" % c for c in cells)))
    # window totals
    for lo, hi, lbl in ((1.5, 3.0, "WINDOW 1.5-3.0"), (1.5, 2.5, "WINDOW 1.5-2.5")):
        sls = [s for s in SLABS if s in ("1.5-2.0", "2.0-2.5")] + \
              (["2.5-3.0"] if hi > 2.5 else [])
        cells = []
        for k in keys:
            p = sum(PT[s][k] for s in sls)
            b = sum(BT[s][k] for s in sls) if BT else 0
            cells.append("%6d /%6d =%+6d" % (p, b, p - b))
        print("%-12s | %s" % (lbl, " ".join("%23s" % c for c in cells)))

    print()
    print("=" * 104)
    print("(2a) PURITY WALL -- accepted chain-family pairs sharing k OT hits: n / same-sim fraction")
    print("=" * 104)
    pw, ratio_tab, md_tab, coll_kind, lay_same = purity_wall(A)
    print("%-12s | %s" % ("slab", " ".join("%16s" % ("share=%d" % k) for k in range(1, 5))))
    for sl in SLABS:
        cells = []
        for k in range(1, 5):
            c = pw[sl][k]
            n = c["same"] + c["diff"]
            cells.append("%6d %9s" % (n, ("p=%.3f" % (c["same"] / n)) if n else "  -  "))
        print("%-12s | %s" % (sl, " ".join("%16s" % c for c in cells)))
    g = defaultdict(Counter)
    for sl in SLABS:
        for k, c in pw[sl].items():
            g[k].update(c)
    cells = []
    for k in range(1, 5):
        c = g[k]
        n = c["same"] + c["diff"]
        cells.append("%6d %9s" % (n, ("p=%.3f" % (c["same"] / n)) if n else "  -  "))
    print("%-12s | %s" % ("GLOBAL", " ".join("%16s" % c for c in cells)))

    print("\n(2b) MD-completeness of the overlap: does a k-hit overlap contain a whole MiniDoublet?")
    for k in sorted(set(kk for kk, _ in md_tab)):
        y, nn = md_tab[(k, True)], md_tab[(k, False)]
        print("   share=%d  n=%d  contains a complete MD: %d (%.3f)" % (k, y + nn, y, y / max(y + nn, 1)))

    print("\n(2c) purity vs shared/min(nOThits) ratio -- the -DDF proxy for an eta gate")
    rbs = ["<0.20", "0.20-0.30", "0.30-0.40", "0.40-0.50", ">=0.50"]
    print("%-12s | %s" % ("slab", " ".join("%16s" % r for r in rbs)))
    for sl in SLABS:
        cells = []
        for rb in rbs:
            c = ratio_tab[(sl, rb)]
            n = c["same"] + c["diff"]
            cells.append("%6d %9s" % (n, ("p=%.3f" % (c["same"] / n)) if n else "  -  "))
        print("%-12s | %s" % (sl, " ".join("%16s" % c for c in cells)))

    print("\n(2d) COLLATERAL composition (different-sim chain pairs sharing >=1 hit)")
    print("%-12s %10s %10s %10s %10s" % ("slab", "real+real", "fake+real", "fake+fake", "total"))
    for sl in SLABS:
        c = coll_kind[sl]
        t = sum(c.values())
        if t == 0:
            continue
        print("%-12s %10d %10d %10d %10d" % (sl, c["real+real"], c["fake+real"], c["fake+fake"], t))

    print("\n(2e) nLayers (min,max) of SAME-SIM chain pairs -- what a 'shorter loses' rule drops")
    for sl in ("1.5-2.0", "2.0-2.5", "barrel<1.1", "1.1-1.5"):
        c = lay_same[sl]
        t = sum(c.values())
        if t == 0:
            continue
        eq = sum(v for (a_, b_), v in c.items() if a_ == b_)
        print("   %-12s n=%5d  equal-length %d (%.3f)  top: %s"
              % (sl, t, eq, eq / t,
                 " ".join("%d/%d:%d" % (a_, b_, v) for (a_, b_), v in
                          sorted(c.items(), key=lambda x: -x[1])[:6])))

    print()
    print("=" * 104)
    print("(3) OFFLINE -DD REPLICA: dup / length / risk ceilings (base = flagship as measured here)")
    print("=" * 104)
    cfgs = [("baseline (no dedup)", 99, 0.0, 1, 0, 0.0),
            ("-DD 2 -DDK 1", 2, 0.0, 1, 0, 0.0),
            ("-DD 2 -DDK 0", 2, 0.0, 0, 0, 0.0),
            ("-DD 2 -DDF 0.25 -DDK 1", 2, 0.25, 1, 0, 0.0),
            ("-DD 2 -DDF 0.30 -DDK 1", 2, 0.30, 1, 0, 0.0),
            ("-DD 2 -DDF 0.40 -DDK 1", 2, 0.40, 1, 0, 0.0),
            ("-DD 2 -DDP 1 -DDK 1", 2, 0.0, 1, 1, 0.0),
            ("-DD 1 -DDK 1", 1, 0.0, 1, 0, 0.0),
            ("[hypo] -DD 2 |eta|>1.5", 2, 0.0, 1, 0, 1.5),
            ("[hypo] -DD 2 |eta|>1.7", 2, 0.0, 1, 0, 1.7),
            ("[hypo] -DD 2 |eta|>2.0", 2, 0.0, 1, 0, 2.0),
            ("[hypo] -DD 1 |eta|>1.5", 1, 0.0, 1, 0, 1.5),
            ("[hypo] -DD 2 |eta|>1.5 DDP1", 2, 0.0, 1, 1, 1.5)]
    print("%-30s %8s %8s %8s %7s %7s %7s %7s %7s %7s"
          % ("config", "nTC", "dup", "rate", "d(rate)", "killed", "lostSim", "nhb", "nht", "nhe"))
    base = None
    for lbl, n_, f_, k_, p_, e_ in cfgs:
        R = dd_sim(A, n_, f_, k_, p_, e_)
        if base is None:
            base = R
        print("%-30s %8d %8d %8.4f %+7.4f %7d %7d %7.3f %7.3f %7.3f  risk(any/d510)=%d/%d"
              % (lbl, R["tot"], R["dup"], R["rate"], R["rate"] - base["rate"], R["killed"],
                 R["lost"], R["nb"], R["nt"], R["ne"], R["risk"], R["risk_d510"]))


if __name__ == "__main__":
    main()
