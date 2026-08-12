#!/usr/bin/env python3
"""S2: affine-pin a retrained 3-class gate head, then refit the ChainConfig bar VALUES at
fixed per-cell signal efficiency, prompt-class and displaced-class as SEPARATE targets.

WHY THE PIN COMES FIRST.  `marginX` has exactly ONE consumer outside the gate itself:
K9's best-first ORDER KEY, `orderKey = score - orderAlpha * max(0, orderHinge - marginX)`
(ChainArbitrate.h:141; grep confirms marginP/marginD/zFake/zPrompt/zDisp have NO other
reader in the tree).  A bar refit absorbs any monotone change in the margin scale, but the
order key does NOT -- it is a RANKING, and S1 [21:05] proved a fixed-efficiency table cannot
protect a ranked logit.  So the head's three output rows are rescaled by `a` and the prompt
and displaced BIASES shifted by `b` relative to fake, which maps ALL THREE margins as
m -> a*m + b and therefore changes NO ordering anywhere, while pinning the scale the hinge
lives on.  Only then are the bars refitted, on the pinned margins.

THE CELLS, verbatim from ChainGate.h:617-664 -- structure preserved, only values move:

  ip4   nL<=4, dca <  max(dcaSplit, t4ExemptDcaMin)=2.0     kill iff mX < m3Theta4 (+zdM4 in band)
  ex4   nL<=4, dca >= 2.0, NOT far                          kill iff mD < m3Theta4D (+zdM4D in band)
  ex4f  nL<=4, dca >= dcaSplit2=12 and f16 <= 0.02          kill iff mD < m3Theta4D2  [LEFT AT -1e9,
                                                            i.e. the shipped free pass: E1-B2's
                                                            guarded far cell is a STRUCTURE, not a bar]
  ip5   nL>=5, dca <  0.5                                   kill iff mX < m3ThetaRI   (m3Theta5/6 = 1e9)
  ex5   nL>=5, dca >= 0.5                                   kill iff mX < barR        (m3ThetaD  = 1e9)
        barR = aEtaC<zEta1 ? m3ThetaRB : (band ? m3ThetaRT : m3ThetaR)
  C25   nL==5 and nNodes==2, only if not already killed     kill iff mP < c25Theta AND mD < c25ThetaD

TARGETS.  Per cell, the SHIPPED gate's own acceptance on the SAME rows, measured from the
dump (which carries the shipped zF/zP/zD), separately for the prompt-true and the
displaced-true class.  Three ways to spend two targets on one bar:

  dual  bar = min(b_P, b_D)   both classes end at or ABOVE shipped acceptance (never loses
              efficiency in that cell by construction; the union of two matched sets is a
              superset of each, so this is neutral-or-LOOSER -- the same honest caveat S1
              recorded for the edge OR-rule)
  dpin  bar = b_D  (displaced matched EXACTLY, prompt free) -- tighter, claws fake back
  ppin  bar = b_P  (prompt matched EXACTLY, displaced free)

Usage: barfit.py --model <pt> --lab <labdir> --out <json> [--pin meanstd|quantile]
"""
import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from train3 import build_inputs, event_split  # noqa: E402

# Shipped ChainConfig values (interface/ChainConfig.h) -- the reference bars.
SHIP = dict(dcaSplit=0.5, t4ExemptDcaMin=2.0, dcaSplit2=12.0, t4FarMaxResid=0.02,
            m3Theta4=2.0, m3Theta4D=-2.5, m3Theta4D2=-1e9,
            m3ThetaRI=-0.5, m3ThetaR=-1.8, m3ThetaRB=-1.2, m3ThetaRT=-1.2,
            c25Theta=2.0, c25ThetaD=-1.5,
            zEta1=1.1, zEta2=1.7, zdM4=-0.5, zdM4D=1.2, zdRI=0.0, zdR=0.0)
MINBAR = -1e4      # a floor that behaves as "accept everything" without being a sentinel


def cells(M):
    """Boolean masks for the gate's decision cells, plus the band flag."""
    nL = M["nLayers"].astype(np.int32)
    dca = M["dcaXY"].astype(np.float64)
    band = (M["flags"] & 4) != 0            # inZ, as the KERNEL computed it
    aeta = M["aEtaC"].astype(np.float64)
    t4 = nL <= 4
    ex = dca >= max(SHIP["dcaSplit"], SHIP["t4ExemptDcaMin"])
    far = (dca >= SHIP["dcaSplit2"])        # the resid guard is added by the caller (needs f16)
    c = {}
    c["ip4"] = t4 & ~ex
    c["ex4"] = t4 & ex
    c["ip5"] = ~t4 & (dca < SHIP["dcaSplit"])
    c["ex5"] = ~t4 & (dca >= SHIP["dcaSplit"])
    c["band"] = band
    c["far_dca"] = far
    c["inB"] = (aeta >= 0.0) & (aeta < SHIP["zEta1"])
    c["c25"] = (nL == 5) & (M["nNodes"] == 2)
    return c


def bar_for_acc(m, target):
    """Smallest-in-magnitude bar b with #{m >= b} == round(target*n), i.e. fixed acceptance.

    Returns (b, achieved_acc).  target 1.0 -> the tightest bar that still keeps 100%.
    """
    n = len(m)
    if n == 0:
        return None, None
    k = int(round(target * n))
    s = np.sort(m)
    if k <= 0:
        return float(s[-1]) + 1e-3, 0.0     # accept nothing (never used in practice)
    if k >= n:
        return float(s[0]), 1.0
    b = float(s[n - k])
    return b, float((m >= b).mean())


def solve_c25(mP, mD, yP, yD, accP, accD, iters=40):
    """Joint fit of (c25Theta, c25ThetaD): accept iff mP >= tP OR mD >= tD.

    Two equations (prompt acceptance = accP, displaced acceptance = accD), two unknowns.
    Alternating exact solve; each half-step is a quantile on the rows the OTHER bar has not
    already rescued, which is monotone, so the iteration is a contraction in practice.
    """
    tP = np.quantile(mP[yP], 1.0 - accP) if yP.sum() else 0.0
    tD = SHIP["c25ThetaD"]
    for _ in range(iters):
        # solve tP so that prompt acceptance hits accP given tD
        if yP.sum():
            rescued = mD[yP] >= tD
            need = int(round(accP * yP.sum())) - int(rescued.sum())
            cand = mP[yP][~rescued]
            if need <= 0:
                tP = float(np.max(mP[yP])) + 1e-3
            elif need >= len(cand):
                tP = float(np.min(cand)) if len(cand) else MINBAR
            else:
                s = np.sort(cand)
                tP = float(s[len(cand) - need])
        # solve tD so that displaced acceptance hits accD given tP
        if yD.sum():
            rescued = mP[yD] >= tP
            need = int(round(accD * yD.sum())) - int(rescued.sum())
            cand = mD[yD][~rescued]
            if need <= 0:
                tD = float(np.max(mD[yD])) + 1e-3
            elif need >= len(cand):
                tD = float(np.min(cand)) if len(cand) else MINBAR
            else:
                s = np.sort(cand)
                tD = float(s[len(cand) - need])
    return tP, tD


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True)
    ap.add_argument("--lab", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--pin", choices=["meanstd", "quantile", "none"], default="meanstd")
    ap.add_argument("--fake-target", type=float, default=0.0,
                    help="live FAKE chains per event to hit with the `fkm` variant. A uniform\nadditive offset on every refitted bar is bisected until the count matches. The natural\ncross-arm target is the CONTROL arm's own shipped-gate live-fake count (268.3/evt), which is\nthe only way to ask `what does the gate have to spend to give the fake rate back`.")
    ap.add_argument("--nevt", type=float, default=1000.0)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--fit-on", choices=["val", "trval"], default="val",
                    help="rows the bars are FITTED on. `val` measures generalisation on\nthe frozen test events (S1 discipline); `trval` uses train+val -- 4x the rows, which is\nwhat a shipped calibration artefact should use, and still leaves the 200 test events clean.")
    a = ap.parse_args()
    import torch

    X, names, M = build_inputs(a.lab)
    tr, va, te = event_split(M["evt"], a.seed)
    fit = (tr | va) if a.fit_on == "trval" else va
    blob = torch.load(a.model, map_location="cpu", weights_only=False)
    norm = json.load(open(os.path.join(os.path.dirname(a.model),
                                       "chain3_norm_" + os.path.basename(a.model)[7:-3] + ".json")))
    mu = np.array(norm["mean"], dtype=np.float32)
    sd = np.array(norm["std"], dtype=np.float32)
    sdt = blob["state_dict"]
    hid = blob["arch"][1]
    net = torch.nn.Sequential(torch.nn.Linear(X.shape[1], hid), torch.nn.ReLU(),
                             torch.nn.Linear(hid, hid), torch.nn.ReLU(),
                             torch.nn.Linear(hid, 3))
    net.load_state_dict(sdt)
    net.eval()
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    net.to(dev)
    Xs = torch.tensor(np.ascontiguousarray((X - mu) / sd))
    Z = np.empty((len(X), 3), dtype=np.float32)
    with torch.no_grad():
        for i in range(0, len(Xs), 1 << 20):
            z = net(Xs[i:i + (1 << 20)].to(dev)).float().cpu()
            Z[i:i + (1 << 20)] = np.asarray(z.tolist(), dtype=np.float32)

    # ---- shipped margins straight out of the dump -------------------------------------
    sP = M["zP"] - M["zF"]
    sD = M["zD"] - M["zF"]
    sX = np.maximum(M["zP"], M["zD"]) - M["zF"]
    nP, nD = Z[:, 1] - Z[:, 0], Z[:, 2] - Z[:, 0]
    nX = np.maximum(Z[:, 1], Z[:, 2]) - Z[:, 0]

    rep = {"model": a.model, "lab": a.lab, "pin": a.pin, "fit_on": a.fit_on,
           "best_epoch": blob.get("best_epoch"), "best_sel": blob.get("best_sel")}

    # ---- STEP 1: affine pin of the margin scale (protects K9's order key) --------------
    if a.pin == "none":
        A, B = 1.0, 0.0
    elif a.pin == "meanstd":
        A = float(sX[fit].std() / nX[fit].std())
        B = float(sX[fit].mean() - A * nX[fit].mean())
    else:
        q = [0.25, 0.75]
        sq, nq = np.quantile(sX[fit], q), np.quantile(nX[fit], q)
        A = float((sq[1] - sq[0]) / (nq[1] - nq[0]))
        B = float(sq[0] - A * nq[0])
    nP, nD, nX = A * nP + B, A * nD + B, A * nX + B
    Zp = Z.copy()
    rep["affine"] = {"a": A, "b": B}
    qs = [0.05, 0.10, 0.25, 0.50, 0.75, 0.90, 0.95]
    rep["mX_pin_check"] = {
        "shipped_mean": float(sX[te].mean()), "new_mean": float(nX[te].mean()),
        "shipped_std": float(sX[te].std()), "new_std": float(nX[te].std()),
        "shipped_q": [float(x) for x in np.quantile(sX[te], qs)],
        "new_q": [float(x) for x in np.quantile(nX[te], qs)],
        "shipped_frac_below_hinge5": float((sX[te] < 5.0).mean()),
        "new_frac_below_hinge5": float((nX[te] < 5.0).mean())}

    # ---- classes and cells ------------------------------------------------------------
    lab = M["label"] == 1
    vxy = M["vxy"]
    y3 = np.zeros(len(X), np.int8)
    y3[lab & (vxy < 1.0)] = 1
    y3[lab & (vxy >= 1.0)] = 2
    C = cells(M)
    f16 = np.load(os.path.join(a.lab, "X.npy"), mmap_mode="r")[:, 16]
    far = C["far_dca"] & (np.asarray(f16) <= SHIP["t4FarMaxResid"])
    C["ex4n"] = C["ex4"] & ~far          # the bar cell
    C["ex4f"] = C["ex4"] & far           # the free-pass cell (m3Theta4D2 = -1e9)

    # the eight primary bar cells, with (margin_ship, margin_new, shipped bar)
    prim = [
        ("ip4",  C["ip4"] & ~C["band"], "X", SHIP["m3Theta4"]),
        ("ip4z", C["ip4"] & C["band"],  "X", SHIP["m3Theta4"] + SHIP["zdM4"]),
        ("ex4",  C["ex4n"] & ~C["band"], "D", SHIP["m3Theta4D"]),
        ("ex4z", C["ex4n"] & C["band"],  "D", SHIP["m3Theta4D"] + SHIP["zdM4D"]),
        ("ip5",  C["ip5"],              "X", SHIP["m3ThetaRI"]),
        ("ex5b", C["ex5"] & C["inB"],   "X", SHIP["m3ThetaRB"]),
        ("ex5t", C["ex5"] & C["band"] & ~C["inB"], "X", SHIP["m3ThetaRT"]),
        ("ex5e", C["ex5"] & ~C["band"] & ~C["inB"], "X", SHIP["m3ThetaR"]),
    ]
    MSHIP = {"X": sX, "D": sD, "P": sP}
    MNEW = {"X": nX, "D": nD, "P": nP}

    fits = {}
    for nm, mask, mkind, sbar in prim:
        mv, mn = MSHIP[mkind], MNEW[mkind]
        e = {"margin": mkind, "shipped_bar": sbar, "n_val": int((mask & fit).sum()),
             "n_test": int((mask & te).sum())}
        for cls, key in ((1, "P"), (2, "D")):
            sel = mask & fit & (y3 == cls)
            n = int(sel.sum())
            accs = float((mv[sel] >= sbar).mean()) if n else None
            b, ach = (bar_for_acc(mn[sel], accs) if n and accs is not None else (None, None))
            e["n_%s" % key] = n
            e["acc_%s_ship" % key] = accs
            e["bar_%s" % key] = b
            e["acc_%s_new" % key] = ach
        e["n_fake"] = int((mask & fit & (y3 == 0)).sum())
        e["acc_fake_ship"] = (float((mv[mask & fit & (y3 == 0)] >= sbar).mean())
                              if e["n_fake"] else None)
        cand = [x for x in (e["bar_P"], e["bar_D"]) if x is not None]
        e["bar_dual"] = min(cand) if cand else sbar
        e["bar_dpin"] = e["bar_D"] if e["bar_D"] is not None else e["bar_dual"]
        e["bar_ppin"] = e["bar_P"] if e["bar_P"] is not None else e["bar_dual"]
        fits[nm] = e

    # ---- C25: joint 2-bar fit, CONDITIONAL on the primary rule -------------------------
    def primary_kill(mask_kind, bars):
        """kill flag under a given bar variant, primary rules only."""
        kill = np.zeros(len(X), bool)
        for nm, mask, mkind, _ in prim:
            b = bars[nm]
            kill |= mask & (MNEW[mkind] < b)
        # far cell: m3Theta4D2 = -1e9 -> never kills
        return kill

    ship_kill = np.zeros(len(X), bool)
    for nm, mask, mkind, sbar in prim:
        ship_kill |= mask & (MSHIP[mkind] < sbar)

    c25 = {}
    for variant in ("dual", "dpin", "ppin"):
        bars = {nm: fits[nm]["bar_" + variant] for nm, _, _, _ in prim}
        nk = primary_kill(None, bars)
        univ_s = C["c25"] & fit & ~ship_kill
        univ_n = C["c25"] & fit & ~nk
        accP = (float(((sP[univ_s & (y3 == 1)] >= SHIP["c25Theta"]) |
                       (sD[univ_s & (y3 == 1)] >= SHIP["c25ThetaD"])).mean())
                if (univ_s & (y3 == 1)).sum() else 1.0)
        accD = (float(((sP[univ_s & (y3 == 2)] >= SHIP["c25Theta"]) |
                       (sD[univ_s & (y3 == 2)] >= SHIP["c25ThetaD"])).mean())
                if (univ_s & (y3 == 2)).sum() else 1.0)
        yP = univ_n & (y3 == 1)
        yD = univ_n & (y3 == 2)
        tP, tD = solve_c25(nP, nD, yP, yD, accP, accD)
        gotP = (float(((nP[yP] >= tP) | (nD[yP] >= tD)).mean()) if yP.sum() else None)
        gotD = (float(((nP[yD] >= tP) | (nD[yD] >= tD)).mean()) if yD.sum() else None)
        c25[variant] = {"c25Theta": tP, "c25ThetaD": tD, "acc_P_ship": accP,
                        "acc_D_ship": accD, "acc_P_new": gotP, "acc_D_new": gotD,
                        "n_P": int(yP.sum()), "n_D": int(yD.sum())}
    rep["fits"] = fits
    rep["c25"] = c25

    # ---- STEP 3: held-out TEST judgement of each variant ------------------------------
    def full_kill(bars, cT, cD, mS=False):
        mv = MSHIP if mS else MNEW
        kill = np.zeros(len(X), bool)
        for nm, mask, mkind, sbar in prim:
            b = sbar if mS else bars[nm]
            kill |= mask & (mv[mkind] < b)
        cell = C["c25"] & ~kill
        tp = SHIP["c25Theta"] if mS else cT
        td = SHIP["c25ThetaD"] if mS else cD
        kill |= cell & (mv["P"] < tp) & (mv["D"] < td)
        return kill

    # ---- STEP 2b: the FAKE-COUNT-MATCHED variant ---------------------------------------
    # A uniform additive offset t on every bar. In the affine-pinned margin scale a common
    # additive shift is the one-parameter family that tightens every cell equally, so it does
    # not re-shape the cell structure -- it only chooses how much efficiency to spend.
    VARIANTS = ["dual", "dpin", "ppin"]
    if a.fake_target > 0:
        base = {nm: fits[nm]["bar_dual"] for nm, _, _, _ in prim}
        cT0, cD0 = c25["dual"]["c25Theta"], c25["dual"]["c25ThetaD"]
        fitn = float(np.unique(M["evt"][fit]).size)

        def nfake(t):
            bars = {k: v + t for k, v in base.items()}
            k = full_kill(bars, cT0 + t, cD0 + t)
            return float(((~k) & fit & (y3 == 0)).sum()) / fitn
        lo, hi = -6.0, 12.0
        if nfake(lo) < a.fake_target:
            t = lo
        elif nfake(hi) > a.fake_target:
            t = hi
        else:
            for _ in range(50):
                mid = 0.5 * (lo + hi)
                if nfake(mid) > a.fake_target:
                    lo = mid
                else:
                    hi = mid
            t = 0.5 * (lo + hi)
        for nm, _, _, _ in prim:
            fits[nm]["bar_fkm"] = fits[nm]["bar_dual"] + t
        c25["fkm"] = dict(c25["dual"])
        c25["fkm"]["c25Theta"] = cT0 + t
        c25["fkm"]["c25ThetaD"] = cD0 + t
        rep["fkm"] = {"offset": t, "target_live_fake_per_evt": a.fake_target,
                      "achieved_live_fake_per_evt": nfake(t),
                      "dual_live_fake_per_evt": nfake(0.0)}
        print("fkm: uniform bar offset %+.4f  -> live fake/evt %.2f (target %.2f; dual gives %.2f)"
              % (t, nfake(t), a.fake_target, nfake(0.0)))
        VARIANTS.append("fkm")

    judge = {}
    ks = full_kill(None, None, None, mS=True)
    for variant in VARIANTS:
        bars = {nm: fits[nm]["bar_" + variant] for nm, _, _, _ in prim}
        kn = full_kill(bars, c25[variant]["c25Theta"], c25[variant]["c25ThetaD"])
        row = {}
        for cls, key in ((0, "fake"), (1, "prompt"), (2, "disp")):
            m = te & (y3 == cls)
            row["acc_%s_ship" % key] = float((~ks[m]).mean())
            row["acc_%s_new" % key] = float((~kn[m]).mean())
        row["live_ship"] = int((~ks[te]).sum())
        row["live_new"] = int((~kn[te]).sum())
        nte = float(np.unique(M["evt"][te]).size)
        for cls, key in ((0, "fake"), (1, "prompt"), (2, "disp")):
            row["live_%s_per_evt_ship" % key] = float(((~ks) & te & (y3 == cls)).sum()) / nte
            row["live_%s_per_evt_new" % key] = float(((~kn) & te & (y3 == cls)).sum()) / nte
        row["bars"] = bars
        row["c25"] = [c25[variant]["c25Theta"], c25[variant]["c25ThetaD"]]
        # per-cell fake acceptance ratio (the head-quality metric at matched signal eff)
        pc = {}
        for nm, mask, mkind, sbar in prim:
            mf = mask & te & (y3 == 0)
            if mf.sum() < 50:
                continue
            fs = float((MSHIP[mkind][mf] >= sbar).mean())
            fn = float((MNEW[mkind][mf] >= bars[nm]).mean())
            pc[nm] = {"n": int(mf.sum()), "fake_acc_ship": fs, "fake_acc_new": fn,
                      "ratio": (fn / fs if fs > 0 else None)}
        row["per_cell_fake"] = pc
        judge[variant] = row
    rep["test_judge"] = judge

    # ---- the ChainConfig deltas to write, per variant ---------------------------------
    cfg = {}
    for variant in VARIANTS:
        b = judge[variant]["bars"]
        cfg[variant] = {
            "m3Theta4": b["ip4"], "zdM4": b["ip4z"] - b["ip4"],
            "m3Theta4D": b["ex4"], "zdM4D": b["ex4z"] - b["ex4"],
            "m3Theta4D2": SHIP["m3Theta4D2"],
            "m3ThetaRI": b["ip5"], "zdRI": 0.0,
            "m3ThetaRB": b["ex5b"], "m3ThetaRT": b["ex5t"], "m3ThetaR": b["ex5e"],
            "zdR": 0.0,
            "c25Theta": c25[variant]["c25Theta"], "c25ThetaD": c25[variant]["c25ThetaD"]}
    rep["chainconfig"] = cfg
    with open(a.out, "w") as fh:
        json.dump(rep, fh, indent=1)

    print("affine a=%.6f b=%.6f | mX TEST shipped %.4f/%.4f -> new %.4f/%.4f  hinge<5 %.4f -> %.4f"
          % (A, B, rep["mX_pin_check"]["shipped_mean"], rep["mX_pin_check"]["shipped_std"],
             rep["mX_pin_check"]["new_mean"], rep["mX_pin_check"]["new_std"],
             rep["mX_pin_check"]["shipped_frac_below_hinge5"],
             rep["mX_pin_check"]["new_frac_below_hinge5"]))
    print("\n%-6s %-3s %8s %8s | acc_P ship->new    acc_D ship->new   | bars P / D / dual"
          % ("cell", "m", "nP", "nD"))
    for nm, _, _, sbar in prim:
        e = fits[nm]
        print("%-6s %-3s %8d %8d | %.5f->%.5f  %.5f->%.5f | %8.3f %8.3f %8.3f  (ship %.3f)"
              % (nm, e["margin"], e["n_P"], e["n_D"],
                 e["acc_P_ship"] or 0, e["acc_P_new"] or 0,
                 e["acc_D_ship"] or 0, e["acc_D_new"] or 0,
                 e["bar_P"] if e["bar_P"] is not None else float("nan"),
                 e["bar_D"] if e["bar_D"] is not None else float("nan"),
                 e["bar_dual"], sbar))
    for variant in VARIANTS:
        r = judge[variant]
        print("\nTEST %-5s  prompt %.5f->%.5f   disp %.5f->%.5f   FAKE %.5f->%.5f (ratio %.4f)  live %d->%d"
              % (variant, r["acc_prompt_ship"], r["acc_prompt_new"],
                 r["acc_disp_ship"], r["acc_disp_new"],
                 r["acc_fake_ship"], r["acc_fake_new"],
                 r["acc_fake_new"] / max(r["acc_fake_ship"], 1e-12),
                 r["live_ship"], r["live_new"]))
        print("   live/evt(TEST)  fake %.2f->%.2f  prompt %.1f->%.1f  disp %.3f->%.3f"
              % (r["live_fake_per_evt_ship"], r["live_fake_per_evt_new"],
                 r["live_prompt_per_evt_ship"], r["live_prompt_per_evt_new"],
                 r["live_disp_per_evt_ship"], r["live_disp_per_evt_new"]))
        print("   C25 %.3f / %.3f   bars " % tuple(r["c25"])
              + " ".join("%s=%.3f" % (k, v) for k, v in r["bars"].items()))
    print("\nwrote %s" % a.out)


if __name__ == "__main__":
    main()
