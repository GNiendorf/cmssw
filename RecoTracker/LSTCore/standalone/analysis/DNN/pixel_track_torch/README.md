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
| `samples/excluded_files.list` | files never used: dev, holdout, every val5k list, the older holdout parents |
| `cmssw/LocalVal/PttTruth` | `CompactClusterTruthProducer`: cluster -> TrackingParticle truth keyed by cluster position |
| `cmssw/LocalVal/PttDump` | `PixelTupleTorchDump`: one row per preselected tuple (inputs, deployed decisions, layer pattern, truth) |
| `cfg/cache_cfg.py` | stream one RAW file into a compact local-reco + truth cache |
| `cfg/dump_cfg.py` | on-policy dump from the cache with the classifier cut off |
| `cfg/policy_op.py`, `cfg/pix_only.py` | the CA policy v1 was trained on (OP, with all, none or a subset of the forward skip pairs) |
| `cfg/policy_cand.py` | the integrated candidate menu (`customizeCandidate`): the policy of v2 and of the otpull network v2 |
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
export PTT_EPS4=0.99 PTT_EPS4_FWD=0.995 PTT_EPS3=0.99 PTT_DEVICE=cuda
scripts/run_all.sh <workdir>          # cache -> dumps -> train -> export -> fit_wp -> install -> parity
```

`PTT_CACHE_ROOT=<dir with ttbar/ and qcd/ caches>` skips the streaming. The menu config is the one thing that
defines the policy: to retrain on-policy for another CA, rebuild `PTT_MENU` for that CA and rerun. The menu config
used here is `cmsDriver.py Phase2 -s L1P2GT,HLT:75e33_timing,VALIDATION::hltMultiTrackValidation+hltMultiPVValidation
--procModifiers trackingMkFitFit,trackingMkFitAlpaka --era Phase2C22I13M9 --geometry ExtendedRun4D121
--conditions auto:phase2_realistic_T35 --customise SLHCUpgradeSimulations/Configuration/aging.customise_aging_1000
--accelerators cpu ...` followed by `customizeOP(process, keepForwardSkipPairs=True)` from `cfg/policy_op.py`.

## The shipped model (2026-11-03)

Produced with this branch at 4a90c015c63 (the selector defaults reproduce itaca-290-extoff 10205f3529a), IB
CMSSW_20_1_X_2026-11-02-1100, policy = OP with the forward skip pairs kept (57 pixel pairs, `customizeOP(process,
keepForwardSkipPairs=True)`), CPU serial dumps.

- Samples: `samples/train_ttbar.lfn` (10 files, all 1000 events) and `samples/train_qcd.lfn` (6 files, all 300 events).
  Split by event: train 603 + 182, validation 199 + 59, test 198 + 59 (ttbar + QCD). 3.87M tuples.
- Largest preselected count per event: 4545 (ttbar), 4736 (QCD), below the menu's cap of 9984.
- Training: kept epoch 151 of 200; worst-sample validation AUC 0.99651. Test AUC: ttbar 0.99661 (three-hit 0.9267, four
  or more 0.99828), QCD 0.99636 (0.9205, 0.99813). Tuples with four or more hits by pT: 0-2 GeV 0.9981, 2-5 0.9989,
  5-10 0.9976, 10-30 0.9964, 30-100 0.9957, above 100 0.9927. `shipped/train_summary.json`.
- Export: TorchScript vs the training model, max |score difference| 2.3e-6 (`shipped/export_check.json`).
- Working point (chosen by the MTV scan below): eps4 0.99 with 0.995 in the |eta| >= 2.5 band, eps3 0.99,
  `shipped/wp_eps4_0.99_fwd0.995_eps3_0.99.json` (`fit_wp.py --eps4 0.99 --eps4-fwd 0.995 --eps3 0.99`). The strict
  fallback is eps4 0.99 everywhere, `shipped/wp_eps4_0.99_eps3_0.99.json`. The high-purity cut fits to 0 in both: the
  selected tuples with four or more hits are already at 1.2-1.6% fake offline.
- C++ parity (`shipped/parity.json`): in-menu decisions of the shipped model and tables against the offline decisions on
  300 cache events (890,542 tuples): agreement 0.9999978; the 2 differing tuples are within 1.2e-7 of their cut.
- MTV (dev, CPU serial, 332 ttbar / 500 QCD events), against the same CA with the earlier classifier (OPfs):
  hltGeneral efficiency +0.124 +- 0.037 / +0.016 +- 0.022, fake rate -0.096 / -0.097, duplicates +0.007 / +0.005;
  pixel-track fake rate 27.9 -> 20.9% / 26.9 -> 20.1%; the high-purity subset (1697 / 1716 tracks per event) at
  1.46% / 1.52% fake (the extension menu's pixel tracks: 3.20% / 3.29%). eps4 0.995 everywhere gave +0.087 / +0.025
  more efficiency for +0.13 / +0.14 more fake, above the extension menu's fake rate. 0.995 in the forward band only
  gave +0.050 +- 0.022 / +0.032 +- 0.007 (|eta| 2.5-3.0 +0.25 to +0.70 per 0.1 bin) for +0.047 / +0.046 fake, the
  forward fake rate staying below the extension menu's in every bin.
- Dev + holdout pooled against the extension menu (eps4 0.99 tables, 11 ttbar / 22 QCD files): hltGeneral efficiency
  +0.359 +- 0.061 / +0.857 +- 0.058, fake -0.078 +- 0.009 / -0.004 +- 0.005.

## v2: retrained on the integrated candidate (2026-11-03), the model file now shipped

The CA policy changed under the model: the integrated candidate keeps 4 of the 14 forward skip pairs (51 pixel pairs,
not the 57 above), saves 3-layer tuples with disk outer layers and sizes the 3-hit storage from a count
(`tripletTuplesOuterDisks`, `countTripletTuples`), so the classifier was retrained on-policy with the same recipe and
the same working-point rule; `pixel_track_classifier_noOT.pt` is the v2 model and the v1 numbers above are history.

- Policy: the candidate menu, `cfg/policy_cand.py` `customizeCandidate` (= `customizeOP` with the forward skip pairs
  d3-d5, d5-d7, d6-d8, d7-d9 kept, the 3-hit tuple options, the LST / mkFit options of the candidate; the dumps ran with
  the v1 classifier deployed, which the dump's cut-off settings replace, and with t5CleanspLS False, an LST setting that
  does not reach the pixel tuples).
  Dump binary: branch itaca-290-extoff-v2 at 682b631cd9b (C++ as 0b936edaa18), IB CMSSW_20_1_X_2026-11-02-1100, CPU serial.
- Samples: the same caches as v1 (`samples/train_{ttbar,qcd}.lfn`, 1000 ttbar + 300 QCD events, all events of each file),
  same event split (train 603 + 182, validation 199 + 59, test 198 + 59). 4.35M training rows. Largest preselected count
  per event 5131 (ttbar) / 5164 (QCD), below the menu cap of 9984.
- Commands: `scripts/run_all.sh` with `PTT_MENU=<candidate menu>`, `PTT_CACHE_ROOT=<v1 caches>`, `PTT_EPS4=0.99
  PTT_EPS4_FWD=0.995 PTT_EPS3=0.99` (the lane ran its steps one by one with the same commands).
- Training: kept epoch 174 of 200, worst-sample validation AUC 0.99482. Test AUC: ttbar 0.99499 (three-hit 0.9260, four or
  more 0.99738), QCD 0.99460 (0.9206, 0.99719). The three-hit share of the rows is larger than for v1 (disk triplets,
  no 3-hit budget), which is why the overall AUC is lower. `shipped/v2/train_summary.json`.
- Export: max |score difference| 2.6e-6 (`shipped/v2/export_check.json`). C++ parity (2 cache files per sample, 1,000,254
  tuples): agreement 1.0 (`shipped/v2/parity.json`).
- Tables: `shipped/v2/wp_eps4_0.99_fwd0.995_eps3_0.99.json` (the v1 choice of eps, refitted). Offline on the test
  events against the v1 model with its tables on the same tuples: tuples with four or more hits 1686 vs 1692 per ttbar
  event at 0.89% vs 1.11% fake; three-hit tuples 896 vs 818 per event; true-tuple efficiency at pT 10-25 GeV 98.1 vs
  96.5, vxy 1-5 cm 87.2 vs 85.1. `shipped/v2/wp_eps4_0.99_fwd0.995_eps3_0.95.json` is the tighter three-hit option
  (559 three-hit tuples per event).

To deploy: `exec(open('cfg/deploy_block.py').read()); pttDeploy(process,
'RecoTracker/FinalTrackSelectors/data/PixelTrackTorchHighPuritySelector/pixel_track_classifier_noOT.pt',
'shipped/v2/wp_eps4_0.99_fwd0.995_eps3_0.99.json')` after the CA policy, before the early-deletion customisation.
