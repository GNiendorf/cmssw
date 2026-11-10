# Torch pixel-track classifier for the CA without the outer-tracker extension

The Patatrack CA (`CAHitNtupletAlpakaPhase2OT`) with the OT extension switched off produces pixel-only tuples,
including three-hit tuples from the barrel-terminal path. `PixelTrackTorchHighPuritySelector` scores every
preselected tuple with a small MLP and keeps it when the score passes the working point of its
(pT, |eta|) cell; tuples with exactly three hits have their own row of cuts. LST and the mkFit seed hand-off receive
every passing tuple. The pixel vertices (and through them the tau sequence) read `hltPhase2PixelTracksHighPurity`:
the passing tuples with at least four hits and a score above one high-purity cut, fitted to the extension menu's
pixel-track purity.

This directory holds everything needed to remake the model from the deployed chain, on-policy:

| File | Role |
|---|---|
| `samples/train_{ttbar,qcd}.lfn` | the RAW files streamed for training (all their events) |
| `samples/excluded_files.txt` | files never used: dev, holdout, every val5k list, the older holdout parents |
| `cmssw/LocalVal/PttTruth` | `CompactClusterTruthProducer`: cluster -> TrackingParticle truth keyed by cluster position |
| `cmssw/LocalVal/PttDump` | `PixelTupleTorchDump`: one row per preselected tuple (inputs, deployed decisions, layer pattern, truth) |
| `cfg/cache_cfg.py` | stream one RAW file into a compact local-reco + truth cache |
| `cfg/dump_cfg.py` | on-policy dump from the cache with the classifier cut off |
| `cfg/policy_op.py`, `cfg/pix_only.py` | the CA policy the model was trained on (OP, optionally with the forward skip pairs) |
| `cfg/deploy_block.py` | `pttDeploy(process, model, wp_json)`: model, tables, high-purity subset, vertex input |
| `pttlib.py` | dump reading, event split, preprocessing, network, deployed TorchScript form |
| `train.py` | training (recipe below) |
| `export.py` | TorchScript export, standardisation folded into the first layer, offline score check |
| `fit_wp.py` | per-(pT, |eta|) working points and the high-purity cut |
| `parity.py` | C++ parity: in-menu decisions of the shipped settings against the offline decisions |
| `scripts/run_all.sh` | the whole chain in one command |

## Recipe (mirrors the chain attach head, `standalone/attach_report_ref/REPORT_ATTACH.tex`)

- Rows: every tuple the selector preselects (CA quality at least tight), dumped from the deployed chain with the
  classifier cut off and no preselection cap. Rows with non-finite inputs are dropped (the selector rejects them).
- Label: the tuple's best TrackingParticle holds at least 75% of its hits (MTV's hit association, signal and pileup
  TrackingParticles; pileup counts as true, as in MTV's fake rate).
- Inputs: the selector's 17 fit and covariance features. `log10(1 + |x| / m)` with the sign of x (m = training
  median of |x|) on the heavy-tailed inputs: those whose 99.9% / 50% quantile ratio of |x| exceeds 10 (angles and
  hit/layer counts excluded). Then clip to the training 0.05% / 99.95% quantiles and standardise; the
  standardisation is folded into the first linear layer at export.
- Network: 17 -> 32 -> 32 -> 1, ReLU, sigmoid output. FP32 inference in the selector (`inferenceHalf = False`).
- Split: 60 / 20 / 20 by event (hash of sample, run, lumi, event).
- Weights: true tuples with vxy 1-5 cm x8, vxy >= 5 cm x16; the QCD sample scaled to 25% of the training weight;
  class balance by pos_weight (fake weight / true weight); focal factor sigmoid(z)^2 on fakes.
- Optimiser: Adam, lr 3e-3 with a cosine decay to 1e-5 per step, batch 16384, 200 epochs; the kept epoch maximises
  the worst per-sample validation AUC (ttbar, QCD).
- Working points: per (pT, |eta|) cell with pT edges 2 / 5 / 10 / 25 / 50 GeV and |eta| edges 1.1 / 1.7 / 2.5, one
  table for tuples with four or more hits (eps4) and one for three-hit tuples (eps3); each cell is the (1 - eps)
  quantile of the scores of true prompt tuples (vxy < 1 cm) on the training + validation events. A cell with fewer
  than 30 such tuples takes the cut of the next lower pT cell of its |eta| band. High pT is handled by these rows, not
  by loss weights. The final eps4 / eps3 are chosen by the end-to-end MTV scan, not by offline fractions.
- High-purity cut: the lowest single cut on tuples with at least four hits whose passing-fake fraction is at most
  3% in both samples (the extension menu's pixel-track fake rate is 3.3% / 3.4% in MTV).

## Reproduce

Environment: `source /cvmfs/cms.cern.ch/cmsset_default.sh; export SCRAM_ARCH=el9_amd64_gcc14`, IB
`CMSSW_20_1_X_2026-11-02-1100`, an area with this branch's packages plus `cmssw/LocalVal/{PttTruth,PttDump}` copied
to `src/LocalVal/` and built (`scram b`), a grid proxy for the streaming (X509_USER_PROXY).

```
export PTT_AREA=<area> PTT_MENU=<full-menu CPU config with the CA policy and VALIDATION, e.g. op57_cpu.py>
export PTT_LFN_TTBAR=$PWD/samples/train_ttbar.lfn PTT_LFN_QCD=$PWD/samples/train_qcd.lfn
export PTT_EPS4=0.995 PTT_EPS3=0.99 PTT_DEVICE=cuda
scripts/run_all.sh <workdir>          # cache -> dumps -> train -> export -> fit_wp -> install -> parity
```

`PTT_CACHE_ROOT=<dir with ttbar/ and qcd/ caches>` skips the streaming. The menu config is the one thing that
defines the policy: to retrain on-policy for another CA, rebuild `PTT_MENU` for that CA and rerun. The menu config
used here is `cmsDriver.py Phase2 -s L1P2GT,HLT:75e33_timing,VALIDATION::hltMultiTrackValidation+hltMultiPVValidation
--procModifiers trackingMkFitFit,trackingMkFitAlpaka --era Phase2C22I13M9 --geometry ExtendedRun4D121
--conditions auto:phase2_realistic_T35 --customise SLHCUpgradeSimulations/Configuration/aging.customise_aging_1000
--accelerators cpu ...` followed by `customizeOP(process, keepForwardSkipPairs=True)` from `cfg/policy_op.py`.

RESULTS: see the section appended below by the training lane.
