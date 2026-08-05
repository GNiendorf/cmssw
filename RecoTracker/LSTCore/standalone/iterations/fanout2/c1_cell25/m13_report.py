#!/usr/bin/env python3
"""m13_report.py -- MISSION 3 aggregation: shadow-fake yield curves + tournament verdict."""
import numpy as np
import uproot

SA = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
SCRATCH = ("/tmp/claude-31734/-mnt-data1-gsn27-here-CMSSW-17-0-0-pre2-src-"
           "RecoTracker-LSTCore-standalone/4cedadc2-877f-450a-b16a-2454b2f202ac/scratchpad")
W = ["02", "05", "10", "20"]

S = np.load(f"{SCRATCH}/m13_shadow.npy")
F = S["isFake"] == 1
T = ~F
disp = T & (S["simVxy"] >= 1.0)
vdisp = T & (S["simVxy"] >= 5.0)
uniq = S["uniqueDeliverer"] == 1

# global TC counts for FR arithmetic (in-cut, whole 296-event replica sample)
t = uproot.open(f"{SA}/prototype/ab_m12_w7.root")["tree"]
a = t.arrays(["tc_pt", "tc_eta", "tc_isFake", "tc_isChain", "evt"], library="np")
evts = set(int(e) for e in np.unique(S["evt"]))
nTC = nFake = nChain = 0
for i in range(len(a["evt"])):
    if int(a["evt"][i]) not in evts:
        continue
    pt = np.asarray(a["tc_pt"][i]); eta = np.asarray(a["tc_eta"][i])
    inc = (pt > 0.9) & (np.abs(eta) < 4.5)
    nTC += int(inc.sum())
    nFake += int((np.asarray(a["tc_isFake"][i])[inc] == 1).sum())
    nChain += int((np.asarray(a["tc_isChain"][i])[inc] == 1).sum())
print(f"SAMPLE: {len(evts)} events | in-cut TCs {nTC} (chain {nChain}) | fake {nFake} "
      f"= FR {nFake/nTC:.4f} | chain-slice victims {len(S)} fake {F.sum()} "
      f"(chain FR {F.mean():.4f}) | trues {T.sum()} (displaced vxy>=1 {disp.sum()}, "
      f"vxy>=5 {vdisp.sum()}) | unique-deliverer trues {int((T&uniq).sum())}")
print()

print("=== 1. SHADOW YIELD (fraction with a qualifying neighbour; pt ratio < 2) ===")
print(f"{'dR':>6} | {'FAKE hi-mX':>10} {'FAKE +pix':>10} {'FAKE hi-gate':>12} "
      f"{'FAKE hi-mX-TRUE':>15} | {'TRUE hi-mX':>10} {'TRUE +pix':>10} | "
      f"{'DISP hi-mX':>10} {'ratio F/T':>9}")
for w in W:
    fX, fP, fG, fT_ = (S[f"sX{w}"][F].mean(), S[f"sXP{w}"][F].mean(),
                       S[f"sG{w}"][F].mean(), S[f"sXT{w}"][F].mean())
    tX, tP = S[f"sX{w}"][T].mean(), S[f"sXP{w}"][T].mean()
    dX = S[f"sX{w}"][disp].mean()
    print(f"{int(w)/100:>6.2f} | {fX:>10.4f} {fP:>10.4f} {fG:>12.4f} {fT_:>15.4f} | "
          f"{tX:>10.4f} {tP:>10.4f} | {dX:>10.4f} {fX/max(tX,1e-9):>9.2f}")
print()

print("=== 2. (a) VICTIM'S OWN ABSOLUTE GATE MARGIN mX -- shadow yield by margin bin ===")
bins = [-np.inf, -2, -1, -0.5, 0, 0.5, 1, 2, 4, np.inf]
print(f"{'mX bin':>14} | {'nFake':>7} {'sX02':>6} {'sX05':>6} {'sX10':>6} {'sX20':>6} | "
      f"{'nTrue':>7} {'sX10':>6} | {'nDisp':>6} {'sX10':>6}")
for b in range(len(bins) - 1):
    m = (S["mX"] >= bins[b]) & (S["mX"] < bins[b + 1])
    nf, nt = int((m & F).sum()), int((m & T).sum())
    if nf + nt == 0:
        continue
    row = f"[{bins[b]:>5.1f},{bins[b+1]:>5.1f}) | {nf:>7d} "
    for w in W:
        row += f"{S[f'sX{w}'][m & F].mean() if nf else 0:>6.3f} "
    row += f"| {nt:>7d} {S['sX10'][m & T].mean() if nt else 0:>6.3f} | "
    nd = int((m & disp).sum())
    row += f"{nd:>6d} {S['sX10'][m & disp].mean() if nd else 0:>6.3f}"
    print(row)
print()

print("=== 3. (b) DISPLACED MARGIN mD of the SHADOWED population (collateral check) ===")
for w in ["05", "10"]:
    sh = S[f"sX{w}"] == 1
    print(f" dR<{int(w)/100:.2f}: shadowed fakes n={int((sh&F).sum())} "
          f"mD median {np.median(S['mD'][sh&F]):+.2f} | shadowed trues n={int((sh&T).sum())} "
          f"mD median {np.median(S['mD'][sh&T]):+.2f} | shadowed displaced-trues "
          f"n={int((sh&disp).sum())} mD median "
          f"{np.median(S['mD'][sh&disp]) if (sh&disp).any() else float('nan'):+.2f}")
    for lo, hi, nm in [(-np.inf, 0, "mD<0"), (0, 2, "0<=mD<2"), (2, np.inf, "mD>=2")]:
        m = (S["mD"] >= lo) & (S["mD"] < hi)
        print(f"    {nm:>8}: fakes {int((m&F).sum()):>6d} shadowed {S[f'sX{w}'][m&F].mean():.3f}"
              f" | trues {int((m&T).sum()):>6d} shadowed {S[f'sX{w}'][m&T].mean():.3f}"
              f" | disp-trues {int((m&disp).sum()):>5d} shadowed "
              f"{S[f'sX{w}'][m&disp].mean() if (m&disp).any() else float('nan'):.3f}")
print()

print("=== 4. (c) LOCAL TC DENSITY DECILE (jet safety; dens = in-cut TCs within dR<0.1) ===")
q = np.quantile(S["dens"], np.linspace(0, 1, 11))
print(f"{'decile':>7} {'densRange':>12} | {'nFake':>7} {'sX10':>6} {'sX20':>6} | "
      f"{'nTrue':>7} {'sX10':>6} {'sX20':>6} | {'nDispT':>7} {'sX10':>6} | {'uniqT':>7} {'sX10':>6}")
for d in range(10):
    lo, hi = q[d], q[d + 1]
    m = (S["dens"] >= lo) & (S["dens"] <= hi if d == 9 else S["dens"] < hi)
    nf, nt = int((m & F).sum()), int((m & T).sum())
    nd, nu = int((m & disp).sum()), int((m & T & uniq).sum())
    if nf + nt == 0:
        continue
    print(f"{d:>7d} {f'{lo:.0f}-{hi:.0f}':>12} | {nf:>7d} "
          f"{S['sX10'][m&F].mean() if nf else 0:>6.3f} {S['sX20'][m&F].mean() if nf else 0:>6.3f} | "
          f"{nt:>7d} {S['sX10'][m&T].mean() if nt else 0:>6.3f} "
          f"{S['sX20'][m&T].mean() if nt else 0:>6.3f} | {nd:>7d} "
          f"{S['sX10'][m&disp].mean() if nd else 0:>6.3f} | {nu:>7d} "
          f"{S['sX10'][m&T&uniq].mean() if nu else 0:>6.3f}")
print()

print("=== 5. TOURNAMENT vs PURE ABSOLUTE MARGIN (the decisive test) ===")
print("   rule A (tournament): kill iff shadowed at dR<w AND mX < tau")
print("   rule B (absolute)  : kill iff mX < tau' -- tau' scanned to match rule A's TRUE cost")
taus = [-1.0, -0.5, 0.0, 0.5, 1.0, 1.5, 2.0]
grid = np.linspace(-4, 4, 401)
for w in W:
    sh = S[f"sX{w}"] == 1
    print(f"  -- dR < {int(w)/100:.2f} --")
    print(f"{'tau':>6} {'fakeKill':>9} {'%allFake':>9} {'trueKill':>9} {'uniqTrue':>9} "
          f"{'dispTrue':>9} {'vdispT':>7} | {'FR after':>9} | {'B: fake@same uniqTrue':>22}")
    for tau in taus:
        killF = int((sh & F & (S["mX"] < tau)).sum())
        killT = int((sh & T & (S["mX"] < tau)).sum())
        killU = int((sh & T & uniq & (S["mX"] < tau)).sum())
        killD = int((sh & disp & (S["mX"] < tau)).sum())
        killV = int((sh & vdisp & (S["mX"] < tau)).sum())
        fr = (nFake - killF) / (nTC - killF - killT)
        # rule B calibrated to the same unique-true collateral
        bU = np.array([int((T & uniq & (S["mX"] < g)).sum()) for g in grid])
        j = int(np.searchsorted(bU, killU, side="right") - 1)
        j = max(j, 0)
        bF = int((F & (S["mX"] < grid[j])).sum())
        print(f"{tau:>6.1f} {killF:>9d} {killF/max(F.sum(),1)*100:>8.1f}% {killT:>9d} "
              f"{killU:>9d} {killD:>9d} {killV:>7d} | {fr:>9.4f} | "
              f"{bF:>10d} (tau'={grid[j]:+.2f}, uniqT={bU[j]})")
print()

print("=== 6. WHAT THE SHADOWED FAKES ARE (dR<0.1, higher-mX chain neighbour) ===")
sh = S["sX10"] == 1
for nm, m in [("all fakes", F), ("shadowed fakes", F & sh), ("unshadowed fakes", F & ~sh)]:
    print(f"  {nm:>18}: n={int(m.sum()):>6d} mX med {np.median(S['mX'][m]):+.2f} "
          f"mD med {np.median(S['mD'][m]):+.2f} gate med {np.median(S['gate'][m]):+.2f} "
          f"nL5+ {float((S['nLayers'][m]>=5).mean()):.3f} exempt {float(S['exempt'][m].mean()):.3f} "
          f"dup {float((S['isDup'][m]==1).mean()):.3f} pt med {np.median(S['pt'][m]):.2f} "
          f"|eta| med {np.median(np.abs(S['eta'][m])):.2f} dens med {np.median(S['dens'][m]):.0f}")
for nm, m in [("all trues", T), ("shadowed trues", T & sh), ("shadowed uniq-trues", T & sh & uniq),
              ("shadowed disp-trues", disp & sh)]:
    print(f"  {nm:>18}: n={int(m.sum()):>6d} mX med {np.median(S['mX'][m]):+.2f} "
          f"mD med {np.median(S['mD'][m]):+.2f} nL5+ {float((S['nLayers'][m]>=5).mean()):.3f} "
          f"dens med {np.median(S['dens'][m]):.0f}")
print()

print("=== 7. PIXEL-DOMINANT VARIANT (pixel TC always outranks a chain) ===")
for w in W:
    p = S[f"pix{w}"] == 1
    print(f"  dR<{int(w)/100:.2f}: fakes with ANY pixel TC nearby {p[F].mean():.4f} "
          f"| trues {p[T].mean():.4f} | uniq-trues {p[T&uniq].mean():.4f} "
          f"| disp-trues {p[disp].mean():.4f}")
