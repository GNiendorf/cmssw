#!/usr/bin/env python3
"""m10_report2.py - the funnel on the EXACT harness efficiency denominator
(pt>0.9, |eta|<4.5, |vz|<30, sim_q != 0), verified to reproduce compare_ab.py's
eff_vxy bands to 4 decimals. READ-ONLY."""
import numpy as np
import uproot

SA = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
SC = "/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-RecoTracker-LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad"

S = np.load(f"{SC}/m10_sims.npy")
F = np.load(f"{SC}/m10_funnel2.npy")
D = np.load(f"{SC}/m10_deep.npy")
W = np.load(f"{SC}/m10_weldfail.npy")
E = np.load(f"{SC}/m10_evt2.npy")

# --- attach vz / q from the anchor output ---
ab = uproot.open(f"{SA}/prototype/ab_m8_h4b.root")["tree"]
a = ab.arrays(["sim_vz", "sim_q", "evt", "lumi"], library="np")
kk, vzv, qv = [], [], []
for i in range(len(a["evt"])):
    n = len(a["sim_vz"][i])
    base = (int(a["lumi"][i]) << 40) + (int(a["evt"][i]) << 16)
    kk.append(base + np.arange(n, dtype=np.int64))
    vzv.append(a["sim_vz"][i])
    qv.append(a["sim_q"][i])
kk = np.concatenate(kk); vzv = np.concatenate(vzv); qv = np.concatenate(qv)
oq = np.argsort(kk); kk, vzv, qv = kk[oq], vzv[oq], qv[oq]


def key(x):
    return (x["lumi"].astype(np.int64) << 40) + (x["evt"].astype(np.int64) << 16) + x["sim"].astype(np.int64)


def attach(x):
    p = np.searchsorted(kk, key(x))
    return vzv[p], qv[p]


for arr in (S, F, D):
    pass
Svz, Sq = attach(S)
Fvz, Fq = attach(F)
Dvz, Dq = attach(D)
Sok = (np.abs(Svz) < 30) & (Sq != 0)
Fok = (np.abs(Fvz) < 30) & (Fq != 0)
Dok = (np.abs(Dvz) < 30) & (Dq != 0)

# join TC outcome onto F and D
kS = key(S); o = np.argsort(kS); kSs = kS[o]
def jn(x, col):
    p = np.searchsorted(kSs, key(x))
    return S[col][o[np.clip(p, 0, len(kSs) - 1)]]
FchainTC, FanyTC, FpixTC = jn(F, "chainTC"), jn(F, "anyTC"), jn(F, "pixTC")
DchainTC, DanyTC, DpixTC = jn(D, "chainTC"), jn(D, "anyTC"), jn(D, "pixTC")

ST = [("prompt vxy<1", 0, 1), ("vxy[1,5)", 1, 5), ("vxy[5,10)", 5, 10), ("vxy[10,30)", 10, 30)]

print("=========== FUNNEL ON THE HARNESS DENOMINATOR (300 evts) ===========")
print(f"{'':40s}" + "".join(f"{n:>15s}" for n, _, _ in ST))
rows = []
for nm, lo, hi in ST:
    ms = Sok & (S["vxy"] >= lo) & (S["vxy"] < hi)
    mf = Fok & (F["vxy"] >= lo) & (F["vxy"] < hi)
    md = Dok & (D["vxy"] >= lo) & (D["vxy"] < hi)
    r = dict(
        N=int(ms.sum()),
        eff=int((ms & S["anyTC"]).sum()),
        base=int((ms & S["baseTC"]).sum()),
        chain=int((ms & S["chainTC"]).sum()),
        pix=int((ms & S["pixTC"]).sum()),
        form=int(mf.sum()),
        s2=int((mf & (F["nPass"] > 0)).sum()),
        s3=int((mf & (F["nWeld"] > 0)).sum()),
        s4=int((mf & (F["nAcc"] > 0)).sum()),
        s5=int((mf & FchainTC).sum()),
        noMD=int((md & (D["nMDmatch"] == 0)).sum()),
        mdNoT3=int((md & (D["nMDmatch"] > 0) & (D["nT3match"] == 0)).sum()),
        oneT3=int((md & (D["nT3match"] == 1)).sum()),
        t3NoEdge=int((md & (D["nT3match"] >= 2) & (D["nTrueEdge"] == 0)).sum()),
    )
    rows.append(r)
for lab, k in [("harness denominator N", "N"), ("  hybrid eff (any TC)", "eff"),
               ("  LST baseline eff (any TC)", "base"), ("  of which chain TC", "chain"),
               ("  of which pixel TC", "pix"),
               ("S1 formable (>=1 true T3-T3 edge)", "form"),
               ("S2 >=1 true edge passes theta=0", "s2"),
               ("S3 >=1 true edge welded", "s3"),
               ("S4 >=1 covering chain K9-accepted*", "s4"),
               ("S5 chain TC 75%-matched", "s5")]:
    print(f"{lab:40s}" + "".join(f"{r[k]:15d}" for r in rows))
print("\nNOT-FORMABLE decomposition (S0 -> S1):")
for lab, k in [("  0 MD matched at all", "noMD"), ("  >=1 MD but 0 T3", "mdNoT3"),
               ("  exactly 1 T3 (needs 2 to weld)", "oneT3"),
               ("  >=2 T3 but NO E1/E2 edge", "t3NoEdge")]:
    print(f"{lab:40s}" + "".join(f"{r[k]:15d}" for r in rows))

print("\n=========== WHERE THE *LOST* SIMS DIE (no TC at all in hybrid) ===========")
print(f"{'':40s}" + "".join(f"{n:>15s}" for n, _, _ in ST))
kF = key(F); mapF = {int(x): i for i, x in enumerate(kF)}
out = []
for nm, lo, hi in ST:
    md = Dok & (D["vxy"] >= lo) & (D["vxy"] < hi) & (~DanyTC)
    kd = key(D)[md]
    inF = np.array([int(x) in mapF for x in kd])
    idxs = np.array([mapF[int(x)] for x in kd[inF]], dtype=int) if inF.any() else np.array([], int)
    r = dict(lost=int(md.sum()), nf=int((~inF).sum()))
    if len(idxs):
        r["th"] = int((F["nPass"][idxs] == 0).sum())
        r["wl"] = int(((F["nPass"][idxs] > 0) & (F["nWeld"][idxs] == 0)).sum())
        r["k9"] = int(((F["nWeld"][idxs] > 0) & (F["nAcc"][idxs] == 0)).sum())
        r["tc"] = int((F["nAcc"][idxs] > 0).sum())
    else:
        r.update(th=0, wl=0, k9=0, tc=0)
    out.append(r)
for lab, k in [("LOST sims (no TC at all)", "lost"),
               ("  died: not formable", "nf"),
               ("  died: all true edges < theta=0", "th"),
               ("  died: welding (passing edges unused)", "wl"),
               ("  died: K9 (pixdrop / claim / theta)", "k9"),
               ("  died: TC 75%-match failure", "tc")]:
    print(f"{lab:40s}" + "".join(f"{r[k]:15d}" for r in out))
print(f"{'  -> as efficiency points recoverable':40s}" +
      "".join(f"{(r['th']+r['wl']+r['k9']+r['tc'])/max(q['N'],1):15.4f}" for r, q in zip(out, rows)))

print("\n=========== theta=-1 / -2 WHAT-IF (measured, full re-weld+K9) ===========")
print(f"{'stratum':14s}{'N':>7s}{'S4 th0':>8s}{'S4 th-1':>9s}{'S4 th-2':>9s}"
      f"{'dS4/N th-1':>12s}{'dS4/N th-2':>12s}")
for nm, lo, hi in ST:
    mf = Fok & (F["vxy"] >= lo) & (F["vxy"] < hi)
    N = int((Sok & (S["vxy"] >= lo) & (S["vxy"] < hi)).sum())
    a0 = int((mf & (F["nAcc"] > 0)).sum())
    a1 = int((mf & (F["nAcc_m1"] > 0)).sum())
    a2 = int((mf & (F["nAcc_m2"] > 0)).sum())
    print(f"{nm:14s}{N:7d}{a0:8d}{a1:9d}{a2:9d}{(a1-a0)/N:12.4f}{(a2-a0)/N:12.4f}")
print(f"  chains/evt {E['nChains'].mean():.0f} -> {E['nChains_m1'].mean():.0f} -> "
      f"{E['nChains_m2'].mean():.0f};  K9-accepted/evt {E['nAcc'].mean():.0f} -> "
      f"{E['nAcc_m1'].mean():.0f} -> {E['nAcc_m2'].mean():.0f}")

print("\n=========== S4->S5 CHAIN-PURITY FAILURE (the big post-formation lever) ===")
for nm, lo, hi in ST:
    md = Dok & (D["vxy"] >= lo) & (D["vxy"] < hi) & (D["nAcc"] > 0)
    lost = md & (~DchainTC) & (~DanyTC)      #真 loss: no TC at all
    lostAny = md & (~DchainTC)
    if lostAny.sum() == 0:
        continue
    p = D["bestPurAcc"][lostAny]
    q = D["bestPur"][lostAny]
    print(f"  {nm:12s} S4={int(md.sum()):5d}  chainTC-fail={int(lostAny.sum()):5d} "
          f"(of which NO TC at all = {int(lost.sum()):4d})")
    print(f"      accepted-chain MD purity: med={np.median(p):.2f} frac<0.75={(p < 0.75).mean():.3f}"
          f" | best welded-chain purity med={np.median(q):.2f} frac==1.0={(q >= 0.999).mean():.3f}")

print("\n=========== Q2 detail: true-edge logits of sims that die at theta=0 =====")
for nm, lo, hi in ST:
    mf = Fok & (F["vxy"] >= lo) & (F["vxy"] < hi) & (F["nPass"] == 0)
    n = int(mf.sum())
    if n == 0:
        continue
    ml = F["maxL"][mf]
    print(f"  {nm:12s} N={n:4d}  best true-edge logit med={np.median(ml):6.2f} "
          f"p10={np.percentile(ml,10):6.2f} p90={np.percentile(ml,90):6.2f} | "
          f">=-1 {100*(ml>=-1).mean():5.1f}%  >=-2 {100*(ml>=-2).mean():5.1f}%  "
          f">=-4 {100*(ml>=-4).mean():5.1f}%")

print("\n=========== Q3 detail: weld failure modes (all vxy>=1, harness denom) ====")
kW = (W["lumi"].astype(np.int64) << 40) + (W["evt"].astype(np.int64) << 16) + W["sim"].astype(np.int64)
pw = np.searchsorted(kk, kW)
wok = (np.abs(vzv[pw]) < 30) & (qv[pw] != 0)
Wc = W[wok]
print(f"  unwelded passing true edges (in-denominator displaced sims): {len(Wc)} "
      f"over {len(np.unique(kW[wok]))} sims")
if len(Wc):
    both = (Wc["outTaken"] == 1) & (Wc["inTaken"] == 1)
    oo = (Wc["outTaken"] == 1) & (Wc["inTaken"] == 0)
    ii = (Wc["outTaken"] == 0) & (Wc["inTaken"] == 1)
    nn = (Wc["outTaken"] == 0) & (Wc["inTaken"] == 0)
    print(f"    both endpoints welded elsewhere : {both.sum()}")
    print(f"    tail out-slot lost only         : {oo.sum()}")
    print(f"    head in-slot lost only          : {ii.sum()}")
    print(f"    both slots free (starvation)    : {nn.sum()}")
    comp = np.concatenate([Wc["outCompL"][Wc["outTaken"] == 1], Wc["inCompL"][Wc["inTaken"] == 1]])
    compT = np.concatenate([Wc["outCompTrue"][Wc["outTaken"] == 1], Wc["inCompTrue"][Wc["inTaken"] == 1]])
    print(f"    winning competitor logit med={np.median(comp):.2f} vs lost true-edge med="
          f"{np.median(Wc['logit']):.2f}; competitor true={compT.mean():.3f}")
