#!/usr/bin/env python3
"""m13_pools.py - WHICH M10 pools did M12 harvest, and what is left in each?

READ-ONLY. Runs the IDENTICAL stage decomposition at both anchors on the same frozen
chain dump (welding + v3 edge weights are identical between them; only K9 acceptance
changed), so pool deltas are exact rather than inferred:

  m8_h4b : -G 2  gate = chain_mlp_v3 (16-feat, 2-class), nLayers<=4 scored BY the gate
           and cut at -T4 2; 5+ keep the legacy sum-logit and are cut at -T5/-T6 0.
           Self-check: theta-pass must equal the binary's 1001626.
  m12_w7 : -G 6 three-class gate, thresholds per m13_gate.py.
           Self-check: theta-pass must equal 839508.  (both verified below)
"""
import json

import numpy as np
import torch
import uproot

SA = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
SC = ("/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-"
      "LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad")

C = np.load(f"{SC}/m13_chains_w7.npz")
W = np.load(f"{SC}/m13_sims_w7.npy")
H = np.load(f"{SC}/m10_sims.npy")
F = np.load(f"{SC}/m10_funnel2.npy")

# ---------- h4b gate (16-feature 2-class chain_mlp_v3) on the same dump ----------
f = uproot.open(f"{SA}/prototype/chains_m12_300evt.root")
tree = f["chains"]
norm = json.load(open(f"{SA}/prototype/chain_norm_v3.json"))
names = norm["feature_names"]
X = np.empty((tree.num_entries, 16), dtype=np.float64)
for i in range(16):
    X[:, i] = tree[f"cf_{i:02d}"].array(library="np")
for c in norm["conditioning"]:
    j = names.index(c["feature"])
    if c["op"] == "log10_1p":
        X[:, j] = np.log10(1.0 + X[:, j])
    else:
        np.clip(X[:, j], c["lo"], c["hi"], out=X[:, j])
X = (X - np.array(norm["mean"])) / np.array(norm["std"])
sd = torch.load(f"{SA}/prototype/chain_mlp_v3.pt", map_location="cpu", weights_only=False)


def arr(t):
    return np.array(t.tolist(), dtype=np.float64)


st = sd["state_dict"] if "state_dict" in sd else sd
kk = list(st.keys())
h = np.maximum(X @ arr(st[kk[0]]).T + arr(st[kk[1]]), 0)
h = np.maximum(h @ arr(st[kk[2]]).T + arr(st[kk[3]]), 0)
gl = (h @ arr(st[kk[4]]).T + arr(st[kk[5]])).ravel()

nL, score = C["nLayers"], C["score"]
h4bScore = np.where(nL <= 4, gl, score)                  # -G 2 rewrites score for T4-class
h4bThr = np.where(nL <= 4, 2.0, 0.0)                     # -T4 2 -T5 0 -T6 0
h4bPass = h4bScore >= h4bThr
print(f"h4b theta-pass replica = {int(h4bPass.sum())}  (binary log 1001626)  "
      f"delta={int(h4bPass.sum())-1001626}")
print(f"w7  theta-pass replica = {int(C['thetaPass'].sum())}  (binary log 839508)   "
      f"delta={int(C['thetaPass'].sum())-839508}")

# ---------- per-sim joins ----------
KEY = W["evt"].astype(np.int64) * 100000 + W["sim"].astype(np.int64)
OK = (np.abs(W["vz"]) < 30) & (W["q"] != 0)
ck = C["evt"].astype(np.int64) * 100000 + C["simIdx"].astype(np.int64)
cap = (C["label"] == 1) & (C["simIdx"] >= 0)


def count(mask):
    k = np.sort(ck[mask])
    return np.searchsorted(k, KEY, "right") - np.searchsorted(k, KEY)


nCap = count(cap)
nPassW7 = count(cap & C["thetaPass"])
nPassH4 = count(cap & h4bPass)

fk = F["evt"].astype(np.int64) * 100000 + F["sim"].astype(np.int64)
fo = np.argsort(fk); fks = fk[fo]
p = np.searchsorted(fks, KEY)
inF = (p < len(fks)) & (fks[np.clip(p, 0, len(fks) - 1)] == KEY)
fi = fo[np.clip(p, 0, len(fks) - 1)]
nPassEdge = np.where(inF, F["nPass"][fi], 0)
nWeld = np.where(inF, F["nWeld"][fi], 0)

hk = H["evt"].astype(np.int64) * 100000 + H["sim"].astype(np.int64)
ho = np.argsort(hk); hks = hk[ho]
p = np.searchsorted(hks, KEY)
inH = (p < len(hks)) & (hks[np.clip(p, 0, len(hks) - 1)] == KEY)
hi_ = ho[np.clip(p, 0, len(hks) - 1)]
h4bAny = np.where(inH, H["anyTC"][hi_], False)
h4bChain = np.where(inH, H["chainTC"][hi_], False)

ST = [("vxy<1", "vxy", 0, 1), ("vxy[1,5)", "vxy", 1, 5), ("vxy[5,10)", "vxy", 5, 10),
      ("vxy[10,30)", "vxy", 10, 30), ("dxy<1", "dxy", 0, 1), ("dxy[1,5)", "dxy", 1, 5),
      ("dxy[5,10)", "dxy", 5, 10), ("dxy[10,30)", "dxy", 10, 30)]
MASKS = [(nm, OK & (W[v] >= lo) & (W[v] < hi)) for nm, v, lo, hi in ST]

print("\n===== POOL-BY-POOL: h4b -> w7 (same 300 evts, same welded chains) =====")
print("  formation pools (identical by construction), then the two acceptance pools.")
hdr = f"{'band':12s}{'N':>6s}" + "".join(f"{x:>13s}" for x in
                                         ["form-lost", "purity", "gateKill h4b", "gateKill w7",
                                          "claim h4b", "claim w7", "chainTC h4b", "chainTC w7"])
print(hdr)
for nm, m in MASKS:
    form = m & (~inF | (nPassEdge == 0) | (nWeld == 0))
    pur = m & (nWeld > 0) & (nCap == 0)
    gkH = m & (nCap > 0) & (nPassH4 == 0)
    gkW = m & (nCap > 0) & (nPassW7 == 0)
    clH = m & (nPassH4 > 0) & ~h4bChain
    clW = m & (nPassW7 > 0) & ~W["chainTC"]
    print(f"{nm:12s}{int(m.sum()):6d}" +
          f"{int(form.sum()):13d}{int(pur.sum()):13d}{int(gkH.sum()):13d}{int(gkW.sum()):13d}"
          f"{int(clH.sum()):13d}{int(clW.sum()):13d}{int((m & h4bChain).sum()):13d}"
          f"{int((m & W['chainTC']).sum()):13d}")

print("\n===== the same pools restricted to sims with NO TC AT ALL (the real loss) =====")
lostW = ~W["anyTC"]
lostH = ~h4bAny
print(f"{'band':12s}{'lost h4b':>10s}{'lost w7':>9s}" +
      "".join(f"{x:>14s}" for x in ["gateKill h4b", "gateKill w7", "claim h4b", "claim w7"]))
for nm, m in MASKS:
    print(f"{nm:12s}{int((m & lostH).sum()):10d}{int((m & lostW).sum()):9d}"
          f"{int((m & lostH & (nCap > 0) & (nPassH4 == 0)).sum()):14d}"
          f"{int((m & lostW & (nCap > 0) & (nPassW7 == 0)).sum()):14d}"
          f"{int((m & lostH & (nPassH4 > 0)).sum()):14d}"
          f"{int((m & lostW & (nPassW7 > 0)).sum()):14d}")

print("\n===== CHAIN-LEVEL: what the w7 gate does to the harness-TRUE population =====")
lab = C["label"] == 1
for nm, m in [("all", np.ones(len(nL), bool)), ("nL<=4", nL <= 4), ("nL=5", nL == 5),
              ("nL>=6", nL >= 6), ("dca<0.5", C["dca"] < 0.5), ("dca>=0.5", C["dca"] >= 0.5)]:
    print(f"  {nm:9s} chains={int(m.sum()):8d} true={int((m & lab).sum()):8d} | "
          f"h4b keeps true={int((m & lab & h4bPass).sum()):8d} fake={int((m & ~lab & h4bPass).sum()):8d}"
          f" | w7 keeps true={int((m & lab & C['thetaPass']).sum()):8d} "
          f"fake={int((m & ~lab & C['thetaPass']).sum()):8d}")

# displaced-true chain retention by the gate (the priority-1 axis)
sv = C["simVxy"]
print("\n  gate retention of harness-TRUE chains by sim displacement:")
for nm, m in [("prompt vxy<1", lab & (sv >= 0) & (sv < 1)), ("vxy[1,10)", lab & (sv >= 1) & (sv < 10)),
              ("vxy>=10", lab & (sv >= 10))]:
    n = int(m.sum())
    print(f"    {nm:14s} n={n:8d}  h4b keep={100*(m & h4bPass).sum()/n:5.1f}%  "
          f"w7 keep={100*(m & C['thetaPass']).sum()/n:5.1f}%")
