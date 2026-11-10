# otpull pair network (LST OT-seeded lookup)

The OT-seeded lookup (`src/alpaka/OTPull.h`) gives an unused OT triplet that starts on barrel OT layer 1-2 the best
fitting Patatrack CA terminal cell (two pixel hits); mutual best pairs become OT-only track candidates. With
`otpullScoreMode 0` the decision is `chi2 < otpullChi2Cut` inside one-sigma windows (sigma table per T3 pT bin). With
`otpullScoreMode 1` it is this network:

* windows of `otpullScoreWindow` sigma (3) on the outer cell hit's radial and z residuals,
* the joint circle rms of the five hits below `otpullScoreJrmsCut` sigma (5),
* the pair network `otpulldnn::runInference` (`NeuralNetwork.h`, weights `OTPullNeuralNetworkWeights.h`),
  15 -> 32 -> 32 -> 1 ReLU, inputs in `common.py` (`FEATURES`), clipped, standardisation folded into layer 1,
* a pair passes if sigmoid(logit) clears the working point of its T3's (pT, |eta|) cell (`otpullScoreWP`, 6 pT rows
  with edges 2/5/10/25/50 GeV x 3 |eta| columns with edges 0.9/1.5, row-major); the arbitration key is
  (1 - score) / (1 - WP), on the same [0, 1) scale as the T5 pixel-stub lookup's chi2 that competes for the cells.

Configuration: `cfg/otpull_score_block.py` (`customizeOtpullScore(process, wp=[...])`) on top of a menu with otpull
enabled (e.g. the extension-off `customizeOP`).

## Files

| file | what |
|---|---|
| `dump.patch` | DIAGNOSTIC dump build (not branch code): `OTPull.h` writes every pair that reaches the network (22 floats: T3 id, cell, the 15 inputs before clipping, logit, chi2, inside the one-sigma windows, pT bin, cost); `LSTEvent`/`LST`/`LSTProducer` (`diagDump`) put the pairs, T3 hits and cell keys; `DiagLST/OTPullDiag` analyzers: `OTPullPairDump` (truth from a training cache's compact cluster truth) and `OTPullDiagAnalyzer` (truth from `ClusterTPAssociation` on RAW) |
| `cfg/dump_cfg.py` | dump job on one cache file: closure of `hltLST` + `OTPullPairDump` |
| `make_rows.py` | dump ROOT -> rows npz (inputs, label, sampling weight, truth particle vxy / pT, C++ logit) |
| `train_otpull.py` | trainer (recipe below) |
| `export_otpull.py` | model json -> `src/alpaka/OTPullNeuralNetworkWeights.h` (+ header-vs-model check) |
| `wp_otpull.py` | working-point tables per target true-pair efficiency q, and the emulated arbitration |
| `parity_otpull.py` | C++ parity: the logit written by the dump build vs the header evaluated in numpy |
| `run_chain.sh` | one command: dump -> rows -> train -> export -> WP |
| `common.py` | input list, dump record layout, WP edges, loaders |

## Recipe (the chain attach head's, `standalone/attach_report_ref/REPORT_ATTACH.tex`)

* Training rows: on-policy dumps of the deployed chain (the arm config the decision will run in), the decision cut OFF
  for the dumped set (every pair that reaches the network is written; the decision does not change that set).
* Samples: PU200 ttbar (reference, >= 1000 events) and high-pT QCD (`QCD_Pt_1800_2400`) at 25% of the loss; events
  disjoint from the dev files, the holdout and the 5k plot lists (see "Samples").
* Label: the T3 (>= 5 of its 6 OT hits) and the cell (both pixel hits) belong to the same simulated particle (in-time
  pileup included).
* 60/20/20 split by event per sample (seed 42); false pairs of the train split kept 1 in 4 with weight 4.
* Weights: true pairs of displaced particles x8 (vxy 1-5 cm), x16 (vxy >= 5 cm); pos_weight = weighted false / true on
  the reference train split; focal factor sigmoid(z)^2 on false pairs; no pT weights (high pT = working-point rows).
* Clip at the reference train split's [0.05%, 99.95%] quantiles, standardise there (folded at export).
* Adam lr 3e-3 cosine to 1e-5, batch 16384; keeps the epoch with the best worst-sample validation AUC.
* Working points: per (T3 pT, |eta|) cell, the score keeping a fraction q of the true pairs of that cell (test split,
  both samples), never fewer than the chi2 decision keeps there; sparse cells pool their pT row (or every pair at or above
  it). With --match-false-row R, the pT rows from R on instead take the threshold that passes no more false pairs than
the current decision there (high-pT fake control). q is chosen by the end-to-end MTV scan, never by the offline numbers.

## Samples

(filled in below with the exact training files, event counts and the producing binary's git SHA)

## Commands

All python runs inside a CMSSW environment (torch, uproot; the CMSSW torch has no NumPy bridge, `common.py` uses DLPack).

```bash
# 0. a scram area of this branch for the dump (CPU serial is enough), with the dump patch:
cd <area>/src && patch -p1 < RecoTracker/LSTCore/standalone/analysis/DNN/otpull_score/dump.patch && scram b -j 16
# 1. training caches (one per RAW file, events disjoint from dev / holdout / 5k lists), made with
#    analysis/DNN/pixel_track_torch/scripts/make_cache.sh (cache_cfg.py: pixel digis, OT clusters, beamspots,
#    random-engine states and the compact cluster truth pttTruth), e.g. <cachedir>/{ttbar,qcd}/cache_*.root
# 2. the arm config of the deployed chain: a full-menu CPU config + customizeOP (or the integration stack)
#    + cfg/otpull_score_block.py customizeOtpullScore(process, wp=[...]); its WP values do not change the dump
# 3. dump -> rows -> train -> export (writes src/alpaka/OTPullNeuralNetworkWeights.h) -> working points:
OTP_DUMP_AREA=<area> OTP_ARM=<arm cfg> OTP_WORK=<workdir> OTP_TAG=<tag> \
OTP_CACHE_TTBAR='<cachedir>/ttbar/cache_*.root' OTP_CACHE_QCD='<cachedir>/qcd/cache_*.root' \
OTP_SLOT='<optional slot wrapper prefix>' bash run_chain.sh all
# 4. rebuild the branch area (and the dump area); C++ parity on a few events of one cache file (in the dump area):
OTP_ARM=<arm cfg> OTP_CACHE=<one cache file> OTP_OUT=parity.root OTP_NEV=10 cmsRun cfg/dump_cfg.py
python3 make_rows.py parity.npz parity.root && python3 parity_otpull.py parity.npz --model <workdir>/models/otpull_<tag>.json
# 5. MTV scan of the WP tables in <workdir>/wp/otpull_<tag>.json (one arm per q); the chosen table goes into the menu
#    configuration (otpullScoreWP).
```

## Production record: weights v1 (2026-11-03)

* Dump binary: commit `7697f7266ca` (branch itaca-290-extoff-f4) + `dump.patch`, CMSSW_20_1_X_2026-11-02-1100, CPU
  serial; arm = the extension-off prototype menu (`customizeOP`) + `customizeOtpullScore` (`production/v1/dump_arm_cpu.py`).
* Training caches (one per RAW file; `production/v1/dump_provenance.txt` lists cache, RAW LFN, events, date per dump):
  * ttbar: 10 files of `/RelValTTbar_14TeV/CMSSW_20_0_0_pre1-PU_150X_mcRun4_realistic_v1_STD_D121_RegeneratedGS_PU-v1/GEN-SIM-DIGI-RAW`
    (all events of each file, 1000 events): 8c0c2859 98494d5b a3269634 a3cad76a d18a7913 d3b7463e d47af44a d9e9d5b0
    ddd5b33a e57ac69b;
  * QCD: 6 files of `/RelValQCD_Pt_1800_2400_14/...` (same campaign, 300 events): 8cc1c5f7 8d48d7b0 8d740de8 8f22095f
    8fb2514c 94743cd7.
  * Disjoint from the itaca_extoff dev files (local ttbar_raw / qcd_raw), the regions holdout lists and the 5k plot lists
    (/mnt/data1/gsn27/here/hltqcd/val5k/files_{ttbar,qcd}.txt); cache production: analysis/DNN/pixel_track_torch.
* Rows: 33.0M pairs (110k true) from ttbar, 11.9M (35k true) from QCD; all pairs dumped (falsePairKeep 1).
* Training (`production/v1/train_v1.log`):
  `python3 train_otpull.py --lab ttbar='rows/ttbar_*.npz' --lab qcd='rows/qcd_*.npz' --ref ttbar --share qcd=0.25 --false-keep 0.25 --epochs 120 --tag v1 --out models`
  (= `run_chain.sh train` with OTP_EPOCHS=120); pos_weight 112.2; best epoch 80, validation worst-sample AUC 0.99437;
  test AUC ttbar 0.99613, QCD 0.99676 (the chi2 alone: about 0.981).
* Export: `python3 export_otpull.py models/otpull_v1.json` (max |header - model| logit 1.5e-5).
* Working points: `python3 wp_otpull.py models/otpull_v1.json --lab ... --q 0.5,0.6,0.7,0.5/0.7/0.5,0.5/0.8/0.5,0.6/0.8/0.6`
  (`production/v1/otpull_v1.{json,txt}`; with `--match-false-row 3`: `otpull_v1_mf3.*`).
* Model json (with the training history and the weights before folding): `production/v1/otpull_v1.json`.
* Selected working points (end-to-end MTV scan on the itaca_extoff dev files + streamed holdout, against the chi2 decision
  of the same menu): q 0.6 / 0.8 / 0.6 in the |eta| columns, `production/v1/wp_selected.py` (`OTPULL_WP_V1`). Scanned:
  provisional-model q 0.5 and 0.7 (one q: the gain grows with q, but q 0.7 raised the barrel fake rate), per-column
  0.5 / 0.7 / 0.5 (provisional), and v1 0.6 / 0.8 / 0.6 (chosen) and 0.5 / 0.8 / 0.5 (barrel efficiency loss).
* C++ parity of v1: the dump build's logits vs `OTPullNeuralNetworkWeights.h` in numpy (`parity_otpull.py`), see the
  lane report.

## Production record: weights v2 (2026-11-03), the weights in `OTPullNeuralNetworkWeights.h`

Retrained on-policy on the integrated candidate (branch itaca-290-extoff-v2): the T3s and cells that reach the network
change with the candidate's pixel tracks (forward skip-pair subset, disk triplets, counted 3-hit storage, classifier v2),
so v1's recipe was rerun unchanged with `run_chain.sh`; v1 above is history.

* Dump binary: branch itaca-290-extoff-v2 at 682b631cd9b (C++ as 0b936edaa18) + `dump.patch`, CMSSW_20_1_X_2026-11-02-1100,
  CPU serial; arm = the candidate menu with classifier v2 (`production/v2/dump_arm_cpu.py`, policy
  `../pixel_track_torch/cfg/policy_cand.py`).
* Training caches: the same 10 ttbar + 6 QCD files as v1 (1000 + 300 events), `production/v2/dump_provenance.txt`.
* Command: `OTP_DUMP_AREA=<area + dump.patch> OTP_ARM=<candidate menu> OTP_TAG=v2 OTP_Q=0.5/0.8/0.5,0.6/0.8/0.6,0.7/0.8/0.7,0.6/0.9/0.6 run_chain.sh all`
  (defaults otherwise: 120 epochs, QCD share 0.25, false keep 0.25).
* Training (`production/v2/train_v2.log`): best epoch 76, validation worst-sample AUC 0.99467; test AUC ttbar 0.99587,
  QCD 0.99678. Export: max |header - model| logit 1.4e-5.
* Working points (`production/v2/otpull_v2_wp.{json,txt}`): v1's MTV-chosen rule q 0.6 / 0.8 / 0.6,
  `production/v2/wp_selected.py` (`OTPULL_WP_V2`); offline on the ttbar test split 23.0 true / 7.7 false objects per event
  against 11.8 / 6.6 for the chi2 decision (QCD 24.3 / 8.0 against 12.9 / 7.1).
* C++ parity of v2 (README step 4, dump build of this branch at 23f6aabf3a6 + `dump.patch`, 10 events of QCD cache
  8cc1c5f7): max |score difference| 2.7e-6, max |model (float64) - C++| logit 1.2e-5: PARITY OK.
* The dumps ran with the T5 x pLS cross cleaning off (as the extension-off prototype menu); the integrated candidate
  keeps it on (`customizeCandidate(..., t5CleanspLS=True)`). The cleaning only removes pure-pLS candidates, which hold no
  OT triplet, so the pairs that reach the network are expected to be nearly the same; not re-dumped.

