# T5 DNN retrain (3-class), September 2026: everything needed to reproduce it

The candidate is fork branch `lst-t5dnn-retrain` (commits 4ee0723ada3 + b3681cacd10 on f90e0fa5558 = merged master
549d9879bd8 + segment counting). This directory lives on fork branch `lst-t5dnn-train` (training build + scripts;
not for merging). Scripts are the exact files that were run; they contain absolute paths of the machine they ran on
(lnx4555, `/mnt/data1/gsn27/here/...`), so adapt the paths.

## What was shipped
- `models/t5dnn_sel.pt` (+ `.json` with args, feature names, mean/std, history, importance, sample provenance):
  35 inputs = current 23 + 6 raw parent-T3 scores + MD-direction mean/max log-LR + log10(1+x) of (T3s leaving the
  middle MD, T3s leaving the first MD, MDs in the first module) + clipped log10(1+dcaXY). 35-32-32-3, softmax.
- `wp/sel_T5NeuralNetworkWeights.h` == the shipped `src/alpaka/T5NeuralNetworkWeights.h` (md5 07036fe9b3b3; the
  standardisation is folded into layer 1).
- Creation WP: `wp/sel.json` tables["0.95"] == the shipped `kWp` in interface/alpaka/Common.h (1 - P(fake) keeps 95%
  of fully matched T5s per bin; pt>5 x 10 bins of |eta| (0.25) of the first anchor). No promotion table: every
  created T5 is TC-eligible.

## Recipe (in order)
1. Training build: training-build commit on `lst-t5dnn-train` (T5 DNN creation cut commented out, t5x_* branches
   under CUT_VALUE_DEBUG). `scripts/build_train.sh` (= `lst_make_tracklooper -mCd`, CPU).
2. Samples: `scripts/gen_samples.sh`. PU200 ttbar event_2000.root (1000 ev, 10 chunks), jets_1000 entries 0-499
   (5 chunks, -J), 10mu 10k 0.5-50 GeV 5 cm cube gun (4 chunks); flags `-p 0.8 -s 16 -v 1 -w 1 --t5 --t5dnn`.
   Per-chunk provenance (binary md5, git commit, build-diff md5, exact command): `samples_provenance/samples/*/*.done`.
3. Training: `scripts/run_arms.sh` (arms full / base / drop-one-group / noTier), then `scripts/run_sel.sh` (the shipped
   `sel` model: `--groups base,t3raw,mddir,density,dca`). The recipe is in train_T5_DNN.py: shares pu .73 / jet .25 /
   gun .02 flat; class weights fakes 1, trues n_fake/n_true, displaced (vxy >= 1 cm) x8 (1-5 cm) / x16 (>= 5 cm) on
   PU only; event split 60/20/20 seed 42; Adam 3e-3 cosine to 1e-5, 300 epochs, batch 16384; selector = min AUC.
   The training script checks that the rebuilt base-23 features reproduce the current T5 DNN (99.99% < 1e-4).
   Logs and every arm's model: `models/`.
4. WPs + export: `../wp_T5_DNN.py --model models/t5dnn_sel.pt --out wp/sel.json --retention ... --export 0.98`
   (scheme choice: `scripts/scheme_check.py`, `wp/scheme_full.txt`). Install: `../install_T5_WP.py` via
   `scripts/build_arm.sh <name> wp/sel.json 0.95 x wp/sel_T5NeuralNetworkWeights.h`.
5. C++ parity: `../parity_T5_DNN.py` on 3 events of PU chunk 0 (`results/selE.txt`: 99.98% < 1e-4).
6. Deployed validation: `scripts/run_deploy.sh <arm> <e3000|e4000|jets|cube50|cube50_highPt>`, `summarize.py`,
   `compare.py`; tables in `results/` (scan: cmp_*; final: final_selGC_*; jets holdout = jets_1000 entries 500-999).
7. HLT (CMSSW_20_1_0_pre1, 75e33_timing, no procModifier, cpu): `scripts_hlt/` (build_area.sh with the
   pixel-priority mkFit patch, run_val.sh, schedulers, cmp_files.py); results/threeway_1k_{qcd,ttbar}.txt.
8. GPU memory: `scripts/gpu_mem_profile.sh` (nvidia-smi -lms 5, idle GPU, never timed), `gpu_mem_plot.py`.

The T4 retrain follows the same steps (`scripts/PLAN_T4.md`, `../train_T4_DNN.py`, gen_t4_samples.sh,
run_t4_arms.sh; its jet chunk 1 is the two halves of a 10-way split with the i%10==1 half dropped, see
samples_provenance/samples_t4/split.log).
