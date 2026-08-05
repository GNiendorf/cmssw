#!/usr/bin/env python3
"""GEN-C M21 -- DEDICATED BARE-T3 attach-head training.

WHAT IS DIFFERENT FROM train_attach_gen.py (and why each difference exists)
--------------------------------------------------------------------------
1. TARGET UNIVERSE. Bare-T3 pairs ONLY (ttype == 1). The chain-target half of the general
   attach is frozen and validated (the M19 pT5-class line); a head trained to be good at
   3-layer targets must not be allowed to move it. One machine, one enumeration, one
   feature builder -- a per-class head, exactly as the acceptance margin is already
   per-class.
2. LABEL. `labelHit`, the HARNESS truth for the object the pair would DELIVER: strictly
   more than 75% of the merged (pLS pixel hits + T3 OT hits) list from one sim, i.e. the
   rule that literally decides tc_isFake. The dump's legacy `label` (>=2-of-3-MD sim vote
   intersected with the pLS sim set) is a proxy; measured on this dump it is a STRICT
   SUBSET -- 758,249 pairs (4.8% more positives) are harness-true and proxy-fake, and the
   old dump would have downsampled every one of them 1-in-N as a fake.
3. MODEL SELECTION AT THE OPERATING POINT. The shipping r2 head was selected on a
   CHAIN-pair validation AUC. Global AUC is the wrong objective here anyway: the deployed
   margin accepts O(1e-4) of the enumerated pairs, so what matters is the true-positive
   rate in the far-left tail of the ROC. Selection metric = weighted TPR at weighted
   FPR = 3e-5 (the deployed acceptance scale), with the full weighted AUC and the whole
   TPR-at-FPR ladder reported alongside.
4. FEATURES. 25 (PixelAttach.h M21 layout): the 19 legacy slots plus the target T3's own
   DNN scores (19-21), the scale-free radius agreement (22) and the target's innermost
   anchor rt/z (23-24) so the head can judge dPhi/zResid against the geometry they were
   measured at -- the eta-calibration failure the Task-1 curve exposed.

Everything else follows the house discipline: fixed seeds, EVENT-level split asserted
against the frozen test-60 list, conditioning before standardization recorded in the norm
json, mean/std fit on TRAIN rows only, wgt-weighted loss and metrics.
"""
import argparse
import copy
import json
import os
import time

import numpy as np

T0 = time.time()
NFEAT = 25
S = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/'
G = S + 'gen_c_ref/'
FROZEN_TEST60 = S + 'prototype/m12_test60_evts.json'


def log(m):
    print('[%8.1fs] %s' % (time.time() - T0, m), flush=True)


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument('--input', default=G + 'pd_bareT3.root')
    p.add_argument('--out-model', default=G + 'attach_t3_mlp.pt')
    p.add_argument('--out-norm', default=G + 'attach_t3_norm.json')
    p.add_argument('--cache', default=G + 'gc_t3_cache.npz')
    p.add_argument('--seed', type=int, default=42)
    p.add_argument('--epochs', type=int, default=60)
    p.add_argument('--patience', type=int, default=8)
    p.add_argument('--hidden', type=int, default=32)
    p.add_argument('--batch-size', type=int, default=32768)
    p.add_argument('--lr', type=float, default=1e-3)
    p.add_argument('--label', default='labelHit', choices=['labelHit', 'label'])
    p.add_argument('--sel-fpr', type=float, default=3e-5)
    p.add_argument('--nfeat', type=int, default=NFEAT,
                   help='use only the leading N features (19 = the legacy layout, for the '
                        'feature-value ablation)')
    return p.parse_args()


# Conditioning: identical vocabulary to the M16 spec for the shared slots (same heavy
# tails in this dump), plus the two new unbounded geometric columns left raw (they are
# O(1..120) cm, no sentinel) -- dKappaRel and the three DNN scores are bounded already.
CONDITIONING = [
    {'feature': 'af_ptErrRel', 'op': 'log10_1p'},
    {'feature': 'af_circleCenterDist', 'op': 'log10_1p'},
    {'feature': 'af_log10PtIn', 'op': 'clip', 'lo': -1.0, 'hi': 4.0},
    {'feature': 'af_log10CircleRadius', 'op': 'clip', 'lo': 1.0, 'hi': 5.0},
    {'feature': 'af_fitKappaSigned', 'op': 'clip', 'lo': -1.0, 'hi': 1.0},
    {'feature': 'af_dKappa', 'op': 'clip', 'lo': -1.0, 'hi': 1.0},
]


def load(args):
    import uproot
    f = uproot.open(args.input)
    spec = f['feature_spec'].member('fTitle')
    assert spec.startswith('af:')
    names = ['af_' + n for n in spec[3:].split(',')]
    assert len(names) == NFEAT, (len(names), names)
    t = f['pairs']
    n = t.num_entries
    log('dump %s: %d rows' % (os.path.basename(args.input), n))
    meta = t.arrays(['evt', 'label', 'labelHit', 'ttype', 'wgt', 'simVxy', 'simPt',
                     'tgtRow', 'plsRow', 'hitFrac'], library='np')
    X = np.empty((n, NFEAT), dtype=np.float32)
    for j in range(NFEAT):
        a = t.arrays(['af_%02d' % j], library='np')
        X[:, j] = a['af_%02d' % j]
        del a
    return meta, X, names


def main():
    args = parse_args()
    import torch
    from sklearn.metrics import roc_auc_score
    np.random.seed(args.seed)
    torch.manual_seed(args.seed)
    rng = np.random.default_rng(args.seed)
    dev = torch.device('cuda' if torch.cuda.is_available() else 'cpu')

    if os.path.exists(args.cache):
        z = np.load(args.cache, allow_pickle=True)
        C = {k: z[k] for k in z.files}
        names = json.loads(bytes(C['names_json']).decode())
        log('loaded cache %s' % args.cache)
    else:
        meta, X, names = load(args)
        keep = meta['ttype'] == 1
        log('bare-T3 rows %d of %d' % (int(keep.sum()), len(keep)))
        for k in meta:
            meta[k] = meta[k][keep]
        X = X[keep]
        nb = int((~np.isfinite(X)).sum())
        if nb:
            log('WARNING %d non-finite feature values -> 0' % nb)
            np.nan_to_num(X, copy=False, nan=0.0, posinf=0.0, neginf=0.0)
        # label agreement report (the reason labelHit exists)
        pl, hl = meta['label'] == 1, meta['labelHit'] == 1
        log('label vs labelHit: proxy-true %d | harness-true %d | proxy-true&harness-fake %d'
            ' | proxy-fake&harness-true %d' % (int(pl.sum()), int(hl.sum()),
                                               int((pl & ~hl).sum()), int((~pl & hl).sum())))
        for c in CONDITIONING:
            j = names.index(c['feature'])
            if c['op'] == 'clip':
                np.clip(X[:, j], c['lo'], c['hi'], out=X[:, j])
            else:
                X[:, j] = np.log10(1.0 + X[:, j])
        key = meta['evt'].astype(np.uint64)
        uniq = np.unique(key)
        rng.shuffle(uniq)
        ntr, nva = int(round(0.6 * len(uniq))), int(round(0.2 * len(uniq)))
        trk, vak, tek = uniq[:ntr], uniq[ntr:ntr + nva], uniq[ntr + nva:]
        frozen = sorted(int(v) for v in json.load(open(FROZEN_TEST60)))
        assert sorted(int(v) for v in tek) == frozen, 'TEST split is not the frozen test-60'
        log('event split verified against the frozen test-60')
        tr, va, te = np.isin(key, trk), np.isin(key, vak), np.isin(key, tek)
        mu = X[tr].mean(axis=0, dtype=np.float64).astype(np.float32)
        sd = X[tr].std(axis=0, dtype=np.float64).astype(np.float32)
        sd[sd < 1e-8] = 1.0
        X -= mu
        X /= sd
        y = (meta[args.label] == 1).astype(np.float32)
        C = dict(Xtr=X[tr], ytr=y[tr], wtr=meta['wgt'][tr],
                 Xva=X[va], yva=y[va], wva=meta['wgt'][va], etava=X[va][:, 0] * 0,
                 Xte=X[te], yte=y[te], wte=meta['wgt'][te],
                 vxy_te=meta['simVxy'][te], pt_te=meta['simPt'][te],
                 rtin_va=(X[va][:, 23] * sd[23] + mu[23]),
                 rtin_te=(X[te][:, 23] * sd[23] + mu[23]),
                 zin_te=(X[te][:, 24] * sd[24] + mu[24]),
                 mu=mu, sd=sd,
                 names_json=np.bytes_(json.dumps(names)))
        np.savez(args.cache, **C)
        log('wrote cache %s (%.2f GB)' % (args.cache, os.path.getsize(args.cache) / 1e9))

    NF = args.nfeat
    Xtr = torch.tensor(np.ascontiguousarray(C['Xtr'][:, :NF]))
    ytr = torch.tensor(np.ascontiguousarray(C['ytr']))
    wtr = torch.tensor(np.ascontiguousarray(C['wtr']))
    Xva_np, yva, wva = C['Xva'][:, :NF], C['yva'], C['wva']
    Xva = torch.tensor(np.ascontiguousarray(Xva_np))
    Wpos = float((C['wtr'] * C['ytr']).sum())
    Wneg = float((C['wtr'] * (1 - C['ytr'])).sum())
    pw = Wneg / max(Wpos, 1.0)
    log('train %d rows | weighted %.4g true / %.4g fake -> pos_weight %.3f'
        % (len(Xtr), Wpos, Wneg, pw))
    Xtr, ytr, wtr = Xtr.to(dev), ytr.to(dev), wtr.to(dev)

    model = torch.nn.Sequential(torch.nn.Linear(NF, args.hidden), torch.nn.ReLU(),
                                torch.nn.Linear(args.hidden, args.hidden), torch.nn.ReLU(),
                                torch.nn.Linear(args.hidden, 1)).to(dev)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    crit = torch.nn.BCEWithLogitsLoss(pos_weight=torch.tensor(pw, device=dev), reduction='none')
    gen = torch.Generator(device='cpu').manual_seed(args.seed)

    def scores(Xt):
        model.eval()
        out = np.empty(len(Xt), dtype=np.float32)
        with torch.no_grad():
            for i in range(0, len(Xt), 1 << 21):
                z = model(Xt[i:i + (1 << 21)].to(dev)).squeeze(1).float().cpu()
                out[i:i + (1 << 21)] = np.asarray(z.tolist(), dtype=np.float32)
        return out

    def tpr_at_fpr(s, y, w, fprs):
        """Weighted TPR at a set of weighted FPR targets. One sort, no sklearn curve."""
        o = np.argsort(-s)
        so, yo, wo = s[o], y[o], w[o]
        cp = np.cumsum(wo * yo)
        cn = np.cumsum(wo * (1 - yo))
        P, N = cp[-1], cn[-1]
        out = {}
        for f in fprs:
            i = np.searchsorted(cn, f * N)
            i = min(i, len(cn) - 1)
            out[f] = float(cp[i] / P)
        return out

    FPRS = [1e-5, 3e-5, 1e-4, 3e-4, 1e-3]
    best, best_ep, best_state, bad = -1.0, -1, None, 0
    for ep in range(1, args.epochs + 1):
        model.train()
        perm = torch.randperm(len(Xtr), generator=gen).to(dev)
        tot, totw = 0.0, 0.0
        for i in range(0, len(Xtr), args.batch_size):
            idx = perm[i:i + args.batch_size]
            xb, yb, wb = Xtr[idx], ytr[idx], wtr[idx]
            opt.zero_grad()
            z = model(xb).squeeze(1)
            l = (crit(z, yb) * wb).sum() / wb.sum()
            l.backward()
            opt.step()
            tot += float(l.detach()) * float(wb.sum())
            totw += float(wb.sum())
        sv = scores(Xva)
        auc = float(roc_auc_score(yva, sv, sample_weight=wva))
        tp = tpr_at_fpr(sv, yva, wva, FPRS)
        sel = tp[args.sel_fpr]
        log('epoch %2d loss %.5f val_auc %.5f | TPR@FPR %s | SEL %.5f'
            % (ep, tot / totw, auc, ' '.join('%.0e:%.4f' % (f, tp[f]) for f in FPRS), sel))
        if sel > best:
            best, best_ep, bad = sel, ep, 0
            best_state = copy.deepcopy({k: v.cpu() for k, v in model.state_dict().items()})
        else:
            bad += 1
            if bad >= args.patience:
                log('early stop at epoch %d (best SEL %.5f @ %d)' % (ep, best, best_ep))
                break
    model.load_state_dict(best_state)
    model.to(dev)

    Xte = torch.tensor(np.ascontiguousarray(C['Xte'][:, :NF]))
    st = scores(Xte)
    yte, wte = C['yte'], C['wte']
    auc = float(roc_auc_score(yte, st, sample_weight=wte))
    tp = tpr_at_fpr(st, yte, wte, FPRS)
    print()
    print('=== GEN-C M21 bare-T3 head on the FROZEN TEST-60 (nfeat=%d, label=%s) ==='
          % (NF, args.label))
    print('  weighted AUC %.5f   (best val SEL TPR@%.0e = %.5f at epoch %d)'
          % (auc, args.sel_fpr, best, best_ep))
    for f in FPRS:
        print('    TPR @ weighted FPR %.0e : %.5f' % (f, tp[f]))
    # per-region operating-point report -- the Task-1 eta-calibration defect
    rt = C['rtin_te']
    zt = C['zin_te']
    theta = np.degrees(np.arctan2(np.maximum(rt, 1e-6), np.abs(zt)))
    # barrel-ish / transition / endcap by the target's innermost anchor geometry
    for nm, m in (('barrel-like  (|z|<40 cm)', np.abs(zt) < 40),
                  ('transition   (40-90)', (np.abs(zt) >= 40) & (np.abs(zt) < 90)),
                  ('endcap-like  (|z|>90)', np.abs(zt) >= 90)):
        if m.sum() == 0:
            continue
        a = float(roc_auc_score(yte[m], st[m], sample_weight=wte[m])) if yte[m].max() > 0 else -1
        t2 = tpr_at_fpr(st[m], yte[m], wte[m], [3e-5, 1e-4])
        print('    %-24s rows %9d AUC %.5f TPR@3e-5 %.4f TPR@1e-4 %.4f'
              % (nm, int(m.sum()), a, t2[3e-5], t2[1e-4]))
    del theta
    for nm, lo, hi in (('prompt vxy<1', 0.0, 1.0), ('displaced vxy 1-5', 1.0, 5.0),
                       ('displaced vxy >=5', 5.0, 1e9)):
        m = (yte == 1) & (C['vxy_te'] >= lo) & (C['vxy_te'] < hi) & (C['pt_te'] > -998)
        fk = yte == 0
        if m.sum() == 0:
            continue
        yy = np.concatenate([np.ones(int(m.sum())), np.zeros(int(fk.sum()))])
        ss = np.concatenate([st[m], st[fk]])
        ww = np.concatenate([wte[m], wte[fk]])
        print('    %-24s n_true %8d AUC %.5f' % (nm, int(m.sum()),
                                                 float(roc_auc_score(yy, ss, sample_weight=ww))))

    import torch as _t
    _t.save({'state_dict': {k: v.cpu() for k, v in model.state_dict().items()},
             'arch': [NF, args.hidden, args.hidden, 1], 'feature_names': names[:NF],
             'seed': args.seed, 'conditioning': CONDITIONING, 'label': args.label,
             'best_epoch': best_ep, 'best_val_sel': best, 'sel_fpr': args.sel_fpr,
             'test_auc': auc}, args.out_model)
    json.dump({'feature_names': names[:NF], 'conditioning': CONDITIONING,
               'mean': C['mu'][:NF].tolist(), 'std': C['sd'][:NF].tolist(),
               'seed': args.seed, 'label': args.label,
               'selection': {'metric': 'weighted TPR at weighted FPR %g' % args.sel_fpr,
                             'value': best, 'epoch': best_ep},
               'test': {'auc': auc, 'tpr_at_fpr': {str(f): tp[f] for f in FPRS}}},
              open(args.out_norm, 'w'), indent=1)
    log('saved %s and %s' % (args.out_model, args.out_norm))


main()
