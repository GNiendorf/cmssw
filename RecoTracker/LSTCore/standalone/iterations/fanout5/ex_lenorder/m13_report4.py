#!/usr/bin/env python3
"""m13_report4.py -- MISSION 3 part 4: efficiency-relevant collateral only.

A killed TRUE chain TC costs harness efficiency only if (i) it is the ONLY in-cut TC
delivering its sim AND (ii) that sim is in the eff denominator (accepted sim, i.e. the
dump's simPt > 0, with pt > 0.9). Everything else is a dup-side removal.
"""
import numpy as np

SCRATCH = ("/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-"
           "RecoTracker-LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad")
S = np.load(f"{SCRATCH}/m13_shadow.npy")
F = S["isFake"] == 1
T = ~F
acc = T & (S["simPt"] > 0.9)                    # sim is in the harness eff denominator
uniq = S["uniqueDeliverer"] == 1
LOSS = acc & uniq                                # killing this loses a denominator sim
dispLOSS = LOSS & (S["simVxy"] >= 1.0)
vdispLOSS = LOSS & (S["simVxy"] >= 5.0)
NSIM = 63603 * 296 / 300.0
nTC, nFake = 565234, 34550
W, WV = ["02", "05", "10", "20"], [0.02, 0.05, 0.10, 0.20]

print(f"true chain TCs {int(T.sum())}: matched to an ACCEPTED sim (in eff denom) "
      f"{int(acc.sum())} ({acc.sum()/T.sum():.3f}); unique-deliverer {int((T&uniq).sum())}; "
      f"BOTH (= a real eff loss if killed) {int(LOSS.sum())} "
      f"[displaced vxy>=1: {int(dispLOSS.sum())}, vxy>=5: {int(vdispLOSS.sum())}]")
print(f"w7 measured band margins over LST baseline (300 evt): dxy[1,5) +15 sims/932, "
      f"dxy[5,10) +1 sim/285, vxy[5,10) +45 sims/634")
print()
print("=== K. EFFICIENCY-RELEVANT EXCHANGE RATE ===")
print(f"{'rule':>36} {'fakeKill':>9} {'simLoss':>8} {'dispLoss':>9} {'fakes/simLoss':>14} "
      f"{'FR':>7} {'dEff':>8}")


def row(nm, k):
    kF = int((k & F).sum()); kT = int((k & T).sum())
    kL = int((k & LOSS).sum()); kD = int((k & dispLOSS).sum())
    fr = (nFake - kF) / (nTC - kF - kT)
    print(f"{nm:>36} {kF:>9d} {kL:>8d} {kD:>9d} {kF/max(kL,1):>14.2f} {fr:>7.4f} "
          f"{-kL/NSIM:>8.4f}")


for w, wv in zip(W, WV):
    for tau in [0.0, 1.0]:
        row(f"A: shadow dR<{wv:.2f} & mX<{tau:.0f}", (S[f"sX{w}"] == 1) & (S["mX"] < tau))
for w, wv in zip(W, WV):
    row(f"A': shadow dR<{wv:.2f} gap4 & mX<0",
        (S[f"bX{w}"] > -90) & (S[f"bX{w}"] - S["mX"] > 4.0) & (S["mX"] < 0.0))
for tau in [-0.5, 0.0, 0.5, 1.0]:
    row(f"B: absolute mX<{tau:+.1f}", S["mX"] < tau)
for tau in [0.0, 1.0]:
    row(f"B+: absolute mX<{tau:+.1f} & mD<0", (S["mX"] < tau) & (S["mD"] < 0))
print()
print("=== L. TO REACH LST BASELINE FR 0.0455 (needs ~8800 fake removals) ===")
for nm, k in [("shadow dR<0.2 & mX<1", (S["sX20"] == 1) & (S["mX"] < 1.0)),
              ("absolute mX<+0.5", S["mX"] < 0.5)]:
    kF = int((k & F).sum()); kT = int((k & T).sum()); kL = int((k & LOSS).sum())
    kD = int((k & dispLOSS).sum())
    print(f"  {nm:>22}: fakes {kF}, FR {(nFake-kF)/(nTC-kF-kT):.4f}, sims lost {kL} "
          f"(displaced {kD}) -> dEff {-kL/NSIM:+.4f}")
