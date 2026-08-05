#!/usr/bin/env python3
"""Follow-up adversarial checks:
  A. Are the CH+cpLS / cpLS+cpLS "zero shared OT hits" cells a tautology
     (i.e. do cpLS rows carry ANY OT hits at all)?
  B. Independent re-count of the decomp_t3 class/dup table from dc_t3_fcx1e.root.
  C. Re-derive the claimed aggregate numbers 1535 / 1496 / 6937 from the
     group-signature excess tables computed from scratch.
"""
from collections import Counter, defaultdict

import numpy as np
import uproot

S = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
PT, FR = 0.9, 0.75


def pcls(ty, isch):
    if isch == 1:
        return "CH"
    if isch in (2, 3):
        return "ATT"
    return {7: "cpT5", 5: "cpT3", 8: "cpLS"}.get(ty, "c?%d" % ty)


def lcls(ty):
    return {7: "pT5", 5: "pT3", 4: "T5", 9: "T4", 8: "pLS"}.get(ty, "?%d" % ty)


# ---- A: hit-list length by class in the diag file
a = uproot.open(f"{S}/fanout4/dupcut/dc_diag.root")["tree"].arrays(
    ["tc_type", "tc_isChain", "tc_hitOT", "tc_nhitOT", "tc_pt"], library="np")
nh_by_cls = defaultdict(Counter)
for i in range(len(a["tc_pt"])):
    ty, ich, ho = a["tc_type"][i], a["tc_isChain"][i], a["tc_hitOT"][i]
    for k in range(len(ty)):
        nh_by_cls[pcls(int(ty[k]), int(ich[k]))][len(ho[k])] += 1
print("[A] tc_hitOT LENGTH distribution per proto class (all pt):")
for c in sorted(nh_by_cls):
    d = nh_by_cls[c]
    print("    %-6s  %s" % (c, " ".join("len%d:%d" % (k, d[k]) for k in sorted(d))[:200]))


def excess_table(path, is_proto):
    t = uproot.open(path)["tree"]
    keys = set(t.keys())
    want = ["tc_pt", "tc_type", "tc_simIdxAll"] + (
        ["tc_isChain", "tc_isDuplicate"] if is_proto else ["tc_simIdxAllFrac"])
    want = [w for w in want if w in keys]
    a = t.arrays(want, library="np")
    tot = dup = 0
    clsall, clsdup = Counter(), Counter()
    sig_ex, sig_n = Counter(), Counter()
    for i in range(len(a["tc_pt"])):
        pt, ty, sia = a["tc_pt"][i], a["tc_type"][i], a["tc_simIdxAll"][i]
        n = len(pt)
        if is_proto:
            ich = a["tc_isChain"][i]
            cls = [pcls(int(ty[k]), int(ich[k])) for k in range(n)]
            fr = None
        else:
            cls = [lcls(int(ty[k])) for k in range(n)]
            fr = a["tc_simIdxAllFrac"][i]
        sim2 = defaultdict(list)
        for k in range(n):
            if fr is None:
                sl = [int(s) for s in sia[k]]
            else:
                sl = [int(s) for s, f in zip(sia[k], fr[k]) if f > FR]
            for s in sl:
                sim2[s].append(k)
        isd = np.zeros(n, dtype=bool)
        for s, rows in sim2.items():
            if len(rows) > 1:
                for r in rows:
                    isd[r] = True
                keep = [r for r in rows if pt[r] > PT]
                if len(keep) >= 2:
                    sig = "+".join(sorted(Counter(cls[r] for r in keep).elements()))
                    sig_n[sig] += 1
                    sig_ex[sig] += len(keep) - 1
        for k in range(n):
            if pt[k] > PT:
                tot += 1
                clsall[cls[k]] += 1
                if isd[k]:
                    dup += 1
                    clsdup[cls[k]] += 1
    return dict(tot=tot, dup=dup, clsall=clsall, clsdup=clsdup, sig_ex=sig_ex, sig_n=sig_n)


print("[B] independent re-count of dc_t3_fcx1e.root")
T3 = excess_table(f"{S}/fanout4/dupcut/dc_t3_fcx1e.root", True)
print("    TCs(pt>0.9)=%d dup=%d rate=%.4f groups=%d"
      % (T3["tot"], T3["dup"], T3["dup"] / T3["tot"], sum(T3["sig_n"].values())))
for c in sorted(T3["clsall"], key=lambda x: -T3["clsall"][x]):
    print("    %-6s nTC=%8d dup=%7d" % (c, T3["clsall"][c], T3["clsdup"][c]))
for sig, ex in sorted(T3["sig_ex"].items(), key=lambda x: -x[1])[:6]:
    print("    %-24s groups=%6d excess=%6d" % (sig, T3["sig_n"][sig], ex))

print("[C] aggregate-claim re-derivation from scratch")
P = excess_table(f"{S}/fanout4/dupcut/dc_diag.root", True)
L = excess_table(f"{S}/LSTNtuple_PU200RelVal_300evt.root", False)


def bucket(sig, proto):
    parts = sig.split("+")
    pls = "cpLS" if proto else "pLS"
    has_pls = any(p == pls for p in parts)
    has_ot = any(p != pls for p in parts)
    if has_pls and has_ot:
        return "MIXED"
    if has_pls:
        return "PLSONLY"
    return "OTONLY"


for tag, R, isp in (("proto", P, True), ("LST", L, False)):
    agg = Counter()
    for sig, ex in R["sig_ex"].items():
        agg[bucket(sig, isp)] += ex
    print("    %-5s excess: OT-OT-only=%d  MIXED(OT+pLS)=%d  pLS-only=%d  TOTAL=%d"
          % (tag, agg["OTONLY"], agg["MIXED"], agg["PLSONLY"], sum(agg.values())))
pa = Counter()
la = Counter()
for sig, ex in P["sig_ex"].items():
    pa[bucket(sig, True)] += ex
for sig, ex in L["sig_ex"].items():
    la[bucket(sig, False)] += ex
print("    DELTA proto-LST: OT-OT-only=%+d  MIXED=%+d  pLS-only=%+d  TOTAL=%+d"
      % (pa["OTONLY"] - la["OTONLY"], pa["MIXED"] - la["MIXED"],
         pa["PLSONLY"] - la["PLSONLY"], sum(pa.values()) - sum(la.values())))
