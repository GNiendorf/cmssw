#!/usr/bin/env python3
"""m13_funnel.py - the M10 funnel redone against the m12_w7 anchor (READ-ONLY).

Methodology = m10_report2.py (harness denominator pt>0.9 |eta|<4.5 |vz|<30 q!=0,
verified to reproduce the compare_ab.py band ratios exactly -- see m13_sims.py output),
with two upgrades made possible by the M12 artifacts:

  * FORMATION stages (S1 formable / S2 edge-theta / S3 weld) are read from the M10
    caches. They are config-INDEPENDENT: w7 changed only K9 acceptance; the edge net
    (v3), thetaEdge=0 and K6 welding are identical to the m8_h4b anchor the caches
    were built on.
  * ACCEPTANCE stages come from the frozen w7 chain dump (chains_m12_300evt.root,
    1.427M chains == the binary's funnel "in") plus the m13_gate.py replica of the
    -G 6 w7 gate (validated: theta-pass 839508 == binary log, exact).

New stage inserted between weld and acceptance: S4 PURITY -- does the welder produce a
chain that the HARNESS would 75%-match to this sim at all (dump label==1 & simIdx==s)?
That is exactly the M12 label-retarget rule, so it is the true acceptance-side ceiling.
"""
import numpy as np

SC = ("/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-"
      "LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad")

W = np.load(f"{SC}/m13_sims_w7.npy")       # per-sim outcome, w7
H = np.load(f"{SC}/m10_sims.npy")          # per-sim outcome, m8_h4b (M10 anchor)
F = np.load(f"{SC}/m10_funnel2.npy")       # formable sims: edge/weld funnel
D = np.load(f"{SC}/m10_deep.npy")          # all sims: not-formable decomposition
C = np.load(f"{SC}/m13_chains_w7.npz")     # all welded chains + w7 gate decision

OK = (np.abs(W["vz"]) < 30) & (W["q"] != 0)
KEY = W["evt"].astype(np.int64) * 100000 + W["sim"].astype(np.int64)


def keyof(a, ev="evt", si="sim"):
    return a[ev].astype(np.int64) * 100000 + a[si].astype(np.int64)


# ---- per-sim chain aggregates from the dump (TC-capable = harness would 75%-match) ----
ck = C["evt"].astype(np.int64) * 100000 + C["simIdx"].astype(np.int64)
cap = (C["label"] == 1) & (C["simIdx"] >= 0)
order = np.argsort(ck[cap], kind="stable")
ck_c = ck[cap][order]
pas_c = C["thetaPass"][cap][order]
nl_c = C["nLayers"][cap][order]
dca_c = C["dca"][cap][order]
mD_c, mX_c, mP_c = C["mD"][cap][order], C["mX"][cap][order], C["mP"][cap][order]
uk, ustart = np.unique(ck_c, return_index=True)
uend = np.append(ustart[1:], len(ck_c))


def agg(fn, default=0.0):
    return np.array([fn(s, e) for s, e in zip(ustart, uend)])


nCap_u = uend - ustart
nPass_u = agg(lambda s, e: pas_c[s:e].sum())
maxNL_u = agg(lambda s, e: nl_c[s:e].max())
minDca_u = agg(lambda s, e: dca_c[s:e].min())
bestmX_u = agg(lambda s, e: mX_c[s:e].max())
bestmD_u = agg(lambda s, e: mD_c[s:e].max())

pos = np.searchsorted(uk, KEY)
hit = (pos < len(uk)) & (uk[np.clip(pos, 0, len(uk) - 1)] == KEY)
pc = np.clip(pos, 0, len(uk) - 1)
nCap = np.where(hit, nCap_u[pc], 0)
nCapPass = np.where(hit, nPass_u[pc], 0)
capMaxNL = np.where(hit, maxNL_u[pc], 0)
capMinDca = np.where(hit, minDca_u[pc], -1.0)
capBestmX = np.where(hit, bestmX_u[pc], -99.0)
capBestmD = np.where(hit, bestmD_u[pc], -99.0)

# ---- formation stages from the M10 caches ----
fk = keyof(F)
fo = np.argsort(fk); fks = fk[fo]
p = np.searchsorted(fks, KEY)
inF = (p < len(fks)) & (fks[np.clip(p, 0, len(fks) - 1)] == KEY)
fi = fo[np.clip(p, 0, len(fks) - 1)]
nPassEdge = np.where(inF, F["nPass"][fi], 0)
nWeld = np.where(inF, F["nWeld"][fi], 0)
nTrueEdge = np.where(inF, F["nTrue"][fi], 0)
maxL = np.where(inF, F["maxL"][fi], -99.0)
max2L = np.where(inF, F["max2L"][fi], -99.0)

dk = keyof(D)
do = np.argsort(dk); dks = dk[do]
p = np.searchsorted(dks, KEY)
inD = (p < len(dks)) & (dks[np.clip(p, 0, len(dks) - 1)] == KEY)
di = do[np.clip(p, 0, len(dks) - 1)]
nMDmatch = np.where(inD, D["nMDmatch"][di], 0)
nT3match = np.where(inD, D["nT3match"][di], 0)

# ---- h4b outcome joined on the same key ----
hk = keyof(H)
ho = np.argsort(hk); hks = hk[ho]
p = np.searchsorted(hks, KEY)
inH = (p < len(hks)) & (hks[np.clip(p, 0, len(hks) - 1)] == KEY)
hi_ = ho[np.clip(p, 0, len(hks) - 1)]
h4bAny = np.where(inH, H["anyTC"][hi_], False)
h4bChain = np.where(inH, H["chainTC"][hi_], False)

ST = [("vxy<1", "vxy", 0, 1), ("vxy[1,5)", "vxy", 1, 5), ("vxy[5,10)", "vxy", 5, 10),
      ("vxy[10,30)", "vxy", 10, 30),
      ("dxy<1", "dxy", 0, 1), ("dxy[1,5)", "dxy", 1, 5), ("dxy[5,10)", "dxy", 5, 10),
      ("dxy[10,30)", "dxy", 10, 30)]
MASKS = [(nm, OK & (W[v] >= lo) & (W[v] < hi)) for nm, v, lo, hi in ST]

print("========== w7 STAGE FUNNEL ON THE HARNESS DENOMINATOR (300 evt) ==========")
print(f"{'':38s}" + "".join(f"{nm:>12s}" for nm, _ in MASKS))
ROWS = [
    ("N (harness denominator)", lambda m: m.sum()),
    ("  LST baseline any TC", lambda m: (m & W["baseTC"]).sum()),
    ("  w7 any TC", lambda m: (m & W["anyTC"]).sum()),
    ("  w7 chain TC", lambda m: (m & W["chainTC"]).sum()),
    ("  w7 pixel TC", lambda m: (m & W["pixTC"]).sum()),
    ("S1 formable (>=1 true T3-T3 edge)", lambda m: (m & inF).sum()),
    ("S2 >=1 true edge >= thetaEdge 0", lambda m: (m & (nPassEdge > 0)).sum()),
    ("S3 >=1 true edge welded", lambda m: (m & (nWeld > 0)).sum()),
    ("S4 >=1 welded chain 75%-pure", lambda m: (m & (nCap > 0)).sum()),
    ("S5 >=1 such chain passes w7 gate", lambda m: (m & (nCapPass > 0)).sum()),
    ("S6 chain TC in the w7 output", lambda m: (m & W["chainTC"]).sum()),
]
for lab, fn in ROWS:
    print(f"{lab:38s}" + "".join(f"{int(fn(m)):12d}" for _, m in MASKS))

print("\n---------- STAGE LOSS (sims lost at each step) ----------")
LOSS = [
    ("L0 not formable", lambda m: (m & ~inF).sum()),
    ("L1 all true edges < thetaEdge", lambda m: (m & inF & (nPassEdge == 0)).sum()),
    ("L2 passing true edges unwelded", lambda m: (m & (nPassEdge > 0) & (nWeld == 0)).sum()),
    ("L3 welded but NO 75%-pure chain", lambda m: (m & (nWeld > 0) & (nCap == 0)).sum()),
    ("L4 pure chain killed by the gate", lambda m: (m & (nCap > 0) & (nCapPass == 0)).sum()),
    ("L5 gate-passed, lost in pixdrop/claim", lambda m: (m & (nCapPass > 0) & ~W["chainTC"]).sum()),
]
for lab, fn in LOSS:
    print(f"{lab:38s}" + "".join(f"{int(fn(m)):12d}" for _, m in MASKS))

print("\n---------- WHERE THE SIMS WITH *NO TC AT ALL* DIE (w7) ----------")
lost = ~W["anyTC"]
LOST = [
    ("LOST sims (no TC of any kind)", lambda m: (m & lost).sum()),
    ("  not formable", lambda m: (m & lost & ~inF).sum()),
    ("  edge-theta (no true edge >= 0)", lambda m: (m & lost & inF & (nPassEdge == 0)).sum()),
    ("  welding (passing edges unwelded)", lambda m: (m & lost & (nPassEdge > 0) & (nWeld == 0)).sum()),
    ("  purity (no 75%-pure chain welded)", lambda m: (m & lost & (nWeld > 0) & (nCap == 0)).sum()),
    ("  acceptance: gate kill", lambda m: (m & lost & (nCap > 0) & (nCapPass == 0)).sum()),
    ("  acceptance: pixdrop/claim", lambda m: (m & lost & (nCapPass > 0)).sum()),
]
for lab, fn in LOST:
    print(f"{lab:38s}" + "".join(f"{int(fn(m)):12d}" for _, m in MASKS))
print(f"{'  -> acceptance-side recoverable (eff pts)':38s}" +
      "".join(f"{(m & lost & (nCap > 0)).sum()/max(m.sum(),1):12.4f}" for _, m in MASKS))

print("\n---------- NOT-FORMABLE DECOMPOSITION ----------")
NF = [("  0 MD matched", lambda m: (m & ~inF & (nMDmatch == 0)).sum()),
      ("  >=1 MD, 0 T3", lambda m: (m & ~inF & (nMDmatch > 0) & (nT3match == 0)).sum()),
      ("  exactly 1 T3 (need 2 to weld)", lambda m: (m & ~inF & (nT3match == 1)).sum()),
      ("  >=2 T3 but no E1/E2 edge", lambda m: (m & ~inF & (nT3match >= 2)).sum())]
for lab, fn in NF:
    print(f"{lab:38s}" + "".join(f"{int(fn(m)):12d}" for _, m in MASKS))

print("\n========== WHAT M12 HARVESTED vs m8_h4b (same 300 evts) ==========")
print(f"{'':38s}" + "".join(f"{nm:>12s}" for nm, _ in MASKS))
GAIN = [
    ("h4b any TC", lambda m: (m & h4bAny).sum()),
    ("w7  any TC", lambda m: (m & W["anyTC"]).sum()),
    ("  gained (w7 only)", lambda m: (m & W["anyTC"] & ~h4bAny).sum()),
    ("  lost   (h4b only)", lambda m: (m & ~W["anyTC"] & h4bAny).sum()),
    ("  gained via T4-class chain", lambda m: (m & W["anyTC"] & ~h4bAny & (W["chainType"] == 9)).sum()),
    ("  gained via T5-class chain", lambda m: (m & W["anyTC"] & ~h4bAny & (W["chainType"] == 4)).sum()),
    ("  gained, exempt branch (dca>=.5)", lambda m: (m & W["anyTC"] & ~h4bAny & (capMinDca >= 0.5)).sum()),
]
for lab, fn in GAIN:
    print(f"{lab:38s}" + "".join(f"{int(fn(m)):12d}" for _, m in MASKS))

print("\n========== REMAINING ACCEPTANCE-SIDE POOL (lost sims WITH a 75%-pure chain) ==========")
for nm, m in MASKS:
    pool = m & lost & (nCap > 0)
    if pool.sum() == 0:
        print(f"  {nm:12s} pool=0")
        continue
    gk = pool & (nCapPass == 0)
    cl = pool & (nCapPass > 0)
    print(f"  {nm:12s} pool={int(pool.sum()):5d}  gate-killed={int(gk.sum()):5d} "
          f"claim/pixdrop={int(cl.sum()):5d} | of gate-killed: T4-class-only="
          f"{int((gk & (capMaxNL <= 4)).sum()):4d} 5+={int((gk & (capMaxNL >= 5)).sum()):4d}"
          f" | exempt(dca>=.5)={int((gk & (capMinDca >= 0.5)).sum()):4d}"
          f" bestmX med={np.median(capBestmX[gk]) if gk.sum() else -99:6.2f}"
          f" bestmD med={np.median(capBestmD[gk]) if gk.sum() else -99:6.2f}")
