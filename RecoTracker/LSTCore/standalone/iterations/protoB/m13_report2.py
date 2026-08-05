#!/usr/bin/env python3
"""m13_report2.py -- MISSION 3 part 2: ceilings, gap rules, class-aware exemptions,
jet-decile collateral, and the efficiency-cost arithmetic that decides the verdict."""
import numpy as np
import uproot

SA = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
SCRATCH = ("/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-"
           "RecoTracker-LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad")
W = ["02", "05", "10", "20"]
WV = [0.02, 0.05, 0.10, 0.20]

S = np.load(f"{SCRATCH}/m13_shadow.npy")
F = S["isFake"] == 1
T = ~F
disp = T & (S["simVxy"] >= 1.0)
uniq = S["uniqueDeliverer"] == 1

t = uproot.open(f"{SA}/prototype/ab_m12_w7.root")["tree"]
a = t.arrays(["tc_pt", "tc_eta", "tc_isFake", "evt"], library="np")
evts = set(int(e) for e in np.unique(S["evt"]))
nTC = nFake = 0
for i in range(len(a["evt"])):
    if int(a["evt"][i]) not in evts:
        continue
    pt = np.asarray(a["tc_pt"][i]); eta = np.asarray(a["tc_eta"][i])
    inc = (pt > 0.9) & (np.abs(eta) < 4.5)
    nTC += int(inc.sum()); nFake += int((np.asarray(a["tc_isFake"][i])[inc] == 1).sum())
NSIM = 63603 * len(evts) / 300.0   # harness eff denominator (accepted sims, scaled)
EFF0 = 0.8180

print(f"denominators: inCut TC {nTC}, fake {nFake} (FR {nFake/nTC:.4f}), "
      f"eff denom ~{NSIM:.0f} sims, w7 eff {EFF0:.4f}")
print()

print("=== A. ORACLE CEILING: kill EVERY shadowed fake, ZERO true collateral (unreachable) ===")
for w, wv in zip(W, WV):
    k = int((F & (S[f'sX{w}'] == 1)).sum())
    print(f"  dR<{wv:.2f}: {k:6d} fakes ({k/F.sum()*100:4.1f}% of chain fakes) -> "
          f"FR {(nFake-k)/(nTC-k):.4f}  [w7 {nFake/nTC:.4f}, LST base 0.0455]")
print()

print("=== B. 'CLEARLY STRONGER NEIGHBOUR' GAP RULE (nb mX - victim mX > gap) ===")
print(f"{'dR':>5} {'gap':>5} | {'fakeYield':>9} {'trueYield':>9} {'enrich':>7} | "
      f"{'uniqTrue%':>9} {'dispTrue%':>9}")
for w, wv in zip(W, WV):
    for gap in [0.0, 2.0, 4.0, 6.0]:
        hit = (S[f"bX{w}"] > -90) & (S[f"bX{w}"] - S["mX"] > gap)
        fy, ty = hit[F].mean(), hit[T].mean()
        print(f"{wv:>5.2f} {gap:>5.1f} | {fy:>9.4f} {ty:>9.4f} {fy/max(ty,1e-9):>7.2f} | "
              f"{hit[T & uniq].mean():>9.4f} {hit[disp].mean():>9.4f}")
print()

print("=== C. CLASS-AWARE RULE (maintainer form) vs the SAME rule without the tournament ===")
print("  A: kill iff shadowed(dR<w, gap g) AND mX < tau AND mD < 0   (displaced-margin exempt)")
print("  B: kill iff mX < tau' AND mD < 0                            (absolute only)")
print(f"{'dR':>5} {'gap':>4} {'tau':>5} | {'fakeKill':>8} {'uniqT':>6} {'dispU':>6} "
      f"{'FR':>7} {'dEff':>8} | {'B fake @ equal uniqT':>21}")
grid = np.linspace(-4, 6, 501)
mdneg = S["mD"] < 0
bU_abs = np.array([int((T & uniq & mdneg & (S["mX"] < g)).sum()) for g in grid])
bF_abs = np.array([int((F & mdneg & (S["mX"] < g)).sum()) for g in grid])
for w, wv in zip(W, WV):
    for gap in [0.0, 4.0]:
        hit = (S[f"bX{w}"] > -90) & (S[f"bX{w}"] - S["mX"] > gap)
        for tau in [0.0, 1.0, 2.0]:
            kill = hit & mdneg & (S["mX"] < tau)
            kF, kT = int((kill & F).sum()), int((kill & T).sum())
            kU = int((kill & T & uniq).sum())
            kD = int((kill & disp & uniq).sum())
            fr = (nFake - kF) / (nTC - kF - kT)
            j = max(int(np.searchsorted(bU_abs, kU, side="right") - 1), 0)
            print(f"{wv:>5.2f} {gap:>4.1f} {tau:>5.1f} | {kF:>8d} {kU:>6d} {kD:>6d} "
                  f"{fr:>7.4f} {-kU/NSIM:>8.4f} | {bF_abs[j]:>8d} (tau'={grid[j]:+.2f}, "
                  f"uniqT={bU_abs[j]})")
print()

print("=== D. JET SAFETY: where the kills land (dR<0.1, mX<1, no exemption) ===")
kill = (S["sX10"] == 1) & (S["mX"] < 1.0)
q = np.quantile(S["dens"], np.linspace(0, 1, 11))
print(f"{'densBin':>10} | {'nTC':>7} | {'fakeKill':>8} {'trueKill':>8} {'uniqTkill':>9} "
      f"{'dispTkill':>9} | {'killed trues per killed fake':>28}")
for d in range(10):
    lo, hi = q[d], q[d + 1]
    m = (S["dens"] >= lo) & ((S["dens"] <= hi) if d == 9 else (S["dens"] < hi))
    if not m.any():
        continue
    kF = int((m & kill & F).sum()); kT = int((m & kill & T).sum())
    kU = int((m & kill & T & uniq).sum()); kD = int((m & kill & disp).sum())
    print(f"{f'{lo:.0f}-{hi:.0f}':>10} | {int(m.sum()):>7d} | {kF:>8d} {kT:>8d} {kU:>9d} "
          f"{kD:>9d} | {kT/max(kF,1):>28.2f}")
print()

print("=== E. HIGH-DENSITY (jet-core proxy) SUB-SAMPLE: top density decile only ===")
top = S["dens"] >= q[9]
print(f"  n={int(top.sum())} TCs (fake {int((top&F).sum())}, true {int((top&T).sum())}, "
      f"disp-true {int((top&disp).sum())}, uniq-true {int((top&T&uniq).sum())})")
for w, wv in zip(W, WV):
    hit = S[f"sX{w}"] == 1
    print(f"  dR<{wv:.2f}: fakes shadowed {hit[top&F].mean():.3f} | trues {hit[top&T].mean():.3f}"
          f" | uniq-trues {hit[top&T&uniq].mean():.3f} | disp-trues {hit[top&disp].mean():.3f}"
          f" | enrichment {hit[top&F].mean()/max(hit[top&T].mean(),1e-9):.2f}")
print()

print("=== F. SANITY: is the shadow flag just a low-margin/high-density proxy? ===")
for w in ["10"]:
    hit = S[f"sX{w}"] == 1
    for nm, m in [("fakes", F), ("trues", T)]:
        c = np.corrcoef(np.vstack([hit[m].astype(float), S["mX"][m], S["dens"][m].astype(float)]))
        print(f"  {nm}: corr(shadow,mX)={c[0,1]:+.3f}  corr(shadow,dens)={c[0,2]:+.3f}  "
              f"corr(mX,dens)={c[1,2]:+.3f}")
# logistic-free check: within a fixed density bin, does shadow still separate fake/true?
print("  within-density-bin enrichment (fake shadow rate / true shadow rate), dR<0.1:")
for d in range(10):
    lo, hi = q[d], q[d + 1]
    m = (S["dens"] >= lo) & ((S["dens"] <= hi) if d == 9 else (S["dens"] < hi))
    if int((m & F).sum()) < 100 or int((m & T).sum()) < 100:
        continue
    fr_, tr_ = S["sX10"][m & F].mean(), S["sX10"][m & T].mean()
    print(f"    dens {lo:.0f}-{hi:.0f}: fake {fr_:.3f} true {tr_:.3f} ratio {fr_/max(tr_,1e-9):.2f}")
