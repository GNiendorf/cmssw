#!/usr/bin/env python3
"""M13 mission-1: anatomy of the SURVIVING fake chain TCs at anchor m12_w7.

READ-ONLY. Inputs (all pre-existing except the two m13_ artifacts this script consumes,
which were produced by re-running the unmodified chainproto binary with the w7 flags and
PROTO_DCA_DUMP set -- verified to reproduce w7's funnel exactly, claim=170918):

  m13_mf_w7.root      : w7 output ntuple (impersonated LST ntuple, tc_isChain present)
  m13_mf_w7_dca.txt   : one line per ACCEPTED chain, in accepted[] order == chain-TC order
  chains_m12_300evt.root : the M12 chain dump (all 1.427M welded chains, 25 features,
                        harness label + label_old + matchFrac + dcaXY)
  chain3_mlp_m12.pt / chain3_norm_m12.json : the live -G 6 3-class gate

Join: accepted chain i of event e  <->  chain-TC i of event e (K10 contract, 0 dropped)
      accepted chain  <->  dump chain by (evt, nLayers, "%.5f" % dcaXY), ambiguities dropped.
"""
import json
import os
import sys
from collections import defaultdict

import numpy as np
import uproot

P = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype/"
PTCUT = 0.9
DCASPLIT = 0.5
M4, M5, M6, MD, M4D, MR = 1.4949901503891982, 0.8974147108711509, -0.6053881223017352, 1e9, -0.5, -0.8

# ---------------------------------------------------------------- output ntuple ------
o = uproot.open(P + "m13_mf_w7.root:tree")
od = o.arrays(["tc_pt", "tc_eta", "tc_phi", "tc_type", "tc_isFake", "tc_isDuplicate",
               "tc_nhitOT", "tc_isChain", "evt"], library="np")
nev = len(od["evt"])
evt_of_entry = np.array([int(x) for x in od["evt"]])
ch_pt, ch_eta, ch_fake, ch_dup, ch_nhit, ch_type, ch_evt = [], [], [], [], [], [], []
counts = []
for i in range(nev):
    m = np.asarray(od["tc_isChain"][i]) > 0
    counts.append(int(m.sum()))
    ch_pt.append(np.asarray(od["tc_pt"][i])[m])
    ch_eta.append(np.asarray(od["tc_eta"][i])[m])
    ch_fake.append(np.asarray(od["tc_isFake"][i])[m])
    ch_dup.append(np.asarray(od["tc_isDuplicate"][i])[m])
    ch_nhit.append(np.asarray(od["tc_nhitOT"][i])[m])
    ch_type.append(np.asarray(od["tc_type"][i])[m])
    ch_evt.append(np.full(int(m.sum()), evt_of_entry[i]))
A = {k: np.concatenate(v) for k, v in
     [("pt", ch_pt), ("eta", ch_eta), ("fake", ch_fake), ("dup", ch_dup),
      ("nhit", ch_nhit), ("type", ch_type), ("evt", ch_evt)]}
nAcc = len(A["pt"])
print("chain TCs in output: %d over %d events" % (nAcc, nev))

# ---------------------------------------------------------------- dca dump -----------
raw = np.loadtxt(P + "m13_mf_w7_dca.txt", comments="#")
assert len(raw) == nAcc, "dca dump rows %d != chain TCs %d" % (len(raw), nAcc)
A["nL_d"] = raw[:, 0].astype(int)
A["dca"] = raw[:, 1]
A["label_dcadump"] = raw[:, 2].astype(int)   # OLD (labelChains) rule
print("dca dump rows: %d  (nLayers 4/5/6+: %d/%d/%d)"
      % (len(raw), (A["nL_d"] <= 4).sum(), (A["nL_d"] == 5).sum(), (A["nL_d"] >= 6).sum()))
# consistency: T4-class chain TCs are type 9, T5-class type 4
print("  type-vs-nLayers check: nL<=4 & type!=9 : %d ; nL>=5 & type!=4 : %d"
      % (((A["nL_d"] <= 4) & (A["type"] != 9)).sum(), ((A["nL_d"] >= 5) & (A["type"] != 4)).sum()))

# ---------------------------------------------------------------- chain dump ---------
t = uproot.open(P + "chains_m12_300evt.root:chains")
CF = ["cf_%02d" % i for i in range(25)]
dd = t.arrays(["evt", "label", "label_old", "matchFrac", "simIdx", "simVxy", "simPt",
               "nLayers", "dcaXY"] + CF, library="np")
nD = len(dd["evt"])
print("dump chains: %d" % nD)

# 3-class margins for every dumped chain (chain3_parity.py recipe)
norm = json.load(open(P + "chain3_norm_m12.json"))
names = norm["feature_names"]
mu = np.asarray(norm["mean"], dtype=np.float32)
sd = np.asarray(norm["std"], dtype=np.float32)
spec = uproot.open(P + "chains_m12_300evt.root")["feature_spec"].member("fTitle")
cf_names = spec[3:].split(",")
X = np.empty((nD, len(names)), dtype=np.float32)
for j, nm in enumerate(names):
    base = nm[3:]
    X[:, j] = dd["dcaXY"] if base == "dcaXY" else dd["cf_%02d" % cf_names.index(base)]
for c in norm.get("conditioning") or []:
    if c["feature"] not in names:
        continue
    j = names.index(c["feature"])
    if c["op"] == "clip":
        np.clip(X[:, j], c["lo"], c["hi"], out=X[:, j])
    else:
        X[:, j] = np.log10(1.0 + X[:, j])
Xs = (X - mu) / sd
import torch
blob = torch.load(P + "chain3_mlp_m12.pt", map_location="cpu", weights_only=False)
sd_ = blob["state_dict"]
wk = [k for k in sd_ if k.endswith("weight")]
ws = [(np.array(sd_[k].tolist(), dtype=np.float32),
       np.array(sd_[k.replace("weight", "bias")].tolist(), dtype=np.float32)) for k in wk]
h = Xs.astype(np.float32)
for li, (w, b) in enumerate(ws):
    h = h @ w.T + b
    if li < len(ws) - 1:
        h = np.maximum(h, 0.0)
z = h.astype(np.float64)
print("arch %s layer shapes %s" % (blob["arch"], [tuple(w.shape) for w, _ in ws]))
mP = z[:, 1] - z[:, 0]
mDm = z[:, 2] - z[:, 0]
mX = np.maximum(z[:, 1], z[:, 2]) - z[:, 0]
print("margins computed; sample z[0] = %s" % np.round(z[0], 5))

# replicate the -G 6 theta stage on the dump (validation of the whole feature path)
nLd = dd["nLayers"].astype(int)
dcad = dd["dcaXY"].astype(np.float64)
legacy = dd["cf_02"].astype(np.float64) + 0.5 * nLd
killed = np.zeros(nD, dtype=bool)
exempt = np.zeros(nD, dtype=bool)
t4 = nLd <= 4
ex_t4 = t4 & (dcad >= max(DCASPLIT, 0.0))
killed |= ex_t4 & (mDm < M4D)
exempt |= ex_t4
ip_t4 = t4 & ~ex_t4
killed |= ip_t4 & (mX < M4)
ip5 = (~t4) & (dcad < DCASPLIT)
thr5 = np.where(nLd >= 6, M6, M5)
killed |= ip5 & (mP < thr5) & (mX < MR)
ex5 = (~t4) & (dcad >= DCASPLIT)
killed |= ex5 & (mDm < MD) & (mX < MR)
exempt |= ex5
# K9 base thresholds: kNoCutTheta (-1e5) for non-exempt, -U4/5/6 = 0 (legacy scale) for exempt
thetaPass = (~killed) & np.where(exempt, legacy >= 0.0, True)
print("replicated theta-pass: %d   (binary reported 839510)" % thetaPass.sum())

# ---------------------------------------------------------------- join ---------------
key_dump = defaultdict(list)
for i in range(nD):
    key_dump[(int(dd["evt"][i]), int(nLd[i]), "%.5f" % dcad[i])].append(i)
idx = np.full(nAcc, -1, dtype=np.int64)
idx2 = np.full(nAcc, -1, dtype=np.int64)   # ambiguity-tolerant fallback
namb = 0
disp_nn, disp_mx, grpsz = [], [], []
labels_dump = np.asarray(dd["label"]).astype(int)
for i in range(nAcc):
    k = (int(A["evt"][i]), int(A["nL_d"][i]), "%.5f" % A["dca"][i])
    v = key_dump.get(k)
    if v is None:
        continue
    if len(v) > 1:
        namb += 1
        # fallback: restrict to dump chains whose harness label agrees with tc_isFake
        # (label == 1 - tc_isFake is an identity, proven below), then take the first.
        want = 0 if A["fake"][i] else 1
        cand = [j for j in v if labels_dump[j] == want] or v
        idx2[i] = cand[0]
        grpsz.append(len(v))
        disp_nn.append(float(np.ptp(dd["cf_00"][cand])))
        disp_mx.append(float(np.ptp(mX[cand])))
        continue
    idx[i] = v[0]
    idx2[i] = v[0]
ok = idx >= 0
ok2 = idx2 >= 0
print("JOIN: unique-key matched %d / %d (%.4f); ambiguous keys %d; unmatched %d"
      % (ok.sum(), nAcc, ok.mean(), namb, (~ok2).sum()))
if grpsz:
    print("  ambiguous groups: mean size %.2f, within-group ptp(nNodes) mean %.4f max %.1f;"
          " ptp(mX) mean %.4f p95 %.4f"
          % (np.mean(grpsz), np.mean(disp_nn), np.max(disp_nn),
             np.mean(disp_mx), np.percentile(disp_mx, 95)))
print("  FR of unique-matched subset %.4f vs ambiguous subset %.4f (bias check)"
      % (A["fake"][ok].mean(), A["fake"][~ok & ok2].mean()))
idx = idx2
ok = ok2

for k in ["label", "label_old", "matchFrac", "simVxy", "simPt", "simIdx"]:
    A[k] = np.where(ok, np.asarray(dd[k])[np.clip(idx, 0, None)], np.nan if k != "simIdx" else -1)
A["nNodes"] = np.where(ok, dd["cf_00"][np.clip(idx, 0, None)], np.nan)
A["mP"] = np.where(ok, mP[np.clip(idx, 0, None)], np.nan)
A["mD"] = np.where(ok, mDm[np.clip(idx, 0, None)], np.nan)
A["mX"] = np.where(ok, mX[np.clip(idx, 0, None)], np.nan)
A["legacy"] = np.where(ok, legacy[np.clip(idx, 0, None)], np.nan)
A["thetaPass"] = np.where(ok, thetaPass[np.clip(idx, 0, None)], False)
for j in range(25):
    A["cf_%02d" % j] = np.where(ok, dd["cf_%02d" % j][np.clip(idx, 0, None)], np.nan)
A["dca_dump"] = np.where(ok, dcad[np.clip(idx, 0, None)], np.nan)

# ---- VALIDATIONS -------------------------------------------------------------------
print("\n=== JOIN VALIDATION ===")
v = ok
print("  |dca_dump - dca_txt| max                     : %.3e" % np.abs(A["dca_dump"][v] - A["dca"][v]).max())
print("  accepted chains failing the replicated theta : %d (expect 0)" % (v & ~A["thetaPass"]).sum())
lab = A["label"][v]
fk = A["fake"][v]
print("  harness label(dump) vs 1-tc_isFake(output)   : agree %d / %d (%.6f)"
      % (((lab == 1) == (fk == 0)).sum(), v.sum(), ((lab == 1) == (fk == 0)).mean()))
print("  matchFrac>0.75 vs tc_isFake==0               : agree %.6f" % ((A["matchFrac"][v] > 0.75) == (fk == 0)).mean())
print("  old label (dca-dump txt) vs label_old(dump)  : agree %.6f"
      % (A["label_dcadump"][v] == A["label_old"][v]).mean())

np.save(P + "m13_mf_join_idx.npy", idx)
import pickle
with open(P + "m13_mf_accepted.pkl", "wb") as fh:
    pickle.dump({k: np.asarray(vv) for k, vv in A.items()}, fh)
print("\nwrote m13_mf_accepted.pkl (%d accepted chain TCs, %d joined)" % (nAcc, ok.sum()))
