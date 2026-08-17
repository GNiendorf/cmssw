#!/usr/bin/env python3
"""[DISP2] Fit the stage-A PROMPT delivery bar table at fixed per-cell signal-pair efficiency.

SCHEME (one sentence): the attach delivery working point is calibrated at fixed per-cell signal
efficiency, like the T5-DNN-binned WP tables of the other networks -- for a target efficiency
epsilon, a cell's bar is the logit at which a fraction epsilon of that cell's TRUE stage-A pairs
(seed shares a sim with the target chain) still clears it.

The table is 6 pt rows (seed ptIn, edges 2/5/10/25/50) x the 3 EXISTING seed-|eta| bands
(<1.1 / <1.7 / >=1.7).  It REPLACES the shipped low/high prompt rows entirely.  The
displaced-target row is a separate mechanism and is out of scope: displaced-judged pairs keep the
shipped displaced bar throughout, and are excluded from the fitted population.

No floors, no clamps, no inheritance: every one of the 18 values is the raw quantile.  The shipped
constants appear in the output ONLY as a reference column and as the baseline the dilution guard's
multiplier is measured against.

epsilon is chosen globally, with at most one target per seed-eta ZONE (1 zone = one global
epsilon; 2 = barrel / outside; 3 = barrel / transition / endcap).  Finer than the three bands is
not offered.

Usage: fitbars.py <extract.npz> <outprefix>
"""
import json
import os
import sys
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import census_disp as C  # noqa: E402

PT_EDGES = np.array([2.0, 5.0, 10.0, 25.0, 50.0])
N_PT, N_ETA = 6, 3
ETA_NAMES = ["barrel |eta|<1.1", "transition 1.1-1.7", "endcap >=1.7"]
PT_NAMES = ["<2", "2-5", "5-10", "10-25", "25-50", ">50"]
# Reference only -- never mixed into an output value.
SHIPPED_LO, SHIPPED_HI = C.NEW_LO, C.NEW_HI
T0 = time.time()


def log(m):
    print("[%7.1fs] %s" % (time.time() - T0, m), flush=True)


def wp_bars(true_logit, true_cell, eps_by_eta):
    """Per-cell bar at fixed signal efficiency: the largest x with (fraction of the cell's true
    pairs >= x) >= eps.  Exact order statistic, no interpolation -- a WP table is a quantile of a
    finite sample and inventing values between order statistics would overstate its resolution."""
    bars = np.full(N_ETA * N_PT, np.nan)
    ns = np.zeros(N_ETA * N_PT, np.int64)
    for e in range(N_ETA):
        eps = eps_by_eta[e]
        for p in range(N_PT):
            c = e * N_PT + p
            x = np.sort(true_logit[true_cell == c])
            ns[c] = len(x)
            if len(x) == 0:
                continue
            k = int(np.floor((1.0 - eps) * len(x)))
            bars[c] = x[min(k, len(x) - 1)]
    return bars, ns


def wp_bars_kfloor(true_logit, true_cell, eps_max, kmin=2):
    """[ARM-RETRAIN] Per-cell target with the anti-overfitting rule applied PER CELL.

    The rule DISP2 shipped -- "the loosest global epsilon at which every one of the 18 cells still
    has order-statistic index k >= 2" -- is a statement about the THINNEST cell (204 true pairs at
    >50 GeV), and it dragged every other cell down with it: at eps 0.99 the endcap < 2 GeV cell,
    which holds 2.4M true pairs, sits at k = 23998, four orders of magnitude clear of the rule that
    is binding it. Since k is an integer index into that cell's OWN sorted true-pair logits, the
    overfitting the rule guards against is a per-cell property, so the guard belongs per cell:

        k[cell] = max(kmin, floor((1 - eps_max) * n[cell]))

    i.e. aim at eps_max everywhere, and in a cell too thin to resolve it fall back to the kmin-th
    lowest true pair, which is exactly the bar the global rule would have given that cell. Every
    bar is still a raw order statistic of its own cell and no cell is looser than kmin allows.
    Returns the bars, the cell populations, the index actually used, and the EFFECTIVE per-cell
    epsilon 1 - k/n, which is what should be quoted rather than eps_max.
    """
    bars = np.full(N_ETA * N_PT, np.nan)
    ns = np.zeros(N_ETA * N_PT, np.int64)
    ks = np.zeros(N_ETA * N_PT, np.int64)
    eff = np.full(N_ETA * N_PT, np.nan)
    for c in range(N_ETA * N_PT):
        x = np.sort(true_logit[true_cell == c])
        ns[c] = len(x)
        if len(x) == 0:
            continue
        k = max(int(kmin), int(np.floor((1.0 - eps_max) * len(x))))
        k = min(k, len(x) - 1)
        ks[c] = k
        bars[c] = x[k]
        eff[c] = 1.0 - k / len(x)
    return bars, ns, ks, eff


class Replay(object):
    """Exact offline replay of stage-A delivery: bar -> per-chain argmax -> per-seed contention.

    Both reductions are 'first row of each group in a fixed total order', so with the two orders
    precomputed a candidate table costs two boolean gathers and no sort.
    """

    def __init__(self, z):
        self.n = len(z["p_ev"])
        ev = z["p_ev"].astype(np.int64)
        NCS, NPS = 1 << 17, 1 << 17
        self.gt = ev * NCS + z["p_tgt"].astype(np.int64)
        self.gp = ev * NPS + z["p_pls"].astype(np.int64)
        self.logit = z["p_logit"].astype(np.float64)
        self.disp = z["p_disp"].astype(bool)
        self.istrue = z["p_istrue"].astype(bool)
        self.ecell = z["p_ecell"].astype(np.int64)
        self.cell = self.ecell * N_PT + z["p_ptcell"].astype(np.int64)
        # displaced rows keep the shipped displaced bar, untouched by the fit
        self.dispbar = np.array(C.NEW_DISP)[self.ecell]
        # tie-breaks: the kernel's are (higher logit, then lower index); lexsort's last key is
        # primary, so the orders below reproduce them exactly.
        log("sorting replay orders (%d rows)" % self.n)
        self.O1 = np.lexsort((self.gp, -self.logit, self.gt))
        self.O2 = np.lexsort((self.gt, -self.logit, self.gp))
        self.gtO1 = self.gt[self.O1]
        self.gpO2 = self.gp[self.O2]
        log("replay ready")

    def run(self, bars18):
        bar = np.where(self.disp, self.dispbar, bars18[self.cell])
        ispass = self.logit >= bar
        s = ispass[self.O1]
        sub = np.flatnonzero(s)
        g = self.gtO1[sub]
        first = np.ones(len(g), bool)
        first[1:] = g[1:] != g[:-1]
        pickrows = self.O1[sub[first]]
        ispick = np.zeros(self.n, bool)
        ispick[pickrows] = True
        s2 = ispick[self.O2]
        sub2 = np.flatnonzero(s2)
        g2 = self.gpO2[sub2]
        f2 = np.ones(len(g2), bool)
        f2[1:] = g2[1:] != g2[:-1]
        return self.O2[sub2[f2]]           # the delivered rows


def main():
    z = np.load(sys.argv[1])
    pref = sys.argv[2]
    R = Replay(z)

    # ---- the fitted population: PROMPT-judged targets only ------------------------------------
    fitmask = R.istrue & ~R.disp
    tl, tc = R.logit[fitmask], R.cell[fitmask]
    log("true prompt pairs in the fit: %d" % len(tl))

    # ---- the sim side --------------------------------------------------------------------------
    s_ev = z["s_ev"].astype(np.int64)
    s_chain = z["s_chain"].astype(np.int64)
    s_pt = z["s_pt"].astype(np.float64)
    s_eta = np.abs(z["s_eta"].astype(np.float64))
    s_our = z["s_our"].astype(np.int64)
    s_mast = z["s_mast"].astype(np.int64)
    s_gt = s_ev * (1 << 17) + s_chain
    fam = np.isin(s_our, (4, 7)) & (s_chain >= 0)          # our T5-family sims, chain identified
    mfam = np.isin(s_mast, (4, 7))                          # master's T5-family sims
    s_eb = C.seed_eta_band(s_eta)
    s_pb = np.digitize(s_pt, PT_EDGES)
    s_cell = s_eb * N_PT + s_pb

    def shares(delivered_gt):
        """replayed pT5 share of our T5-family, per (eta band) and per cell."""
        d = np.isin(s_gt[fam], delivered_gt)
        eb, cl = s_eb[fam], s_cell[fam]
        out_e = np.array([d[eb == e].mean() if (eb == e).any() else np.nan for e in range(N_ETA)])
        out_c = np.array([d[cl == c].mean() if (cl == c).any() else np.nan
                          for c in range(N_ETA * N_PT)])
        return out_e, out_c, int(d.sum()), int(len(d))

    mast_e = np.array([(s_mast[mfam & (s_eb == e)] == 7).mean() for e in range(N_ETA)])
    mast_c = np.array([(s_mast[mfam & (s_cell == c)] == 7).mean()
                       if (mfam & (s_cell == c)).any() else np.nan for c in range(N_ETA * N_PT)])
    mast_barrel = (s_mast[mfam & (s_eb == 0)] == 7).mean()
    mast_out = (s_mast[mfam & (s_eb > 0)] == 7).mean()

    # ---- the SHIPPED baseline, replayed ---------------------------------------------------------
    ship18 = np.empty(N_ETA * N_PT)
    for e in range(N_ETA):
        for p in range(N_PT):
            # pt bins 0 (<2) and 1 (2-5) are the shipped LOW row, 2-5 the shipped HIGH row.
            ship18[e * N_PT + p] = SHIPPED_LO[e] if (p <= 1) else SHIPPED_HI[e]
    # [ARM-RETRAIN] argv[3], when given, is a JSON list of 18 bars in the SAME (eta-major) order as
    # `ship18` -- the table actually deployed in the binary that produced this dump. Without it the
    # baseline is reconstructed from the pre-table 2-row constants, which stopped being what the
    # binary does the moment the 6x3 table landed, and every "x shipped" ratio below would then be
    # measured against a bar model no build has.
    if len(sys.argv) > 3:
        ship18 = np.asarray(json.load(open(sys.argv[3])), dtype=np.float64)
        assert ship18.shape == (N_ETA * N_PT,), ship18.shape
        log("baseline bar table read from %s" % sys.argv[3])
    dv = R.run(ship18)
    base_wrong = int((~R.istrue[dv]).sum())
    base_wrong_cell = np.bincount(R.cell[dv][~R.istrue[dv]], minlength=N_ETA * N_PT)
    base_true = int(R.istrue[dv].sum())
    b_e, b_c, b_n, b_d = shares(np.unique(R.gt[dv]))
    b_barrel = b_e[0]
    b_out = np.isin(s_gt[fam & (s_eb > 0)], np.unique(R.gt[dv])).mean()
    log("SHIPPED replay: delivered %d (true %d, wrong-seed %d); share barrel %.4f out %.4f "
        "| master barrel %.4f out %.4f" % (len(dv), base_true, base_wrong, b_barrel, b_out,
                                           mast_barrel, mast_out))

    # ---- epsilon scans ---------------------------------------------------------------------------
    # The grid runs to 1.0 (bar = the cell's LOWEST true-pair logit) because the dilution guard
    # turns out not to bind anywhere near 0.995 -- without the top of the range the scan reports a
    # grid edge as an optimum.
    GRID = [0.90, 0.95, 0.97, 0.98, 0.99, 0.995, 0.9975, 0.999, 0.9995, 0.9999, 1.0]
    scan1 = []
    for eps in GRID:
        bars, ns = wp_bars(tl, tc, [eps] * N_ETA)
        bb = np.where(np.isnan(bars), np.inf, bars)
        dv = R.run(bb)
        gtu = np.unique(R.gt[dv])
        wrong = int((~R.istrue[dv]).sum())
        sh_e, sh_c, nd, nf = shares(gtu)
        sh_out = np.isin(s_gt[fam & (s_eb > 0)], gtu).mean()
        scan1.append(dict(eps=eps, bars=bars.tolist(), n_cell=ns.tolist(),
                          wrong=wrong, wrong_mult=wrong / base_wrong,
                          true_deliv=int(R.istrue[dv].sum()),
                          share_barrel=float(sh_e[0]), share_out=float(sh_out),
                          share_eta=sh_e.tolist(), share_cell=sh_c.tolist()))
        log("eps %.4f  barrel %.4f (master %.4f)  out %.4f (master %.4f)  wrong x%.2f  true+%d"
            % (eps, sh_e[0], mast_barrel, sh_out, mast_out, wrong / base_wrong,
               int(R.istrue[dv].sum()) - base_true))

    # [ARM-RETRAIN] the same grid under the PER-CELL k floor (see wp_bars_kfloor).
    scanK = []
    for eps in GRID:
        bars, ns, ks, eff = wp_bars_kfloor(tl, tc, eps, kmin=2)
        bb = np.where(np.isnan(bars), np.inf, bars)
        dv = R.run(bb)
        gtu = np.unique(R.gt[dv])
        wrong = int((~R.istrue[dv]).sum())
        sh_e, sh_c, nd, nf = shares(gtu)
        sh_out = np.isin(s_gt[fam & (s_eb > 0)], gtu).mean()
        scanK.append(dict(eps_max=eps, bars=bars.tolist(), n_cell=ns.tolist(), k_cell=ks.tolist(),
                          eps_eff=eff.tolist(), wrong=wrong, wrong_mult=wrong / base_wrong,
                          true_deliv=int(R.istrue[dv].sum()),
                          share_barrel=float(sh_e[0]), share_out=float(sh_out),
                          share_eta=sh_e.tolist(), share_cell=sh_c.tolist()))
        log("K eps_max %.4f  barrel %.4f  out %.4f (master %.4f)  wrong x%.2f  true+%d  minK %d"
            % (eps, sh_e[0], sh_out, mast_out, wrong / base_wrong,
               int(R.istrue[dv].sum()) - base_true, int(ks.min())))

    def score(sh_b, sh_o):
        return abs(sh_b - mast_barrel) + abs(sh_o - mast_out)

    ok1 = [s for s in scan1 if s["wrong_mult"] <= 2.0]
    best1 = min(ok1, key=lambda s: score(s["share_barrel"], s["share_out"])) if ok1 else None

    # 2-zone (barrel, outside) and 3-zone scans
    scanN = {}
    for nz in (2, 3):
        rows = []
        G = [0.97, 0.98, 0.99, 0.995, 0.9975, 0.999, 0.9995, 0.9999, 1.0]
        combos = ([(a, b, b) for a in G for b in G] if nz == 2
                  else [(a, b, c) for a in G for b in G for c in G])
        for combo in combos:
            bars, ns = wp_bars(tl, tc, list(combo))
            bb = np.where(np.isnan(bars), np.inf, bars)
            dv = R.run(bb)
            gtu = np.unique(R.gt[dv])
            wrong = int((~R.istrue[dv]).sum())
            sh_e, sh_c, nd, nf = shares(gtu)
            sh_out = np.isin(s_gt[fam & (s_eb > 0)], gtu).mean()
            rows.append(dict(eps=list(combo), wrong_mult=wrong / base_wrong, wrong=wrong,
                             true_deliv=int(R.istrue[dv].sum()),
                             share_barrel=float(sh_e[0]), share_out=float(sh_out),
                             share_eta=sh_e.tolist(), share_cell=sh_c.tolist(),
                             bars=bars.tolist(), n_cell=ns.tolist()))
        okN = [r for r in rows if r["wrong_mult"] <= 2.0]
        bestN = min(okN, key=lambda r: score(r["share_barrel"], r["share_out"])) if okN else None
        scanN[nz] = dict(best=bestN, n=len(rows))
        log("%d-zone best: eps %s barrel %.4f out %.4f wrong x%.2f"
            % (nz, bestN["eps"], bestN["share_barrel"], bestN["share_out"], bestN["wrong_mult"])
            if bestN else "%d-zone: no point passes the guard" % nz)

    out = dict(
        base=dict(wrong=base_wrong, true=base_true, share_barrel=float(b_barrel),
                  share_out=float(b_out), share_eta=b_e.tolist(), share_cell=b_c.tolist(),
                  bars=ship18.tolist(), wrong_cell=base_wrong_cell.tolist()),
        master=dict(share_barrel=float(mast_barrel), share_out=float(mast_out),
                    share_eta=mast_e.tolist(), share_cell=mast_c.tolist()),
        scan1=scan1, best1=best1, scanK=scanK,
        best2=scanN[2]["best"], best3=scanN[3]["best"],
        pt_edges=PT_EDGES.tolist(), eta_names=ETA_NAMES, pt_names=PT_NAMES,
        shipped_lo=list(SHIPPED_LO), shipped_hi=list(SHIPPED_HI), shipped_disp=list(C.NEW_DISP))
    json.dump(out, open(pref + ".json", "w"), indent=1)
    log("wrote %s.json" % pref)


if __name__ == "__main__":
    main()
