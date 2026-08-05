#!/usr/bin/env python3
"""m13_report3.py -- MISSION 3 part 3: overlap with an absolute kill, per-branch
enrichment (is the lever alive anywhere?), shell enrichment, and the exchange rate."""
import numpy as np

SCRATCH = ("/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-"
           "RecoTracker-LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad")
S = np.load(f"{SCRATCH}/m13_shadow.npy")
F = S["isFake"] == 1
T = ~F
disp = T & (S["simVxy"] >= 1.0)
uniq = S["uniqueDeliverer"] == 1
W = ["02", "05", "10", "20"]
WV = [0.02, 0.05, 0.10, 0.20]

print("=== G. OVERLAP with a pure ABSOLUTE mX kill (would the kill already take it?) ===")
for w, wv in zip(W, WV):
    sh = S[f"sX{w}"] == 1
    print(f"  dR<{wv:.2f}: shadowed fakes n={int((sh&F).sum())};"
          f" mX<0 {float((S['mX'][sh&F] < 0).mean()):.3f},"
          f" mX<1 {float((S['mX'][sh&F] < 1).mean()):.3f},"
          f" mX<2 {float((S['mX'][sh&F] < 2).mean()):.3f}"
          f"  || ALL fakes: mX<0 {float((S['mX'][F] < 0).mean()):.3f},"
          f" mX<1 {float((S['mX'][F] < 1).mean()):.3f},"
          f" mX<2 {float((S['mX'][F] < 2).mean()):.3f}")
print()

print("=== H. SHELL enrichment (exclusive dR shells, higher-mX chain neighbour) ===")
prevF = prevT = 0.0
for w, wv in zip(W, WV):
    fy, ty = S[f"sX{w}"][F].mean(), S[f"sX{w}"][T].mean()
    print(f"  shell (..,{wv:.2f}]: fake {fy - prevF:.4f}  true {ty - prevT:.4f}  "
          f"enrichment {(fy - prevF) / max(ty - prevT, 1e-9):.2f}")
    prevF, prevT = fy, ty
print()

print("=== I. PER-BRANCH enrichment (is the lever alive in the residual-fake home?) ===")
branches = [("T4-class (nL<=4)", S["nLayers"] <= 4),
            ("IP 5+ (dca<0.5)", (S["nLayers"] >= 5) & (S["exempt"] == 0)),
            ("EXEMPT 5+ (dca>=0.5)", (S["nLayers"] >= 5) & (S["exempt"] == 1)),
            ("barrel |eta|<1", np.abs(S["eta"]) < 1.0),
            ("endcap |eta|>2", np.abs(S["eta"]) > 2.0)]
for nm, b in branches:
    line = f"  {nm:>22}: nFake {int((b&F).sum()):>6d} nTrue {int((b&T).sum()):>6d} | "
    for w, wv in zip(W, WV):
        fy = S[f"sX{w}"][b & F].mean() if (b & F).any() else 0
        ty = S[f"sX{w}"][b & T].mean() if (b & T).any() else 0
        line += f"dR{wv:.2f} {fy:.3f}/{ty:.3f}={fy/max(ty,1e-9):.2f}  "
    print(line)
print()

print("=== J. EXCHANGE RATE: fakes removed per IRREPLACEABLE true removed ===")
print("   (a lost unique-deliverer true == a lost sim == direct efficiency loss)")
print(f"{'rule':>34} {'fakeKill':>9} {'uniqTrue':>9} {'fakes/lostSim':>14}")
grid = np.linspace(-4, 6, 501)
for w, wv in zip(W, WV):
    for gap in [0.0, 4.0]:
        for tau in [0.0, 1.0]:
            hit = (S[f"bX{w}"] > -90) & (S[f"bX{w}"] - S["mX"] > gap)
            k = hit & (S["mX"] < tau)
            kF, kU = int((k & F).sum()), int((k & T & uniq).sum())
            print(f"{f'A: dR<{wv:.2f} gap{gap:.0f} mX<{tau:.0f}':>34} {kF:>9d} {kU:>9d} "
                  f"{kF/max(kU,1):>14.2f}")
for tau in [-0.5, 0.0, 0.5, 1.0, 2.0]:
    k = S["mX"] < tau
    kF, kU = int((k & F).sum()), int((k & T & uniq).sum())
    print(f"{f'B: absolute mX<{tau:+.1f}':>34} {kF:>9d} {kU:>9d} {kF/max(kU,1):>14.2f}")
for tau in [-0.5, 0.0, 1.0]:
    k = (S["mX"] < tau) & (S["mD"] < 0)
    kF, kU = int((k & F).sum()), int((k & T & uniq).sum())
    print(f"{f'B+: absolute mX<{tau:+.1f} & mD<0':>34} {kF:>9d} {kU:>9d} {kF/max(kU,1):>14.2f}")
