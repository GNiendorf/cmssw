# T5 DNN retrain (chain-style, 3-output) - plan for owner review, 2026-09-22

Goal: a T5 DNN that raises displaced efficiency and cuts fakes on ttbar PU200 and in jets, trained like the
chain gate. The T5 DNN stays FIRST in T5 creation; no existing post-DNN T5 cut quantity (rz chi2, regression
chi2, dBeta) becomes an input.

## 1. Network
- 3 outputs (fake / prompt / displaced), 32-32 hidden, ReLU (same shape as the chain gate and the T4 DNN).
- Inputs (~41), all computed in runQuintupletDefaultAlgo BEFORE the DNN call:
  - the existing 23 (anchor-hit eta/phi/z/r, hit-to-hit deltas, log inner/bridge/outer radius);
  - parent T3 DNN scores: fake/prompt/displaced of inner and outer T3 + the 3 differences (9) - already
    stored per T3, same as the T4 DNN and the chain gate;
  - MD direction statistic: mean saturating log-LR over the 5 MDs (all-module module frame) + max per-MD w (2);
  - local density (conditioning variable): log10(1+) of the number of T3s leaving the middle MD and of the MD
    occupancy of the first module (2) - analogue of the chain gate's maxJunctionDegProduct;
  - chain-gate-style topology: nPS, nBarrel, innermost logical layer (3), charge consistency of the two T3s (1),
    dcaXY of the T5 circle to the beam line (1).
- Conditioning as in the chain gate (log10_1p on counts/residuals, clips), standardisation fitted on the PU200
  train split only.

## 2. Code for the training build (base = master + segment counting, f90e0fa5558)
- Compute the new inputs in runQuintupletDefaultAlgo before the DNN; store them in QuintupletsSoA under
  CUT_VALUE_DEBUG (no memory cost in production); write new t5_* branches in write_lst_ntuple.cc.
- Comment out the T5 DNN creation cut (score still computed and written), as the owner did before.
- T3 DNN cut and everything upstream unchanged (on-policy w.r.t. what ships). The hard T5 direction rule is
  NOT in this build (the statistic becomes an input instead).
- Writer: all created T5s, including ones later flagged duplicate; --t5dnn-style slim output (not --allobj)
  to keep disk small; -d build.
- Smoke test on 10 events: new branches filled/finite, T5 count up vs master as expected.

## 3. Samples (mirror the chain gate A02 corpus)
| sample | file | events | loss share |
|---|---|---|---|
| PU200 ttbar | event_2000.root (event_1000 was destroyed) | 1000 | 0.73 (reference) |
| jets | jet_ref/trackingNtuple_jets_1000.root, events 0-499 | 500 | 0.25 |
| muon gun | trackingNtuple_10mu_10k_pt_0p5_50_5cm_cube.root (the chain's "c5hp") | 10k | 0.02, flat |
Never trained: cube50, cube50_highPt, jets 500-999, PU200 holdout, HLT QCD (signal-only truth).
Produced in 100-event chunks with a stall watchdog (--allobj/writer hang rule), <= 4 lst jobs box-wide.

## 4. Training (train3mix2.py recipe)
- Labels: true iff one sim owns > 75% of the T5's hits; displaced iff that sim's vxy >= 1 cm (chain gate
  convention; pileup-only true = prompt).
- Weights: fakes 1, trues n_fake/n_true per sample; displaced tiers x8 (vxy 1-5 cm) and x16 (>= 5 cm) on
  PU200 only; jets not tiered; gun flat.
- Adam 3e-3, cosine to 1e-5, 300 epochs, batch 16384, seed 42; event split 60/20/20.
- Selector: min over samples of AUC(prompt vs fake) and AUC(displaced vs fake).
- Control arm: same recipe with only the existing 23 inputs, to separate "new variables" from "retrain".

## 5. Working points and deployment
- Fixed per-bin signal efficiency on LST's pt x eta binning (2 x 10), separate prompt and displaced tables,
  pass = (zP - zF > wpP) OR (zD - zF > wpD).
- Creation cut at the retention the current kWp98 has; promotion at the kWp93 analogue; the same eta
  variable for deriving and applying (current kWp93 mismatch fixed).
- Scalar for the low-pT dup tiebreak and T5 extension: 1 - P(fake).
- Export + Python-vs-C++ parity check on the new header (the chain parity scripts are stale; write a real one).
- Scripts + commands committed to analysis/DNN (reproducibility mandate).

## 6. Validation (deployed, never offline-only)
PU200 holdout (e3000 + e4000, paired, all displaced bands, prompt, fake, dup); cube50 and cube50_highPt; jets
500-999 incl. fine core dR bins; HLT QCD + ttbar in BOTH menu modes at the end (reference setup matched, pilot
first); CPU timing on a quiet box + GPU build.

## Decisions for the owner
1. PU200 training file: event_2000 (then validate on e3000/e4000), or re-produce a fresh 1k-event sample.
2. Displaced split at vxy 1 cm (chain) or 0.1 cm (current LST T3/T4/T5 DNNs).
3. OK to leave the T4 DNN for a second round (its population depends on the T5s).
