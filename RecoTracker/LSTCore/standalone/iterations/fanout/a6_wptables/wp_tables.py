#!/usr/bin/env python3
"""Per-(branch, nLayers, |eta|) operating-point generator for the -G 5 K9 path.

Angle a6 of the M10 fan-out. The M9 winner (-G 5 -X 0.5 -T4 2 -T5 1 -U4 1e9) uses SIX
scalar thresholds: three on the gate-logit scale for the IP-compatible branch
(dcaXY < dcaSplit) and three on the legacy sum-logit scale for the dca-exempt branch.
This script replaces them with an 18-cell table

    cell = (branch, lenBin, etaBin)
      branch 0 = IP-compatible  -> threshold on the GATE LOGIT
      branch 1 = dca-exempt     -> threshold on the LEGACY score
      lenBin   0: nLayers <= 4   1: == 5   2: >= 6
      etaBin   0: |eta| < 1.1    1: [1.1, 1.7)   2: >= 1.7

placed so that each cell holds a *sim-level* true-chain efficiency target, separately
for the prompt (simVxy < 1 cm) and displaced (simVxy >= 1 cm) strata, and takes the
more permissive of the two.

Why sim-level and not chain-level: chains braid -- one sim track is typically re-found
by several chains built from different T3/MD duplicates (plan M2 "comparison hygiene:
count per sim track, not per object"). Losing 3% of TRUE CHAINS can cost 0% or 3% of
sim tracks depending on which ones; only the per-sim MAX score matters for whether the
track survives K9 at all. So the quantile is taken over per-(event, sim) maxima.

Calibration population: welded chains that actually face the threshold, i.e.
pixdrop == 0 (chains with a partOfPT5/PT3 member are dropped by K9 regardless of any
threshold, so including them would bias the quantiles).

Efficiency accounting uses only ACCEPTED sims (dump simVxy > -900); pileup-only-matched
chains are label 1 (they are real tracks -- not fakes by the harness definition) but
contribute no efficiency, so they are excluded from the quantile population while still
being counted in the reported kill/keep statistics.

Input: the PROTO_WP_DUMP text dump (see main.cc), columns
  evt nLayers eta dcaXY gateLogit legacyScore pixdrop label simIdx simVxy simPt
"""

import argparse
import sys

import numpy as np

DCA_SPLIT_DEFAULT = 0.5
ETA_EDGES = (1.1, 1.7)
DISP_VXY = 1.0  # cm: sim displacement boundary for the two-stratum targets
NO_CUT = -1e30

LEN_NAME = ("<=4", "==5", ">=6")
ETA_NAME = ("|eta|<1.1", "1.1-1.7", ">=1.7")
BR_NAME = ("IP/gate", "exempt/legacy")


def len_bin(n):
    return np.where(n >= 6, 2, np.where(n == 5, 1, 0))


def eta_bin(eta):
    a = np.abs(eta)
    return np.where(a >= ETA_EDGES[1], 2, np.where(a >= ETA_EDGES[0], 1, 0))


def load(path):
    d = np.loadtxt(path, comments="#", dtype=np.float64)
    return dict(
        evt=d[:, 0].astype(np.int64),
        nlay=d[:, 1].astype(np.int32),
        eta=d[:, 2].astype(np.float32),
        dca=d[:, 3].astype(np.float32),
        gate=d[:, 4].astype(np.float32),
        legacy=d[:, 5].astype(np.float32),
        pixdrop=d[:, 6].astype(np.int8),
        label=d[:, 7].astype(np.int8),
        sim=d[:, 8].astype(np.int64),
        vxy=d[:, 9].astype(np.float32),
        pt=d[:, 10].astype(np.float32),
    )


def per_sim_max(evt, sim, score):
    """Max score per (evt, sim) pair. Returns (keys, maxima)."""
    if evt.size == 0:
        return np.empty(0, np.int64), np.empty(0, np.float64)
    key = evt.astype(np.int64) * 10_000_000 + sim.astype(np.int64)
    order = np.argsort(key, kind="stable")
    key_s, score_s = key[order], score[order]
    uniq, start = np.unique(key_s, return_index=True)
    out = np.maximum.reduceat(score_s, start)
    return uniq, out


def quantile_threshold(maxima, target):
    """Largest t with fraction(maxima >= t) >= target (t drawn from the sample)."""
    n = maxima.size
    if n == 0:
        return None
    k = int(np.floor(n * (1.0 - target)))
    if k <= 0:
        return float(np.min(maxima))
    s = np.sort(maxima)
    return float(s[k])


def build(data, dca_split, t4_floor, tgt_prompt, tgt_disp, min_sims, min_disp_sims,
          margin, verbose=True):
    keep = data["pixdrop"] == 0
    nlay = data["nlay"][keep]
    eta = data["eta"][keep]
    dca = data["dca"][keep]
    gate = data["gate"][keep]
    legacy = data["legacy"][keep]
    label = data["label"][keep]
    evt = data["evt"][keep]
    sim = data["sim"][keep]
    vxy = data["vxy"][keep]

    lb = len_bin(nlay)
    eb = eta_bin(eta)
    floor = np.where(nlay <= 4, max(dca_split, t4_floor), dca_split)
    branch = (dca >= floor).astype(np.int32)  # 0 = IP (gate scale), 1 = exempt (legacy)

    thr = {}
    rows = []
    for b in range(2):
        score_all = gate if b == 0 else legacy
        for l in range(3):
            for e in range(3):
                m = (branch == b) & (lb == l) & (eb == e)
                n_tot = int(m.sum())
                n_true = int((m & (label == 1)).sum())
                n_fake = n_tot - n_true
                # Calibration population: true chains matched to an ACCEPTED sim.
                mc = m & (label == 1) & (vxy > -900)
                keys, mx = per_sim_max(evt[mc], sim[mc], score_all[mc])
                # Stratify the per-sim maxima by the sim's displacement (vxy is a sim
                # property, so any chain of that sim carries it).
                _, vx = per_sim_max(evt[mc], sim[mc], vxy[mc].astype(np.float64))
                prompt = mx[vx < DISP_VXY]
                disp = mx[vx >= DISP_VXY]
                t_p = quantile_threshold(prompt, tgt_prompt) if prompt.size >= min_sims else None
                t_d = quantile_threshold(disp, tgt_disp) if disp.size >= min_disp_sims else (
                    float(np.min(disp)) if disp.size else None)
                cands = [t for t in (t_p, t_d) if t is not None]
                t = min(cands) - margin if cands else NO_CUT
                if len(cands) == 0 or (prompt.size + disp.size) < min_sims:
                    t = NO_CUT
                thr[(b, l, e)] = t
                # Reported yields at the chosen threshold.
                surv = m & (score_all >= t)
                rows.append(
                    dict(b=b, l=l, e=e, n=n_tot, ntrue=n_true, nfake=n_fake,
                         nsim=int(prompt.size + disp.size), ndisp=int(disp.size), thr=t,
                         keep_true=int((surv & (label == 1)).sum()),
                         keep_fake=int((surv & (label == 0)).sum())))
    if verbose:
        print(f"# cells: dcaSplit={dca_split} t4Floor={t4_floor} "
              f"targets prompt={tgt_prompt} displaced={tgt_disp} margin={margin}")
        print(f"{'branch':14s} {'len':4s} {'eta':10s} {'nChain':>8s} {'nTrue':>8s} {'nFake':>8s} "
              f"{'nSim':>7s} {'nDisp':>6s} {'thr':>10s} {'keepT%':>7s} {'keepF%':>7s}")
        for r in rows:
            kt = 100.0 * r["keep_true"] / max(r["ntrue"], 1)
            kf = 100.0 * r["keep_fake"] / max(r["nfake"], 1)
            ts = "none" if r["thr"] <= NO_CUT / 2 else f"{r['thr']:.3f}"
            print(f"{BR_NAME[r['b']]:14s} {LEN_NAME[r['l']]:4s} {ETA_NAME[r['e']]:10s} "
                  f"{r['n']:8d} {r['ntrue']:8d} {r['nfake']:8d} {r['nsim']:7d} {r['ndisp']:6d} "
                  f"{ts:>10s} {kt:7.2f} {kf:7.2f}")
    return thr, rows


def write_table(path, thr, header):
    with open(path, "w") as f:
        f.write("# " + header + "\n")
        f.write("# branch lenBin etaBin threshold\n")
        for (b, l, e), t in sorted(thr.items()):
            f.write(f"{b} {l} {e} {t:.6f}\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--dca-split", type=float, default=DCA_SPLIT_DEFAULT)
    ap.add_argument("--t4-floor", type=float, default=0.0)
    ap.add_argument("--target", type=float, default=0.98, help="prompt per-cell sim-eff target")
    ap.add_argument("--target-disp", type=float, default=0.995,
                    help="displaced (simVxy>=1cm) per-cell sim-eff target (looser = higher)")
    ap.add_argument("--min-sims", type=int, default=200)
    ap.add_argument("--min-disp-sims", type=int, default=30)
    ap.add_argument("--margin", type=float, default=1e-4,
                    help="subtracted from every threshold so the calibrating chain itself survives")
    ap.add_argument("--exempt-t4-kill", action="store_true",
                    help="force cell (branch 1, len <=4, any eta) to +inf, i.e. the -U4 1e9 "
                         "v1j behaviour (exempt T4-class chains all die)")
    args = ap.parse_args()

    data = load(args.dump)
    print(f"loaded {data['evt'].size} chains from {args.dump}")
    thr, _ = build(data, args.dca_split, args.t4_floor, args.target, args.target_disp,
                   args.min_sims, args.min_disp_sims, args.margin)
    if args.exempt_t4_kill:
        for e in range(3):
            thr[(1, 0, e)] = 1e9
    write_table(args.out, thr,
                f"a6 per-cell WPs: target={args.target} targetDisp={args.target_disp} "
                f"dcaSplit={args.dca_split} t4Floor={args.t4_floor} "
                f"exemptT4Kill={args.exempt_t4_kill}")
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
