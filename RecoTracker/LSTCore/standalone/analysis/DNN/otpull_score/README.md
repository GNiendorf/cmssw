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
  it). q is chosen by the end-to-end MTV scan, never by the offline numbers.

## Samples

(filled in below with the exact training files, event counts and the producing binary's git SHA)

## Commands

(filled in below)
