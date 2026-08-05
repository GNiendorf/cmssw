#!/usr/bin/env python3
"""M16 head-to-head control: the M7 CHAIN-ONLY attach head (18 inputs, test AUC 0.99851
on the M7 h4b-shape dump) vs the M16 GENERAL head (19 inputs) scored on the SAME rows.

The published 0.99851 is not directly comparable to anything measured here: it was
measured on a different target universe (the M7 h4b chain shape, ~660 accepted 5+ chains
per event) with a different fake population. The only fair statement about "does the
general head cost us anything on chain pairs" is BOTH HEADS ON THE SAME ROWS -- the
CHAIN (ttype 0) rows of the frozen test-60 events of the M16 general dump. That is what
this script measures.

Both heads consume af_00..af_17 with the SAME conditioning op vocabulary; the general
head additionally consumes af_18 (targetType) and refits mean/std on the general
population, and adds the M16 curvature-sentinel clips. Each head is fed through its own
norm json, so each is evaluated exactly as its own C++ port would score it.

  python3 compare_v1_control.py --gen-model attach_mlp_g1.pt --gen-norm attach_norm_g1.json
"""

import argparse
import glob
import json
import os
import sys
import time

import numpy as np

T0 = time.time()
PROTO_DIR = os.path.dirname(os.path.abspath(__file__))
FROZEN_TEST60 = ("/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/"
                 "standalone/prototype/m12_test60_evts.json")


def log(m):
    print(f"[{time.time() - T0:7.1f}s] {m}", flush=True)


def parse_args():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--input", default=f"{PROTO_DIR}/pairs_gen_c*.root")
    p.add_argument("--v1-model", default=f"{PROTO_DIR}/attach_mlp_v1.pt")
    p.add_argument("--v1-norm", default=f"{PROTO_DIR}/attach_norm_v1.json")
    p.add_argument("--gen-model", default=f"{PROTO_DIR}/attach_mlp_g1.pt")
    p.add_argument("--gen-norm", default=f"{PROTO_DIR}/attach_norm_g1.json")
    p.add_argument("--out", default=f"{PROTO_DIR}/attach_v1_control.json")
    return p.parse_args()


def score(model_path, norm_path, Xraw, names_all, ttype):
    import torch
    with open(norm_path) as fh:
        norm = json.load(fh)
    names = norm["feature_names"]
    n_in = len(names)
    assert names == names_all[:n_in], "feature-name prefix mismatch"
    X = Xraw[:, :n_in].copy()
    for c in norm.get("conditioning") or []:
        j = names.index(c["feature"])
        if c["op"] == "clip":
            np.clip(X[:, j], c["lo"], c["hi"], out=X[:, j])
        elif c["op"] == "log10_1p":
            X[:, j] = np.log10(1.0 + X[:, j])
    X -= np.asarray(norm["mean"], dtype=np.float32)
    X /= np.asarray(norm["std"], dtype=np.float32)
    blob = torch.load(model_path, map_location="cpu", weights_only=False)
    arch = blob["arch"]
    n_hid, n_out = arch[1], arch[3]
    model = torch.nn.Sequential(torch.nn.Linear(n_in, n_hid), torch.nn.ReLU(),
                                torch.nn.Linear(n_hid, n_hid), torch.nn.ReLU(),
                                torch.nn.Linear(n_hid, n_out))
    model.load_state_dict(blob["state_dict"])
    model.eval()
    out = np.empty(len(X), dtype=np.float32)
    with torch.no_grad():
        for i in range(0, len(X), 1 << 20):
            z = np.asarray(model(torch.tensor(np.ascontiguousarray(X[i:i + (1 << 20)]))).tolist(),
                           dtype=np.float32)
            out[i:i + (1 << 20)] = z[np.arange(len(z)), ttype[i:i + (1 << 20)].astype(np.int64)] \
                if n_out > 1 else z[:, 0]
    return out, n_in


def wauc(s, y, w):
    from sklearn.metrics import roc_auc_score
    if y.sum() == 0 or (1 - y).sum() == 0:
        return None
    return float(roc_auc_score(y, s, sample_weight=w))


def main():
    args = parse_args()
    import uproot

    with open(FROZEN_TEST60) as fh:
        te_keys = np.asarray(sorted(int(v) for v in json.load(fh)), dtype=np.uint64)

    files = sorted(glob.glob(args.input))
    spec = uproot.open(files[0])["feature_spec"].member("fTitle")
    names_all = [f"af_{n}" for n in spec[3:].split(",")]
    n_feat = len(names_all)
    feat_branches = [f"af_{i:02d}" for i in range(n_feat)]

    Xs, labs, wgs, vxys, pts, tts = [], [], [], [], [], []
    for path in files:
        t = uproot.open(path)["pairs"]
        m = t.arrays(["evt", "ttype", "label", "wgt", "simVxy", "simPt"], library="np")
        keep = np.isin(m["evt"].astype(np.uint64), te_keys) & (m["ttype"] == 0)
        if not keep.any():
            continue
        a = t.arrays(feat_branches, library="np")
        X = np.empty((int(keep.sum()), n_feat), dtype=np.float32)
        for j, b in enumerate(feat_branches):
            X[:, j] = a[b][keep]
        del a
        Xs.append(X)
        labs.append(m["label"][keep])
        wgs.append(m["wgt"][keep])
        vxys.append(m["simVxy"][keep])
        pts.append(m["simPt"][keep])
        tts.append(m["ttype"][keep])
        log(f"  {os.path.basename(path)}: +{int(keep.sum())} test chain rows")
    X = np.concatenate(Xs)
    lab = np.concatenate(labs).astype(np.int32)
    wgt = np.concatenate(wgs).astype(np.float64)
    vxy = np.concatenate(vxys)
    spt = np.concatenate(pts)
    tt = np.concatenate(tts).astype(np.int32)
    log(f"TEST-60 CHAIN rows: {len(X)} ({int((lab == 1).sum())} true)")
    assert (tt == 0).all()

    s_v1, n_v1 = score(args.v1_model, args.v1_norm, X, names_all, tt)
    log(f"scored v1 chain-only head ({n_v1} inputs)")
    s_g1, n_g1 = score(args.gen_model, args.gen_norm, X, names_all, tt)
    log(f"scored general head ({n_g1} inputs)")

    y = (lab == 1).astype(np.float64)
    res = {}
    print("\n=== CHAIN pairs of the FROZEN TEST-60 -- same rows, two heads ===")
    print(f"{'stratum':>26} {'n_true':>8} {'n_fake':>9} {'v1(18)':>9} {'gen(19)':>9} {'delta':>9}")

    def row(tag, mtrue):
        mf = lab != 1
        sel = mtrue | mf
        yy, ww = y[sel], wgt[sel]
        a1 = wauc(s_v1[sel], yy, ww)
        a2 = wauc(s_g1[sel], yy, ww)
        d = None if (a1 is None or a2 is None) else a2 - a1
        print(f"{tag:>26} {int(mtrue.sum()):>8d} {int(mf.sum()):>9d} "
              f"{'n/a' if a1 is None else f'{a1:.5f}':>9} "
              f"{'n/a' if a2 is None else f'{a2:.5f}':>9} "
              f"{'n/a' if d is None else f'{d:+.5f}':>9}")
        res[tag] = {"n_true": int(mtrue.sum()), "n_fake": int(mf.sum()),
                    "auc_v1": a1, "auc_gen": a2,
                    "delta": None if d is None else float(d)}

    is_true = lab == 1
    acc = is_true & (spt > -998.0)
    row("ALL chain pairs", is_true)
    row("prompt vxy<1", acc & (vxy < 1))
    row("displaced vxy [1,5)", acc & (vxy >= 1) & (vxy < 5))
    row("displaced vxy >=5", acc & (vxy >= 5))
    row("displaced vxy >=1 (all)", acc & (vxy >= 1))
    row("pileup-sim true", is_true & ~acc)

    with open(args.out, "w") as fh:
        json.dump({"v1_model": args.v1_model, "gen_model": args.gen_model,
                   "rows": int(len(X)), "test": res,
                   "note": "M7 published chain-only test AUC 0.99851 was measured on the "
                           "M7 h4b-shape dump (different target universe); the only fair "
                           "comparison is these same-row numbers."}, fh, indent=1)
    log(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
