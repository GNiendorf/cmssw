#!/usr/bin/env python3
"""T4-band census: find every LST-delivered sim with 10<=|sim_pca_dxy|<30, pt>0.9,
|eta|<4.5, q!=0, split by delivering TC type, and resolve the member T3 rows.

Writes a JSON ledger keyed by (event, simIdx) for the binary-side trace to join on.
"""
import json
import sys
import ROOT

S = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
NEV = int(sys.argv[1]) if len(sys.argv) > 1 else 300
OUT = sys.argv[2] if len(sys.argv) > 2 else S + "/fanout4/t4trace/t4_targets.json"

f = ROOT.TFile.Open(S + "/LSTNtuple_PU200RelVal_300evt.root")
t = f.Get("tree")

want = [
    "sim_pt", "sim_eta", "sim_q", "sim_pca_dxy", "sim_pca_dz", "sim_tcIdx",
    "sim_vx", "sim_vy",
    "tc_type", "tc_t4Idx", "tc_t5Idx", "tc_isFake", "tc_pt", "tc_eta",
    "t4_t3_idx0", "t4_t3_idx1", "t4_pt", "t4_eta", "t4_isFake", "t4_matched_simIdx",
    "t5_t3Idx0", "t5_t3Idx1",
    "t3_lsIdx0", "t3_lsIdx1", "ls_mdIdx0", "ls_mdIdx1",
    "t3_pt", "t3_eta", "t3_fakeScore", "t3_promptScore", "t3_displacedScore",
    "t3_partOfPT5", "t3_partOfPT3",
    "run", "lumi", "evt",
]
t.SetBranchStatus("*", 0)
for b in want:
    t.SetBranchStatus(b, 1)

nev = min(NEV, t.GetEntries())
ledger = []
counts = {"t4": 0, "t5": 0, "other": 0, "fake_deliver": 0}
typehist = {}

for i in range(nev):
    t.GetEntry(i)
    sim_pt = list(t.sim_pt)
    sim_eta = list(t.sim_eta)
    sim_q = list(t.sim_q)
    sim_dxy = list(t.sim_pca_dxy)
    sim_tc = list(t.sim_tcIdx)
    sim_vx = list(t.sim_vx)
    sim_vy = list(t.sim_vy)
    tc_type = list(t.tc_type)
    tc_t4 = list(t.tc_t4Idx)
    tc_t5 = list(t.tc_t5Idx)
    tc_fake = list(t.tc_isFake)
    t4_a = list(t.t4_t3_idx0)
    t4_b = list(t.t4_t3_idx1)
    t5_a = list(t.t5_t3Idx0)
    t5_b = list(t.t5_t3Idx1)
    t3_ls0 = list(t.t3_lsIdx0)
    t3_ls1 = list(t.t3_lsIdx1)
    nT3 = len(t3_ls0)

    for s in range(len(sim_pt)):
        if sim_pt[s] <= 0.9 or abs(sim_eta[s]) >= 4.5 or sim_q[s] == 0:
            continue
        if not (10.0 <= abs(sim_dxy[s]) < 30.0):
            continue
        tcidx = sim_tc[s]
        if tcidx < 0:
            continue  # not delivered by LST
        ty = tc_type[tcidx]
        typehist[ty] = typehist.get(ty, 0) + 1
        rec = {
            "iev": i, "sim": s, "type": int(ty),
            "pt": round(sim_pt[s], 3), "eta": round(sim_eta[s], 3),
            "dxy": round(sim_dxy[s], 3),
            "vxy": round((sim_vx[s] ** 2 + sim_vy[s] ** 2) ** 0.5, 3),
            "tc": int(tcidx), "tcFake": int(tc_fake[tcidx]),
        }
        if ty == 9:
            counts["t4"] += 1
            r = tc_t4[tcidx]
            rec["objRow"] = int(r)
            if 0 <= r < len(t4_a):
                a, b = t4_a[r], t4_b[r]
                rec["t3a"], rec["t3b"] = int(a), int(b)
                rec["inNodes"] = int(0 <= a < nT3 and 0 <= b < nT3)
                if rec["inNodes"]:
                    # E2 relation test: inner.ls1 == outer.ls0 (either orientation)
                    rec["e2_ab"] = int(t3_ls1[a] == t3_ls0[b])
                    rec["e2_ba"] = int(t3_ls1[b] == t3_ls0[a])
                    rec["e1_ab"] = int(t3_ls1[a] == t3_ls1[b])  # placeholder, refined below
        elif ty == 4:
            counts["t5"] += 1
            r = tc_t5[tcidx]
            rec["objRow"] = int(r)
            if 0 <= r < len(t5_a):
                a, b = t5_a[r], t5_b[r]
                rec["t3a"], rec["t3b"] = int(a), int(b)
                rec["inNodes"] = int(0 <= a < nT3 and 0 <= b < nT3)
        else:
            counts["other"] += 1
        if tc_fake[tcidx]:
            counts["fake_deliver"] += 1
        ledger.append(rec)

    if (i + 1) % 50 == 0:
        print("  ... event %d  ledger=%d" % (i + 1, len(ledger)), flush=True)

print("events:", nev)
print("counts:", counts)
print("delivering tc_type histogram:", typehist)
nT4 = sum(1 for r in ledger if r["type"] == 9)
inN = sum(1 for r in ledger if r["type"] == 9 and r.get("inNodes"))
e2ab = sum(1 for r in ledger if r["type"] == 9 and r.get("e2_ab"))
e2ba = sum(1 for r in ledger if r["type"] == 9 and r.get("e2_ba"))
print("T4-delivered: %d ; both T3 rows in node range: %d ; E2(a->b): %d ; E2(b->a): %d"
      % (nT4, inN, e2ab, e2ba))
nT5 = sum(1 for r in ledger if r["type"] == 4)
inN5 = sum(1 for r in ledger if r["type"] == 4 and r.get("inNodes"))
print("T5-delivered: %d ; both T3 rows in node range: %d" % (nT5, inN5))

with open(OUT, "w") as fh:
    json.dump(ledger, fh)
print("wrote", OUT)
