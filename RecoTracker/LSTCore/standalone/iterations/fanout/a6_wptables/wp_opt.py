#!/usr/bin/env python3
"""Joint per-cell operating-point solver for the -G 5 K9 path (angle a6, M10).

wp_tables.py places each cell's threshold from that cell's OWN per-sim score quantile.
Measured consequence (see the a6 log): the thresholds come out so loose they keep
87-99% of fakes. The reason is structural -- a per-cell quantile implicitly assumes the
sim is reachable ONLY through that cell, so it protects the worst 2% of per-cell maxima
even when the same sim is also carried by three other cells at a far higher score. LST's
own per-type WP tables do not work that way: a T4 WP may be brutal precisely because the
track is also findable as a T5.

This solver states the real constraint: a sim must survive SOMEWHERE. With
  pass(chain)      = score(chain) >= thr[cell(chain)]        (score scale = branch scale)
  retained(sim)    = any true chain of the sim passes
the problem is: maximise killed fakes subject to retained-sim fraction >= target in every
displacement stratum. Retention is MONOTONE non-increasing in each threshold separately,
so coordinate ascent with an exact per-cell solve converges quickly:

  for cell k (round-robin):
    safe(s)   = sim s already retained via a cell other than k   (cheap: counts)
    cmax_k(s) = max score of s's true chains in cell k
    need      = ceil(target * N) - #safe                 (per stratum)
    thr_k     = the need-th largest cmax_k among at-risk sims    (exact, O(N log N))
  thr_k = min over strata; +inf when need <= 0 (the whole cell is redundant -- this is how
  the v1j "-U4 1e9 kills every exempt T4-class chain" discovery reproduces itself).

Caveats (stated, not hidden): chain-level retention is an UPPER BOUND on TC efficiency --
a surviving chain can still lose the K9 MD claim, and labelChains truth (>=2/3 MDs share
a sim, per member) is a proxy for the harness's >0.75 hit-fraction matching. The solver
is therefore a threshold-GEOMETRY instrument; the A/B harness is the judge.
"""

import argparse
import sys

import numpy as np

NO_CUT = -1e30
KILL = 1e9
LEN_NAME = ("<=4", "==5", ">=6")
ETA_NAME = ("|eta|<1.1", "1.1-1.7", ">=1.7")
BR_NAME = ("IP/gate", "exempt/legacy")

# Displacement strata (sim vxy, cm) with independent retention targets. The plan's
# priority 1 says the eff-vs-vxy CURVE must hold across the full range, so the strata are
# separate constraints rather than one population-weighted number.
STRATA = (("prompt", 0.0, 1.0), ("d1_5", 1.0, 5.0), ("d5_15", 5.0, 15.0), ("d15+", 15.0, 1e9))


def cell_index(nlay, eta, branch):
    lb = np.where(nlay >= 6, 2, np.where(nlay == 5, 1, 0))
    a = np.abs(eta)
    eb = np.where(a >= 1.7, 2, np.where(a >= 1.1, 1, 0))
    return (branch * 3 + lb) * 3 + eb


def load(path):
    d = np.loadtxt(path, comments="#", dtype=np.float64)
    return d


def solve(dump, dca_split, t4_floor, targets, rounds, margin, cell_cap, verbose=True):
    d = load(dump)
    keep = d[:, 6] == 0  # pixdrop == 0: only chains that actually face a threshold
    d = d[keep]
    evt = d[:, 0].astype(np.int64)
    nlay = d[:, 1].astype(np.int32)
    eta = d[:, 2]
    dca = d[:, 3]
    gate = d[:, 4]
    legacy = d[:, 5]
    label = d[:, 7].astype(np.int8)
    sim = d[:, 8].astype(np.int64)
    vxy = d[:, 9]

    floor = np.where(nlay <= 4, max(dca_split, t4_floor), dca_split)
    branch = (dca >= floor).astype(np.int64)
    cell = cell_index(nlay, eta, branch)
    score = np.where(branch == 0, gate, legacy)

    # ---- truth population: true chains matched to an ACCEPTED sim (kinematics known).
    tmask = (label == 1) & (vxy > -900)
    t_cell, t_score, t_vxy = cell[tmask], score[tmask], vxy[tmask]
    key = evt[tmask] * 10_000_000 + sim[tmask]
    usim, sidx = np.unique(key, return_inverse=True)
    nsim = usim.size
    # sim-level vxy (constant per sim)
    sim_vxy = np.zeros(nsim)
    sim_vxy[sidx] = t_vxy

    strat_of_sim = np.full(nsim, -1, np.int32)
    for i, (_, lo, hi) in enumerate(STRATA):
        strat_of_sim[(sim_vxy >= lo) & (sim_vxy < hi)] = i

    # per (sim, cell) max score -> dense [nsim, 18] matrix (18 cells is tiny).
    # hasm is REQUIRED: cmax stays -inf where the sim has no chain in the cell, and
    # (-inf >= -inf) is True in numpy, which would silently make a no-cut cell "cover"
    # every sim in the event sample.
    cmax = np.full((nsim, 18), -np.inf)
    np.maximum.at(cmax, (sidx, t_cell), t_score)
    hasm = np.zeros((nsim, 18), bool)
    hasm[sidx, t_cell] = True

    # ---- fake population per cell (for the reported kill counts)
    f_cell, f_score = cell[label == 0], score[label == 0]

    # Per-cell true-CHAIN efficiency cap: no cell may kill more than (1 - cap) of its own
    # true chains. Without it the redundancy argument degenerates -- the solver happily
    # kills 97% of all true chains because one survivor per sim is formally enough, which
    # destroys the K9 claim dynamics and the track-length metric.
    chain_cap = np.full(18, -np.inf)
    for k in range(18):
        s = np.sort(t_score[t_cell == k])
        if s.size:
            kk = int(np.floor(s.size * (1.0 - cell_cap)))
            chain_cap[k] = -np.inf if kk <= 0 else float(s[kk]) - margin

    thr = np.full(18, -np.inf)  # start permissive: every cell accepts everything
    # Fake-rich cells first: the objective is fake kill, and greedy coordinate ascent is
    # order-dependent, so spend the shared redundancy budget where it buys the most.
    order = sorted(range(18), key=lambda k: -int((f_cell == k).sum()))
    for rnd in range(rounds):
        moved = 0.0
        for k in order:
            has_k = hasm[:, k]
            # retained through some OTHER cell at the current thresholds
            other = np.zeros(nsim, bool)
            for j in range(18):
                if j == k:
                    continue
                other |= hasm[:, j] & (cmax[:, j] >= thr[j])
            tk = np.inf
            for si, (_, _, _) in enumerate(STRATA):
                m = strat_of_sim == si
                n = int(m.sum())
                if n == 0:
                    continue
                need = int(np.ceil(targets[si] * n)) - int((other & m).sum())
                if need <= 0:
                    continue  # this stratum is fully covered elsewhere: cell k may die
                cand = cmax[m & ~other & has_k, k]
                if cand.size < need:
                    tk = min(tk, -np.inf)  # cannot reach the target even keeping all
                else:
                    s = np.sort(cand)[::-1]
                    tk = min(tk, float(s[need - 1]) - margin)
            # tk == +inf  : every stratum is covered without this cell -> kill it whole
            #               (this is how "-U4 1e9" reproduces itself from data)
            # tk == -inf  : some stratum cannot reach its target even keeping everything
            #               -> no cut in this cell
            new = KILL if tk == np.inf else tk
            new = min(new, chain_cap[k]) if np.isfinite(chain_cap[k]) else -np.inf
            old = thr[k]
            moved += abs(np.clip(old, -1e3, 1e3) - np.clip(new, -1e3, 1e3))
            thr[k] = new
        if verbose:
            print(f"  round {rnd}: total threshold movement {moved:.3f}")
        if moved < 1e-6:
            break

    # ---- report
    passed = np.zeros(nsim, bool)
    for j in range(18):
        passed |= hasm[:, j] & (cmax[:, j] >= thr[j])
    if verbose:
        print(f"{'stratum':8s} {'nSim':>7s} {'retained':>9s} {'frac':>7s} {'target':>7s}")
        for si, (nm, _, _) in enumerate(STRATA):
            m = strat_of_sim == si
            n = int(m.sum())
            r = int((passed & m).sum())
            print(f"{nm:8s} {n:7d} {r:9d} {r / max(n, 1):7.4f} {targets[si]:7.4f}")
        print()
        print(f"{'branch':14s} {'len':4s} {'eta':10s} {'thr':>12s} {'nFake':>9s} {'killF%':>7s} "
              f"{'nTrue':>9s} {'killT%':>7s}")
        for k in range(18):
            b, l, e = k // 9, (k // 3) % 3, k % 3
            fm = f_cell == k
            nf = int(fm.sum())
            kf = int((fm & (f_score < thr[k])).sum())
            tm = t_cell == k
            nt = int(tm.sum())
            kt = int((tm & (t_score < thr[k])).sum())
            ts = "kill-all" if thr[k] >= KILL else ("none" if not np.isfinite(thr[k]) else f"{thr[k]:.3f}")
            print(f"{BR_NAME[b]:14s} {LEN_NAME[l]:4s} {ETA_NAME[e]:10s} {ts:>12s} {nf:9d} "
                  f"{100.0 * kf / max(nf, 1):7.2f} {nt:9d} {100.0 * kt / max(nt, 1):7.2f}")
        tot_f = f_cell.size
        tot_kf = int((f_score < thr[f_cell]).sum())
        tot_t = t_cell.size
        tot_kt = int((t_score < thr[t_cell]).sum())
        print(f"TOTAL fakes {tot_f} killed {tot_kf} ({100.0 * tot_kf / max(tot_f, 1):.2f}%);"
              f" true chains {tot_t} killed {tot_kt} ({100.0 * tot_kt / max(tot_t, 1):.2f}%)")
    return thr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--dca-split", type=float, default=0.5)
    ap.add_argument("--t4-floor", type=float, default=0.0)
    ap.add_argument("--targets", default="0.99,0.995,0.998,0.999",
                    help="per-stratum sim-retention targets: prompt,1-5,5-15,15+")
    ap.add_argument("--rounds", type=int, default=4)
    ap.add_argument("--margin", type=float, default=1e-4)
    ap.add_argument("--cell-cap", type=float, default=0.97,
                    help="per-cell TRUE-CHAIN efficiency floor (no cell may kill more than 1-cap)")
    args = ap.parse_args()

    targets = [float(x) for x in args.targets.split(",")]
    if len(targets) != len(STRATA):
        print(f"Error: --targets needs {len(STRATA)} values", file=sys.stderr)
        return 1
    thr = solve(args.dump, args.dca_split, args.t4_floor, targets, args.rounds, args.margin,
                args.cell_cap)
    with open(args.out, "w") as f:
        f.write(f"# a6 joint per-cell WPs: targets={args.targets} dcaSplit={args.dca_split} "
                f"t4Floor={args.t4_floor} rounds={args.rounds} cellCap={args.cell_cap}\n")
        f.write("# branch lenBin etaBin threshold\n")
        for k in range(18):
            b, l, e = k // 9, (k // 3) % 3, k % 3
            t = thr[k]
            if not np.isfinite(t):
                t = NO_CUT if t < 0 else KILL
            f.write(f"{b} {l} {e} {t:.6f}\n")
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
