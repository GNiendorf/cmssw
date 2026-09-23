# T4 DNN retrain: plan (2026-09-22), same process as the T5 retrain

Base: the T5 candidate `4ee0723ada3` (new T5 DNN), because T4s are built from the triplets T5s leave free, so the T4
population depends on the T5 cut. Training worktree `t5dnn/wt_t4train` (branch t4dnn-train).

## Current T4 DNN (what changes)
- 30 inputs: 16 anchor-hit geometry, 5 radius terms (1/Rin, 1/Rout, Rin/Rout, 1/Rreg, 1/RnonAnchorReg), 6 parent-T3
  scores + 3 differences. 32-32-3 softmax. Displaced label vxy > 0.1 cm.
- Cut: P(displaced) > kWp_displaced[pt][eta] AND P(fake) < kWp_fake[pt][eta] with 25 eta bins x 2 pt bins x 2 tables =
  100 constants.
- After the DNN: dBeta (only pT < 1 or > 10), r-z constraint, T3 MD-direction flag. T4 dedup ranks on P(disp) - P(fake).

## New
1. Inputs: current geometry/radius inputs + the 6 raw parent-T3 scores (the 3 differences are redundant for an MLP)
   + the T5 winners adapted to 4 MDs: MD-direction log-LR mean/max (module frame, circle through MDs 0,1,3), density
   (T3s leaving the shared MD 1 and MD 0, MDs in the first module), dcaXY of that circle. One shared templated helper
   computes these for T5 (N=5, circle 0,2,4) and T4 (N=4), and runs BEFORE the DNN (production placement).
2. Training build: T4 DNN cut commented out; features stored under CUT_VALUE_DEBUG and written as t4x_* branches; the
   T4 counting kernel sees the same inputs (counting stays a superset of creation). Post-DNN cuts stay (on-policy).
3. Samples: SAME enrichment as T5: PU200 event_2000 1000 ev (ref, displaced x8/x16), jets_1000 0-499 (share 0.25),
   gun cube5 10k (share 0.02 flat); 100-event chunks, <= 4 jobs; started once the HLT jobs free the box.
4. Training: train_T4_DNN.py sharing the T5 script's recipe (class weights, event split, selector); arms = full,
   current-inputs control, drop-one-group retrains, no-tier; base-feature parity vs the current T4 scores first.
5. WPs: ONE pt x eta table (2 x 10 bins of 0.25, as T5) on one score. The deployed scan decides between 1 - P(fake)
   and P(displaced) (prompt T4s might only add dups of pT5/T5 tracks). Displaced label split 1 cm (as T5/chain),
   checked on the 0.1-1 cm band.
6. Deployed scan vs the T5 candidate on e3000 + jets holdout, then e4000 + cube50/cube50_highPt. Simplifications
   tested there: T4 dedup on the same scalar as the WP, and dropping the post-DNN T3 MD-direction flag once the
   direction LR is an input.
7. C++ parity check, commit locally, then HLT (same 1k/5k three-way setup, as a 4th arm).
8. ROCm: the shared feature helper must keep the ROCm-only noinline macro (T5 fix b3681cacd10). Check a full CMSSW
   all-backend build of the T4 candidate early (the T4 counting kernel inlines the helper + T4 DNN).
9. TIMING RISK: with the T4 DNN cut off, one dense jet event (jet chunk 1) spent > 15 min in RemoveDupQuadrupletsAfterBuild
   (quadratic per module). A loose deployed T4 WP could blow up T4 dedup time in jets: time the jets holdout per stage
   for every T4 WP arm, and cap/restructure the dedup if needed.
