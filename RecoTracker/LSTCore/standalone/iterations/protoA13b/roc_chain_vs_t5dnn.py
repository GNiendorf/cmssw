#!/usr/bin/env python3
"""P1 kill-criterion study: chain-level discrimination vs the production t5dnn on
frozen identical T3 inputs (plan section 5c "P1 offline harness" / section 7 P1 gates).

Baseline side (production t5dnn, from the LST --allobj ntuple):
  - The intermediate T5 collection contains EXACTLY the T5s that passed the production
    per-(ptBin, etaBin) working point (dnn::t5dnn::kWp, interface/alpaka/Common.h) --
    they exist because they passed. Threshold -inf on t5_dnnScore therefore IS the
    production operating point; sweeping the threshold upward draws the (truncated)
    ROC above it. Scores below the WP are unobservable (those T5s were never built).
  - Sim-track efficiency: per ACCEPTED sim (pt > 0.9, |eta| < 4.5), a sim counts at
    threshold t iff ANY T5 matched to it with > 0.75 hit fraction (sim_t5IdxAllFrac)
    has t5_dnnScore >= t. Comparison hygiene (plan 10.4c, 2026-08-01): the t5_*
    collection RETAINS duplicates (dedup kernels only FLAG); all efficiency counting
    here is PER SIM TRACK, never per object.
  - Fake side: PER-OBJECT fake rate of the T5 collection vs threshold
    (t5_isFake == 1: no sim matched with > 0.75 hit fraction, pileup sims included in
    the matching). Per-object is the standard fake-rate convention; duplicates are
    counted as objects on both sides (both populations are pre-final-dedup).

Chain side (prototype K1-K6 welded chains, chains_300evt_simidx.root; -e 0 -L 0.5,
pre-arbitration, i.e. NO K9 -- the full pre-decision population):
  - Filtered to nLayers >= 5 for apples-to-apples with the T5 collection (the dump also
    contains the T4-class nLayers == 4 chains; the baseline T5 collection is 5-7 layers
    including extensions).
  - Two scores per chain:
      gate   = chain-gate MLP v2 logit (chain_mlp_v2.pt + chain_norm_v2.json,
               conditioning then standardization, exact chain_parity.py math),
      legacy = sum of member edge logits + 0.5 * nLayers (cf_sumEdgeLogit + lambdaLen
               * nLayers, the K6 sum-logit score at the production lambdaLen = 0.5).
  - Sim-track efficiency: per accepted sim, best score among chains LABELED to that sim
    (labelChains: label 1 iff ALL member T3s share one sim at >= 2/3 MD level; simIdx =
    highest-pt accepted sim of the intersection). Per sim track, never per object.
  - Fake side: per-object chain fake rate vs threshold (label == 0). Chains whose only
    matched sims are pileup are TRUE (not fake) -- same convention as t5_isFake.

Strata (denominator: accepted sims, pt > 0.9, |eta| < 4.5; vxy = sqrt(vx^2 + vy^2)):
  prompt vxy < 1 cm | displaced vxy >= 1 cm | very displaced vxy >= 5 cm.

Kill criteria (plan section 7, P1) evaluated at the t5dnn production operating point:
  K1: chain fake rate <= 1.2x t5dnn fake rate at EQUAL PROMPT sim-track efficiency.
  K2: chain displaced sim-track efficiency >= 1.5x t5dnn's at EQUAL per-object fake
      rate (both displaced strata reported).

Event sets: PRIMARY = the 60 held-out TEST events of the seed-42 60/20/20 event-level
split (train_chain.py / train_edge.py use the identical split procedure on the same
300 evt keys, so these events are jointly out-of-sample for BOTH the edge MLP -- which
feeds welding and the legacy score -- and the chain gate). The full 300-event sample is
reported as a cross-check (chain scores are partially in-sample there).

Outputs: printed tables + roc_chain_vs_t5dnn.json (curves + operating points).
"""

import argparse
import json
import os
import sys
import time

import numpy as np

PROTO_DIR = os.path.dirname(os.path.abspath(__file__))
T0 = time.time()

MATCH_FRAC = 0.75  # strict > 0.75, the writer's matchfrac convention
PT_MIN, ETA_MAX = 0.9, 4.5
LAMBDA_LEN = 0.5  # production K6 chain-length prior (r5/h4b winner shape)
N_GRID = 2001  # threshold-grid points stored in the json (criteria use exact data)


def log(msg):
    print(f"[{time.time() - T0:7.1f}s] {msg}", flush=True)


def parse_args():
    p = argparse.ArgumentParser(description=__doc__)
    d = os.path.dirname(PROTO_DIR)
    p.add_argument("--ntuple", default=os.path.join(d, "LSTNtuple_PU200RelVal_300evt.root"))
    p.add_argument("--chains", default=os.path.join(PROTO_DIR, "chains_300evt_simidx.root"))
    p.add_argument("--model", default=os.path.join(PROTO_DIR, "chain_mlp_v2.pt"))
    p.add_argument("--norm", default=os.path.join(PROTO_DIR, "chain_norm_v2.json"))
    p.add_argument("--out", default=os.path.join(PROTO_DIR, "roc_chain_vs_t5dnn.json"))
    p.add_argument("--seed", type=int, default=42,
                   help="split seed; MUST match train_chain.py/train_edge.py (42)")
    p.add_argument("--train-frac", type=float, default=0.6)
    p.add_argument("--val-frac", type=float, default=0.2)
    p.add_argument("--min-layers", type=int, default=5,
                   help="chain nLayers filter (default 5 = apples-to-apples with the "
                        "5-7 layer T5 collection; 4 adds the T4-class chains, a "
                        "population the T5 collection does not contain)")
    return p.parse_args()


# ------------------------------------------------------------------ event split

def test_event_keys(evt_keys, seed, train_frac, val_frac):
    """The exact train_chain.py/train_edge.py event split (np.unique is sorted, the
    default_rng(seed) shuffle is deterministic) -> the held-out TEST evt keys."""
    rng = np.random.default_rng(seed)
    uniq = np.unique(evt_keys.astype(np.uint64))
    rng.shuffle(uniq)
    n = len(uniq)
    n_tr = int(round(train_frac * n))
    n_va = int(round(val_frac * n))
    return set(uniq[n_tr + n_va:].tolist())


# ------------------------------------------------------------------ chain side

def load_chains(path, model_path, norm_path, min_layers):
    """Load the chain dump (nLayers >= min_layers), score with the v2 gate and the
    legacy sum-logit + lambda_len * nLayers. Returns dict of flat arrays."""
    import uproot
    import torch

    with open(norm_path) as fh:
        norm = json.load(fh)
    names = norm["feature_names"]
    mu = np.asarray(norm["mean"], dtype=np.float32)
    sd = np.asarray(norm["std"], dtype=np.float32)
    n_in = len(names)

    f = uproot.open(path)
    tree = f["chains"]
    feat_branches = [f"cf_{i:02d}" for i in range(n_in)]
    arr = tree.arrays(["evt", "label", "simIdx", "nLayers", "simVxy"] + feat_branches,
                      library="np")
    keep = arr["nLayers"] >= min_layers
    log(f"chains: {len(keep)} total, {keep.sum()} with nLayers >= {min_layers}")

    X = np.empty((int(keep.sum()), n_in), dtype=np.float32)
    for j, b in enumerate(feat_branches):
        X[:, j] = arr[b][keep]
    out = {k: arr[k][keep] for k in ("evt", "label", "simIdx", "nLayers", "simVxy")}

    # Legacy K6 score from RAW features (cf_02 = sumEdgeLogit, pre-conditioning).
    i_sum = names.index("cf_sumEdgeLogit")
    out["legacy"] = X[:, i_sum] + LAMBDA_LEN * out["nLayers"].astype(np.float32)

    # Gate logit: conditioning IN ORDER then standardization (chain_parity.py math).
    for c in norm.get("conditioning") or []:
        j = names.index(c["feature"])
        if c["op"] == "clip":
            np.clip(X[:, j], c["lo"], c["hi"], out=X[:, j])
        elif c["op"] == "log10_1p":
            X[:, j] = np.log10(1.0 + X[:, j])
        else:
            raise ValueError(f"unknown conditioning op {c['op']}")
    Xs = (X - mu) / sd

    try:
        blob = torch.load(model_path, map_location="cpu")
    except Exception:
        blob = torch.load(model_path, map_location="cpu", weights_only=False)
    assert blob.get("feature_names", names) == names, "model/norm feature_names disagree"
    model = torch.nn.Sequential(torch.nn.Linear(n_in, 24), torch.nn.ReLU(),
                                torch.nn.Linear(24, 24), torch.nn.ReLU(),
                                torch.nn.Linear(24, 1))
    model.load_state_dict(blob["state_dict"])
    model.eval()
    logits = np.empty(len(Xs), dtype=np.float32)
    bs = 1 << 20
    with torch.no_grad():
        for i in range(0, len(Xs), bs):
            lg = model(torch.tensor(np.ascontiguousarray(Xs[i:i + bs]))).squeeze(1)
            logits[i:i + bs] = lg.tolist()  # CMSSW torch build lacks numpy interop
    out["gate"] = logits
    log(f"chain gate v2 scored: best_epoch={blob.get('best_epoch')} "
        f"val_meta={blob.get('best_val_meta')}")
    return out


# ------------------------------------------------------------------ ntuple side

def load_ntuple(path):
    """Per-sim table (accepted sims, denominator cuts applied) with best-matched
    t5_dnnScore, plus the flat per-object T5 arrays. Also per-event nAccepted and the
    event evt keys, for joining chain simIdx."""
    import uproot

    tree = uproot.open(path)["tree"]
    branches = ["evt", "sim_pt", "sim_eta", "sim_vx", "sim_vy",
                "sim_t5IdxAll", "sim_t5IdxAllFrac", "t5_dnnScore", "t5_isFake"]
    a = tree.arrays(branches, library="np")

    n_evt = len(a["evt"])
    sim_rows = []  # (evt, pt, eta, vxy, bestT5)
    t5_score_all, t5_fake_all, t5_evt_all = [], [], []
    n_acc = np.empty(n_evt, dtype=np.int64)
    n_dropped = 0
    for i in range(n_evt):
        pt = np.asarray(a["sim_pt"][i], dtype=np.float32)
        eta = np.asarray(a["sim_eta"][i], dtype=np.float32)
        vx = np.asarray(a["sim_vx"][i], dtype=np.float32)
        vy = np.asarray(a["sim_vy"][i], dtype=np.float32)
        vxy = np.sqrt(vx * vx + vy * vy)
        n_acc[i] = len(pt)
        score = np.asarray(a["t5_dnnScore"][i], dtype=np.float32)
        fake = np.asarray(a["t5_isFake"][i], dtype=np.int8)
        # Per-object arrays keep finite scores only (1 NaN dnnScore in 2.2M objects
        # on this sample; it is a fake and is dropped + counted).
        fin = np.isfinite(score)
        t5_score_all.append(score[fin])
        t5_fake_all.append(fake[fin])
        t5_evt_all.append(np.full(int(fin.sum()), a["evt"][i], dtype=np.uint64))
        n_dropped += int((~fin).sum())

        idx_all = a["sim_t5IdxAll"][i]
        frac_all = a["sim_t5IdxAllFrac"][i]
        # NaN = "no matched T5": NaN fails every >= threshold comparison, including
        # -inf, so eff_at works uniformly (a -inf sentinel would pass thr = -inf).
        best = np.full(len(pt), np.nan, dtype=np.float64)
        for s in range(len(pt)):
            idxs = np.asarray(idx_all[s], dtype=np.int64)
            if len(idxs) == 0:
                continue
            fr = np.asarray(frac_all[s], dtype=np.float32)
            m = fr > MATCH_FRAC
            if m.any():
                sm = score[idxs[m]]
                sm = sm[np.isfinite(sm)]
                if len(sm):
                    best[s] = sm.max()
        ev = np.full(len(pt), a["evt"][i], dtype=np.uint64)
        sim_rows.append((ev, pt, eta, vxy, best))

    sims = {
        "evt": np.concatenate([r[0] for r in sim_rows]),
        "pt": np.concatenate([r[1] for r in sim_rows]),
        "eta": np.concatenate([r[2] for r in sim_rows]),
        "vxy": np.concatenate([r[3] for r in sim_rows]),
        "best_t5": np.concatenate([r[4] for r in sim_rows]),
    }
    t5 = {
        "evt": np.concatenate(t5_evt_all),
        "score": np.concatenate(t5_score_all),
        "fake": np.concatenate(t5_fake_all),
    }
    evt_keys = a["evt"].astype(np.uint64)
    offsets = np.zeros(n_evt + 1, dtype=np.int64)
    offsets[1:] = np.cumsum(n_acc)
    log(f"ntuple: {n_evt} events, {offsets[-1]} accepted sims, {len(t5['score'])} T5 "
        f"objects ({n_dropped} non-finite dnnScore dropped)")
    return sims, t5, evt_keys, offsets


def best_chain_per_sim(chains, score_name, evt_keys, offsets, n_sims):
    """Global per-sim best chain score (label 1, accepted-sim simIdx), -inf default.
    simIdx indexes the LST-ntuple sim block of its event (accepted prefix of the full
    tracking-ntuple rows; pileup-only matches have simIdx >= nAccepted and simPt -999,
    they never enter the denominator)."""
    ev_pos = {int(e): i for i, e in enumerate(evt_keys)}
    n_acc = np.diff(offsets)
    # NaN sentinel = "no labeled chain" (see load_ntuple: fails every threshold).
    best = np.full(n_sims, np.nan, dtype=np.float64)
    lab = chains["label"]
    sidx = chains["simIdx"]
    sev = chains["evt"]
    sc = chains[score_name]
    sel = lab == 1
    for e, si, v in zip(sev[sel], sidx[sel], sc[sel]):
        p = ev_pos[int(e)]
        if si < 0 or si >= n_acc[p]:
            continue  # pileup-only match: full-row index beyond the accepted prefix
        g = offsets[p] + si
        if np.isnan(best[g]) or v > best[g]:
            best[g] = v
    return best


# ------------------------------------------------------------------ curves

def eff_at(best_scores, thr):
    return float((best_scores >= thr).mean()) if len(best_scores) else float("nan")


def fake_rate_at(scores, fake, thr):
    m = scores >= thr
    n = int(m.sum())
    return (float(fake[m].sum()) / n if n else float("nan")), n


def thr_for_eff(best_scores, target_eff):
    """Smallest threshold achieving eff >= target on the per-sim best-score set
    (eff(thr) is a step function; k-th largest best score). None if unreachable."""
    n = len(best_scores)
    k = int(np.ceil(target_eff * n - 1e-9))
    finite = np.sort(best_scores[np.isfinite(best_scores)])[::-1]
    if k <= 0:
        return np.inf
    if k > len(finite):
        return None
    return float(finite[k - 1])


def thr_for_fake(scores, fake, target_fr):
    """Loosest (smallest) threshold with fake rate <= target: maximizes efficiency
    subject to the fake constraint. Computed exactly via suffix sums over the sorted
    unique scores. None if even the tightest cut exceeds target."""
    order = np.argsort(scores, kind="mergesort")
    s = scores[order]
    fk = fake[order].astype(np.int64)
    n = len(s)
    tot_fake = np.cumsum(fk[::-1])[::-1]  # fakes with score >= s[i]
    cnt = n - np.arange(n)
    fr = tot_fake / cnt
    # candidate thresholds = each distinct score's first (lowest) position
    first = np.ones(n, dtype=bool)
    first[1:] = s[1:] != s[:-1]
    idxs = np.nonzero(first)[0]
    ok = idxs[fr[idxs] <= target_fr]
    if len(ok) == 0:
        return None
    return float(s[ok[0]])


def curve(scores, fake, strata_best, grid):
    """Threshold-grid curve: per-object fake rate + per-stratum sim efficiencies."""
    out = {"thr": grid.tolist(), "fake_rate": [], "n_pass": []}
    for name in strata_best:
        out[f"eff_{name}"] = []
    for t in grid:
        frv, npass = fake_rate_at(scores, fake, t)
        out["fake_rate"].append(frv)
        out["n_pass"].append(npass)
        for name, bs in strata_best.items():
            out[f"eff_{name}"].append(eff_at(bs, t))
    return out


def auc(scores_pos, scores_neg):
    from sklearn.metrics import roc_auc_score
    if len(scores_pos) == 0 or len(scores_neg) == 0:
        return None
    y = np.concatenate([np.ones(len(scores_pos)), np.zeros(len(scores_neg))])
    s = np.concatenate([scores_pos, scores_neg])
    return float(roc_auc_score(y, s))


# ------------------------------------------------------------------ study

def run_eventset(tag, evt_sel_sims, evt_sel_t5, evt_sel_ch, sims, t5, chains,
                 best_gate, best_legacy):
    """All numbers for one event set. best_gate/best_legacy are FULL-LENGTH per-sim
    arrays (event selection enters through the strata masks). Returns a json-ready
    dict and prints a table."""
    den = (sims["pt"] > PT_MIN) & (np.abs(sims["eta"]) < ETA_MAX) & evt_sel_sims
    strata = {
        "prompt": den & (sims["vxy"] < 1.0),
        "disp1": den & (sims["vxy"] >= 1.0),
        "disp5": den & (sims["vxy"] >= 5.0),
    }
    n_str = {k: int(v.sum()) for k, v in strata.items()}

    t5_scores = t5["score"][evt_sel_t5]
    t5_fake = t5["fake"][evt_sel_t5].astype(np.int64)
    res = {"n_sims": n_str, "n_t5_objects": int(len(t5_scores)),
           "n_chain_objects": int(evt_sel_ch.sum())}

    # ---- t5dnn operating point (threshold -inf == production WP acceptance) ----
    op = {"fake_rate": float(t5_fake.mean())}
    t5_best = {k: sims["best_t5"][m] for k, m in strata.items()}
    for k in strata:
        op[f"eff_{k}"] = eff_at(t5_best[k], -np.inf)
    res["t5_operating_point"] = op

    scores_ch = {"gate": chains["gate"][evt_sel_ch], "legacy": chains["legacy"][evt_sel_ch]}
    fake_ch = (chains["label"][evt_sel_ch] == 0).astype(np.int64)
    best_ch = {"gate": best_gate, "legacy": best_legacy}

    print(f"\n===== event set: {tag} =====")
    print(f"denominator sims: prompt={n_str['prompt']} disp1={n_str['disp1']} "
          f"disp5={n_str['disp5']} | t5 objects={len(t5_scores)} "
          f"chain objects (nLayers>=5)={int(evt_sel_ch.sum())}")
    print(f"t5dnn operating point: FR={op['fake_rate']:.4f} "
          f"eff prompt={op['eff_prompt']:.4f} disp1={op['eff_disp1']:.4f} "
          f"disp5={op['eff_disp5']:.4f}")

    # ---- kill criteria per chain score ----
    for sname in ("gate", "legacy"):
        sc = scores_ch[sname]
        bc = {k: best_ch[sname][m] for k, m in strata.items()}
        r = {}

        # K1: equal prompt sim-eff -> compare fake rates
        thr1 = thr_for_eff(bc["prompt"], op["eff_prompt"])
        if thr1 is None:
            r["K1"] = {"reachable": False,
                       "max_prompt_eff": eff_at(bc["prompt"], -np.inf)}
        else:
            fr1, npass1 = fake_rate_at(sc, np.asarray(fake_ch), thr1)
            r["K1"] = {"reachable": True, "thr": thr1,
                       "prompt_eff_achieved": eff_at(bc["prompt"], thr1),
                       "fake_rate": fr1, "n_pass": npass1,
                       "fake_ratio_vs_t5": fr1 / op["fake_rate"],
                       "pass": bool(fr1 <= 1.2 * op["fake_rate"])}

        # K2: equal per-object fake rate -> compare displaced sim-eff
        thr2 = thr_for_fake(sc, np.asarray(fake_ch), op["fake_rate"])
        if thr2 is None:
            r["K2"] = {"reachable": False,
                       "min_fake_rate": fake_rate_at(sc, np.asarray(fake_ch), sc.max())[0]}
        else:
            fr2, npass2 = fake_rate_at(sc, np.asarray(fake_ch), thr2)
            k2 = {"reachable": True, "thr": thr2, "fake_rate_achieved": fr2,
                  "n_pass": npass2}
            for k in ("disp1", "disp5"):
                e = eff_at(bc[k], thr2)
                k2[f"eff_{k}"] = e
                base = op[f"eff_{k}"]
                k2[f"eff_{k}_ratio_vs_t5"] = (e / base if base > 0 else float("inf"))
                k2[f"pass_{k}"] = bool(e >= 1.5 * base)
            k2["eff_prompt"] = eff_at(bc["prompt"], thr2)
            r["K2"] = k2

        # full-range reference: what the chain path reaches with NO threshold
        r["no_threshold"] = {"fake_rate": float(np.asarray(fake_ch).mean())}
        for k in strata:
            r["no_threshold"][f"eff_{k}"] = eff_at(bc[k], -np.inf)

        res[f"chain_{sname}"] = r
        k1, k2 = r["K1"], r["K2"]
        print(f"\n-- chain score: {sname} --")
        print("  no-threshold: FR={fake_rate:.4f} eff prompt={eff_prompt:.4f} "
              "disp1={eff_disp1:.4f} disp5={eff_disp5:.4f}".format(**r["no_threshold"]))
        if k1.get("reachable"):
            print(f"  K1 (equal prompt eff {op['eff_prompt']:.4f} @ thr={k1['thr']:+.3f}): "
                  f"FR={k1['fake_rate']:.4f} ({k1['fake_ratio_vs_t5']:.2f}x t5, "
                  f"limit 1.20x) -> {'PASS' if k1['pass'] else 'FAIL'}")
        else:
            print(f"  K1: UNREACHABLE (max prompt eff {k1['max_prompt_eff']:.4f} "
                  f"< t5 {op['eff_prompt']:.4f}) -> FAIL")
        if k2.get("reachable"):
            print(f"  K2 (equal FR {op['fake_rate']:.4f} @ thr={k2['thr']:+.3f}, "
                  f"FR achieved {k2['fake_rate_achieved']:.4f}):")
            for k in ("disp1", "disp5"):
                print(f"     eff_{k}={k2[f'eff_{k}']:.4f} vs t5 {op[f'eff_{k}']:.4f} "
                      f"({k2[f'eff_{k}_ratio_vs_t5']:.2f}x, need >= 1.50x) -> "
                      f"{'PASS' if k2[f'pass_{k}'] else 'FAIL'}")
            print(f"     (prompt eff at that threshold: {k2['eff_prompt']:.4f})")
        else:
            print(f"  K2: UNREACHABLE (min fake rate {k2['min_fake_rate']:.4f} "
                  f"> t5 {op['fake_rate']:.4f}) -> FAIL")

    # ---- object-level AUCs (true vs fake; chain strata per the train_chain.py
    # convention: true-of-stratum vs ALL fakes, simVxy = -999 for pileup-matched
    # true chains so they land in the "prompt" stratum) ----
    aucs = {}
    t5_true, t5_fk = t5_scores[t5_fake == 0], t5_scores[t5_fake == 1]
    aucs["t5_dnnScore_conditional"] = auc(t5_true, t5_fk)
    ch_vxy = chains["simVxy"][evt_sel_ch]
    for sname in ("gate", "legacy"):
        sc = scores_ch[sname]
        tr, fk = sc[fake_ch == 0], sc[fake_ch == 1]
        aucs[f"chain_{sname}"] = auc(tr, fk)
        is_tr = fake_ch == 0
        aucs[f"chain_{sname}_prompt"] = auc(sc[is_tr & (ch_vxy < 1.0)], fk)
        aucs[f"chain_{sname}_disp1"] = auc(sc[is_tr & (ch_vxy >= 1.0)], fk)
    res["object_auc"] = aucs
    print("\n  object-level AUC (true vs fake): "
          + "  ".join(f"{k}={v:.4f}" for k, v in aucs.items() if v is not None))
    print("  (t5 AUC is CONDITIONAL on production-WP survival -- the collection is"
          " truncated below the WP, so it is not comparable to an untruncated ROC)")

    # ---- stored curves (grid = score quantiles) ----
    res["curves"] = {}
    q = np.linspace(0.0, 1.0, N_GRID)
    grid_t5 = np.unique(np.quantile(t5_scores, q))
    res["curves"]["t5"] = curve(t5_scores, t5_fake, t5_best, grid_t5)
    for sname in ("gate", "legacy"):
        sc = scores_ch[sname]
        grid = np.unique(np.quantile(sc, q))
        bc = {k: best_ch[sname][m] for k, m in strata.items()}
        res["curves"][f"chain_{sname}"] = curve(sc, np.asarray(fake_ch), bc, grid)
    return res


def main():
    args = parse_args()
    np.random.seed(args.seed)

    chains = load_chains(args.chains, args.model, args.norm, args.min_layers)
    sims, t5, evt_keys, offsets = load_ntuple(args.ntuple)
    n_sims = int(offsets[-1])

    assert set(np.unique(chains["evt"]).tolist()) <= set(evt_keys.tolist()), \
        "chain dump evt keys not a subset of ntuple evt keys"

    best_gate = best_chain_per_sim(chains, "gate", evt_keys, offsets, n_sims)
    best_legacy = best_chain_per_sim(chains, "legacy", evt_keys, offsets, n_sims)
    log("per-sim best chain scores built")

    te_keys = test_event_keys(evt_keys, args.seed, args.train_frac, args.val_frac)
    log(f"held-out TEST events: {len(te_keys)} of {len(evt_keys)} (seed {args.seed})")

    results = {"config": {"ntuple": args.ntuple, "chains": args.chains,
                          "model": args.model, "norm": args.norm, "seed": args.seed,
                          "match_frac": MATCH_FRAC, "pt_min": PT_MIN,
                          "eta_max": ETA_MAX, "lambda_len": LAMBDA_LEN,
                          "chain_filter": f"nLayers >= {args.min_layers}",
                          "n_test_events": len(te_keys)}}
    for tag, keys in (("test60", te_keys), ("full300", set(evt_keys.tolist()))):
        in_sims = np.isin(sims["evt"], list(keys))
        in_t5 = np.isin(t5["evt"], list(keys))
        in_ch = np.isin(chains["evt"], list(keys))
        results[tag] = run_eventset(tag, in_sims, in_t5, in_ch, sims, t5, chains,
                                    best_gate, best_legacy)

    with open(args.out, "w") as fh:
        json.dump(results, fh, indent=1)
    log(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
