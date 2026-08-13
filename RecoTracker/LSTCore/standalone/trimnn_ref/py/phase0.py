#!/usr/bin/env python3
"""TRIM-NN PHASE 0. Can the FROZEN chain head rank a chain's terminal variants?

Input: the labelled variant dump (truthv.py output dir), which carries THREE rows per pre-trim
chain with nNodes >= 3 -- variant 0 = full, 1 = inner-dropped, 2 = outer-dropped -- each with

  * its own >= 75% sim-match label (labelChainsHarness over the variant's OWN hit list),
  * its own 25-feature row, dcaXY and the three frozen-head logits,
  * its own combined-fit chi2 (the `score` slot), which is the quantity K6f compares.

The three questions the round is gated on:

  Q1  agreement of the frozen head's argmax with (a) the K6f chi2 rule and (b) the LABEL;
  Q2  AUC of "this variant is the real track" from the head margin mX vs from the chi2
      improvement ratio, on the eligible population;
  Q3  what fraction of chains is eligible at all (the cost bound of the whole idea).

Usage: phase0.py <labdir> <tag> [--core]
"""
import os
import sys

import numpy as np

# K6f constants (ChainConfig.h defaults; the rule this round is trying to replace)
TRIM_ABS_CHI2 = 1.0
TRIM_FACTOR = 1.2
TRIM_MIN_LAYERS = 5
CHI2_FLOOR = 1e-9

FULL, INNER, OUTER = 0, 1, 2


def auc(score, y):
    """Mann-Whitney AUC of `score` separating y == 1 from y == 0."""
    y = np.asarray(y).astype(bool)
    n1, n0 = int(y.sum()), int((~y).sum())
    if n1 == 0 or n0 == 0:
        return float("nan")
    r = np.empty(len(score), dtype=np.float64)
    order = np.argsort(score, kind="mergesort")
    s = score[order]
    # average ranks for ties
    i = 0
    rank = np.empty(len(s), dtype=np.float64)
    while i < len(s):
        j = i
        while j + 1 < len(s) and s[j + 1] == s[i]:
            j += 1
        rank[i:j + 1] = 0.5 * (i + j) + 1.0
        i = j + 1
    r[order] = rank
    return (r[y].sum() - n1 * (n1 + 1) / 2.0) / (n1 * n0)


def main():
    labdir, tag = sys.argv[1], sys.argv[2]
    core_only = "--core" in sys.argv[3:]
    M = np.load(os.path.join(labdir, "meta.npz"))
    X = np.load(os.path.join(labdir, "X.npy"))

    evt = M["evt"].astype(np.int64)
    cid = M["drop"].astype(np.int64)          # chain index within the event
    var = M["branch"].astype(np.int64)        # variant id
    lab = M["label"].astype(np.int64)
    chi2 = M["score"].astype(np.float64)      # the variant's combined-fit chi2
    zF, zP, zD = M["zF"].astype(np.float64), M["zP"].astype(np.float64), M["zD"].astype(np.float64)
    mX = np.maximum(zP, zD) - zF
    mP = zP - zF
    nLay = M["nLayers"].astype(np.int64)
    nNod = M["nNodes"].astype(np.int64)
    frac = M["frac"].astype(np.float64)

    # ---- pivot to one row per chain, three columns ------------------------------------------
    key = evt * (1 << 22) + cid
    uk, inv = np.unique(key, return_inverse=True)
    nC = len(uk)
    assert len(key) == 3 * nC, "expected exactly 3 variant rows per chain, got %d for %d chains" % (len(key), nC)

    def pivot(a, dtype=np.float64):
        out = np.zeros((nC, 3), dtype=dtype)
        out[inv, var] = a
        return out

    C2 = pivot(chi2)
    MX = pivot(mX)
    MP = pivot(mP)
    LB = pivot(lab, np.int64)
    NL = pivot(nLay, np.int64)
    NN = pivot(nNod, np.int64)
    FR = pivot(frac)
    ISC = pivot(M["isCore"].astype(np.int64), np.int64) if "isCore" in M else np.zeros((nC, 3), np.int64)

    sel = np.ones(nC, bool)
    if core_only:
        sel = ISC.max(axis=1) > 0
    print("== %s ==  chains with nNodes >= 3: %d%s" % (tag, nC, "   (jet-core subset)" if core_only else ""))

    # ---- Q3: eligibility ---------------------------------------------------------------------
    elig_chi2 = C2[:, FULL] > TRIM_ABS_CHI2
    print("[Q3] of those, chi2Full > %.1f (the K6f concentrating guard): %d  (%.1f%%)"
          % (TRIM_ABS_CHI2, int(elig_chi2.sum()), 100.0 * elig_chi2.mean()))

    # ---- the K6f decision, reconstructed ------------------------------------------------------
    rI = np.where(NL[:, INNER] >= TRIM_MIN_LAYERS, C2[:, FULL] / np.maximum(C2[:, INNER], CHI2_FLOOR), -1.0)
    rO = np.where(NL[:, OUTER] >= TRIM_MIN_LAYERS, C2[:, FULL] / np.maximum(C2[:, OUTER], CHI2_FLOOR), -1.0)
    k6f = np.full(nC, FULL, np.int64)
    take_i = elig_chi2 & (rI >= rO) & (rI > TRIM_FACTOR)
    take_o = elig_chi2 & (rO > rI) & (rO > TRIM_FACTOR)
    k6f[take_i] = INNER
    k6f[take_o] = OUTER
    print("[K6f] would edit %d chains (%.2f%% of nNodes>=3): inner %d, outer %d"
          % (int((k6f != FULL).sum()), 100.0 * (k6f != FULL).mean(), int(take_i.sum()), int(take_o.sum())))

    # ---- the head's decision ------------------------------------------------------------------
    head = np.argmax(MX, axis=1)
    print("[HEAD] frozen-head argmax over mX picks: full %d (%.2f%%), inner %d, outer %d"
          % (int((head == FULL).sum()), 100.0 * (head == FULL).mean(),
             int((head == INNER).sum()), int((head == OUTER).sum())))
    headP = np.argmax(MP, axis=1)
    print("[HEAD] argmax over mP picks: full %.2f%%, inner %d, outer %d"
          % (100.0 * (headP == FULL).mean(), int((headP == INNER).sum()), int((headP == OUTER).sum())))

    # ---- Q1: agreement ------------------------------------------------------------------------
    s = sel
    print("[Q1a] head vs K6f agreement on eligible chains: %.4f"
          % (head[s & elig_chi2] == k6f[s & elig_chi2]).mean())
    print("[Q1a] head vs K6f agreement on the %d chains K6f actually EDITS: %.4f"
          % (int((s & (k6f != FULL)).sum()), (head[s & (k6f != FULL)] == k6f[s & (k6f != FULL)]).mean()))

    # against the LABEL. A chain is DECIDABLE when its variants disagree on truth.
    anyT = LB.max(axis=1) > 0
    allT = LB.min(axis=1) > 0
    dec = anyT & ~allT & s
    print("[Q1b] label-decidable chains (variants disagree on realness): %d of %d (%.2f%%)"
          % (int(dec.sum()), int(s.sum()), 100.0 * dec.sum() / max(int(s.sum()), 1)))
    if dec.any():
        # a decision is CORRECT when the picked variant is true
        pick_head = LB[dec, head[dec]] == 1
        pick_k6f = LB[dec, k6f[dec]] == 1
        pick_full = LB[dec, FULL] == 1
        print("[Q1b] on those, the picked variant is TRUE:  head %.4f | K6f %.4f | always-full %.4f"
              % (pick_head.mean(), pick_k6f.mean(), pick_full.mean()))
        # ORACLE tie-break: among true variants prefer the longest (most layers)
        best = np.where(LB[dec].max(axis=1) > 0,
                        np.argmax(LB[dec] * 1000 + NL[dec], axis=1), FULL)
        print("[Q1b] oracle would pick full %.3f / inner %.3f / outer %.3f"
              % ((best == FULL).mean(), (best == INNER).mean(), (best == OUTER).mean()))

    # the cell that matters most: chains K6f edits AND the edit changes the truth verdict
    ed = s & (k6f != FULL)
    if ed.any():
        gain = (LB[ed, k6f[ed]] == 1) & (LB[ed, FULL] == 0)
        loss = (LB[ed, k6f[ed]] == 0) & (LB[ed, FULL] == 1)
        print("[K6f truth ledger] of %d edits: RESCUES (fake full -> true variant) %d, "
              "KILLS (true full -> fake variant) %d, neutral %d"
              % (int(ed.sum()), int(gain.sum()), int(loss.sum()), int(ed.sum() - gain.sum() - loss.sum())))

    # ---- Q2: AUC of realness, per variant row ------------------------------------------------
    rowsel = np.repeat(s, 3)[np.argsort(np.argsort(inv * 3 + var))] if False else s[inv]
    el = elig_chi2[inv] & rowsel
    y = lab[el] == 1
    # chi2 improvement ratio of THIS row against the chain's full variant (1.0 for the full row)
    ratio = np.where(var[el] == FULL, 1.0, C2[inv[el], FULL] / np.maximum(chi2[el], CHI2_FLOOR))
    print("[Q2] eligible variant rows %d, true %d (%.4f)" % (len(y), int(y.sum()), y.mean()))
    print("[Q2] AUC(realness) from head mX          : %.4f" % auc(mX[el], y))
    print("[Q2] AUC(realness) from head mP          : %.4f" % auc(mP[el], y))
    print("[Q2] AUC(realness) from chi2 impr. ratio : %.4f" % auc(ratio, y))
    print("[Q2] AUC(realness) from -chi2 of the row : %.4f" % auc(-chi2[el], y))
    print("[Q2] AUC(realness) from nLayers          : %.4f" % auc(nLay[el].astype(float), y))

    # WITHIN-CHAIN ranking is the actual job. Pairwise contests on decidable chains.
    if dec.any():
        pairs = []
        for v in (INNER, OUTER):
            m = dec & (LB[:, FULL] != LB[:, v])
            if not m.any():
                continue
            yv = (LB[m, v] > LB[m, FULL])          # True: the variant is the real one
            dmX = MX[m, v] - MX[m, FULL]
            dr = np.log(np.maximum(C2[m, FULL], CHI2_FLOOR) / np.maximum(C2[m, v], CHI2_FLOOR))
            pairs.append((v, m.sum(), yv, dmX, dr))
        for v, n, yv, dmX, dr in pairs:
            print("[Q2 pairwise] %-5s vs full: %6d contests, variant-is-the-real-one %.4f | "
                  "AUC(dmX) %.4f  AUC(log chi2 ratio) %.4f"
                  % (("inner", "outer")[v - 1], n, yv.mean(), auc(dmX, yv), auc(dr, yv)))
        # pooled
        yv = np.concatenate([p[2] for p in pairs])
        dmX = np.concatenate([p[3] for p in pairs])
        dr = np.concatenate([p[4] for p in pairs])
        print("[Q2 pairwise POOLED] %d contests: AUC(dmX) %.4f  AUC(log chi2 ratio) %.4f"
              % (len(yv), auc(dmX, yv), auc(dr, yv)))

    # ---- what the head's own ordering costs, expressed as truth ------------------------------
    print("[SUMMARY] truth rate of the SELECTED variant over all %d eligible chains: "
          "head %.4f | K6f %.4f | always-full %.4f | oracle %.4f"
          % (int((s & elig_chi2).sum()),
             (LB[s & elig_chi2, head[s & elig_chi2]] == 1).mean(),
             (LB[s & elig_chi2, k6f[s & elig_chi2]] == 1).mean(),
             (LB[s & elig_chi2, FULL] == 1).mean(),
             (LB[s & elig_chi2].max(axis=1) == 1).mean()))


if __name__ == "__main__":
    main()
