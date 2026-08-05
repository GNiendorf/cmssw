#!/usr/bin/env python3
"""M19 edge-head EQUAL-KILL calibration + frozen-test-60 score anatomy.

Reproduces the frozen test-60 event set from the PRIMARY dump alone (the M8 rule
makes the test keys a function of edges_300evt.root + seed only: the first use of
the seed-42 rng is the shuffle of the primary file's unique (lumi,evt) keys), scores
the resident v3 head and each retrained variant on those rows, and reports:

  * the -e threshold that reproduces v3's ALL-EDGE pass fraction exactly
    (EQUAL-KILL recalibration: it removes any global logit-scale shift introduced
    by the retrain and leaves only the selective re-ranking),
  * true/fake pass rates by (vxy class x etype) at -e 0 AND at the equal-kill -e.
"""
import argparse
import json
import os
import sys

import numpy as np

D = os.path.dirname(os.path.abspath(__file__))
PROTO = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype"
N_NODE, N_EDGE = 13, 14


def load(path, seed=42, train_frac=0.6, val_frac=0.2):
    import uproot
    f = uproot.open(path)
    spec = f["feature_spec"].member("fTitle")
    ni, ef = spec.split(";")
    node_names = ni.split(":")[1].split(",")
    edge_names = ef.split(":")[1].split(",")
    names = ([f"ni_{n}" for n in node_names] + [f"no_{n}" for n in node_names]
             + [f"ef_{n}" for n in edge_names])
    br = ([f"ni_{i:02d}" for i in range(N_NODE)] + [f"no_{i:02d}" for i in range(N_NODE)]
          + [f"ef_{i:02d}" for i in range(N_EDGE)])
    t = f["edges"]
    a = t.arrays(["evt", "lumi", "etype", "label", "simVxy", "simPt"] + br, library="np")
    key = (a["lumi"].astype(np.uint64) << np.uint64(32)) | a["evt"].astype(np.uint64)
    uniq = np.unique(key)
    rng = np.random.default_rng(seed)
    rng.shuffle(uniq)
    n = len(uniq)
    te_keys = uniq[int(round(train_frac * n)) + int(round(val_frac * n)):]
    te = np.isin(key, te_keys)
    print(f"frozen test-60: {len(te_keys)} events, {int(te.sum())} edges")
    X = np.empty((int(te.sum()), len(br)), dtype=np.float32)
    for j, b in enumerate(br):
        X[:, j] = a[b][te]
    return X, names, {k: a[k][te] for k in ("etype", "label", "simVxy", "simPt")}


def score(model_path, norm_path, X_raw, names):
    import torch
    norm = json.load(open(norm_path))
    assert norm["feature_names"] == names
    X = X_raw.copy()
    for c in norm.get("conditioning") or []:
        j = names.index(c["feature"])
        if c["op"] == "clip":
            np.clip(X[:, j], c["lo"], c["hi"], out=X[:, j])
        else:
            X[:, j] = np.log10(1.0 + X[:, j])
    X = (X - np.asarray(norm["mean"], np.float32)) / np.asarray(norm["std"], np.float32)
    blob = torch.load(model_path, map_location="cpu", weights_only=False)
    m = torch.nn.Sequential(torch.nn.Linear(len(names), 32), torch.nn.ReLU(),
                            torch.nn.Linear(32, 32), torch.nn.ReLU(),
                            torch.nn.Linear(32, 1))
    m.load_state_dict(blob["state_dict"])
    m.eval()
    out = np.empty(len(X), dtype=np.float32)
    with torch.no_grad():
        for i in range(0, len(X), 1 << 20):
            out[i:i + (1 << 20)] = m(torch.tensor(np.ascontiguousarray(
                X[i:i + (1 << 20)]))).squeeze(1).tolist()
    return out


BANDS = [("unknown vxy(-999)", lambda v: v < -900),
         ("prompt [0,1)", lambda v: (v >= 0) & (v < 1)),
         ("displ [1,5)", lambda v: (v >= 1) & (v < 5)),
         ("displ >=5", lambda v: v >= 5)]


def anatomy(tag, s, meta, thr):
    lab, vxy, ety = meta["label"], meta["simVxy"], meta["etype"]
    print(f"\n  --- {tag} at -e {thr:+.4f} ---")
    print(f"  {'band':>18} {'et':>3} {'n_true':>9} {'pass_true':>10} "
          f"{'n_fake':>9} {'pass_fake':>10}")
    rows = {}
    for bname, fn in BANDS:
        mb = fn(vxy)
        for et in (1, 2):
            m = mb & (ety == et)
            nt = int((m & (lab == 1)).sum())
            nf = int((m & (lab == 0)).sum())
            pt = float((s[m & (lab == 1)] >= thr).mean()) if nt else 0.0
            pf = float((s[m & (lab == 0)] >= thr).mean()) if nf else 0.0
            print(f"  {bname:>18} {'E' + str(et):>3} {nt:>9d} {pt:>10.4f} {nf:>9d} {pf:>10.4f}")
            rows[f"{bname}|E{et}"] = {"n_true": nt, "pass_true": pt,
                                      "n_fake": nf, "pass_fake": pf}
    allp = float((s >= thr).mean())
    tp = float((s[lab == 1] >= thr).mean())
    fp = float((s[lab == 0] >= thr).mean())
    print(f"  {'ALL':>18} {'':>3} {int((lab == 1).sum()):>9d} {tp:>10.4f} "
          f"{int((lab == 0).sum()):>9d} {fp:>10.4f}   all_pass={allp:.5f}")
    rows["_all"] = {"all_pass": allp, "true_pass": tp, "fake_pass": fp, "thr": thr}
    return rows


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--dump", default=f"{PROTO}/edges_300evt.root")
    p.add_argument("--variants", nargs="+", default=["d1", "d2", "d3"])
    p.add_argument("--out", default=f"{D}/er_edge_calib.json")
    a = p.parse_args()

    X, names, meta = load(a.dump)
    res = {}
    s3 = score(f"{D}/edge_mlp_v3.pt", f"{D}/edge_norm_v3.json", X, names)
    ref_pass = float((s3 >= 0.0).mean())
    print(f"\nRESIDENT v3 ALL-edge pass fraction at -e 0: {ref_pass:.6f}")
    res["v3"] = {"e0": anatomy("v3", s3, meta, 0.0), "equal_kill_e": 0.0,
                 "ref_all_pass": ref_pass}
    for tag in a.variants:
        mp, npth = f"{D}/er_edge_mlp_{tag}.pt", f"{D}/er_edge_norm_{tag}.json"
        if not os.path.exists(mp):
            print(f"skip {tag}: no model")
            continue
        s = score(mp, npth, X, names)
        # equal-kill threshold: the quantile of the new scores at the v3 pass fraction
        thr = float(np.quantile(s, 1.0 - ref_pass, method="linear"))
        print(f"\n=== {tag}: equal-kill -e = {thr:+.4f} "
              f"(pass at -e 0 = {(s >= 0).mean():.6f} vs v3 {ref_pass:.6f}) ===")
        res[tag] = {"e0": anatomy(tag, s, meta, 0.0),
                    "equal_kill": anatomy(tag, s, meta, thr),
                    "equal_kill_e": thr,
                    "pass_at_e0": float((s >= 0).mean())}
    json.dump(res, open(a.out, "w"), indent=1)
    print(f"\nwrote {a.out}")


if __name__ == "__main__":
    sys.exit(main())
