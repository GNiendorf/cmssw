# P2.6d status (durable; resume from here if interrupted)

Working tree invariant: branch `chain_tracking_proto`, clean at 37bf47dd4c8.
If a step below says CHECKED OUT P2.5, the tree is DETACHED at f41c6abb8a4 and must be
restored with `git checkout chain_tracking_proto` + rebuild.

## JOB 1 - pinned-toolchain re-verification P2.5 (f41c6abb8a4) vs HEAD (37bf47dd4c8)

[DONE] J1.0 confirmed the defect: p26_ref/bin_before and p26b_ref/bin_before contain ONLY
       lst_cpu/lst_cuda, no liblst_*.so. p26c_ref/bin_before/bin_after do contain both.
       => the P2.6a and P2.6b bit-identity gates ran the OLD executable against the NEW library.
[DONE] J1.1 snapshot HEAD debug toolchain -> p26d_ref/bin_head_d (md5_head_d.txt)
[DONE] J1.2 checkout f41c6abb8a4, build -md, snapshot -> p26d_ref/bin_p25_d (rc=0, 0 error lines)
[DONE] J1.3 tree restored to chain_tracking_proto @ 37bf47dd4c8, clean.
       md5 proof, liblst_cpu.so:  P2.5 133a742732639cd62fb2851bc8f6461e
                                  HEAD d4ea4e74f9f4b362ac6cbce1de9fb6b6
       md5 proof, liblst_cuda.so: P2.5 c53b441cd516ba377262a3268377d82e
                                  HEAD 4122955c8856413fbbdf7f30e853c3af
       Neither executable carries an RPATH entry for liblst, so LD_LIBRARY_PATH decides; each leg
       is run with its own snapshot dir prepended and ldd proof is printed in the gate log.
NOTE: the snapshot dirs also carry librooutil.so, because `lst_make_tracklooper` deletes
      code/rooutil/librooutil.so during `make clean` and a concurrent build otherwise makes every
      pinned replay fail with "librooutil.so: cannot open shared object file". The first attempt at
      J1.4 died that way and was rerun.

[DONE] J1.4 CPU bit-identity, 10 evt --allobj  -> j1_bit_cpu.out   *** FINDING ***
       TC sidecar        100.0000% (set identity, all 10 events)
       CHAIN(mds)        100.0000%
       CHAIN(full)        67.6591%  <-- NOT 100%
       Field-level localisation (p26d_chaindiff.py): nLayers/branch/trim/flags all 0 differing;
       the ONLY differing field is chains.score, 14479/44770 chains (32.34%), always at the
       last 1-2 ulp of float32, e.g. A=16.977041244506836 vs B=16.97704315185547.
       chains.score = sum of member weld-edge logits + lambdaLen*nLayers (ChainWeld.h), i.e. an
       accumulation whose value depends on summation order. It is ALSO an acceptance threshold
       input (ChainsSoA.h comment on SOA_COLUMN(float, score)), so it is not purely diagnostic.
       Magnitude: |delta| max 1.144e-05, median 1.907e-06; RELATIVE max 4.67e-07, median 9.38e-08
       (float32 eps = 1.19e-07). i.e. 1-4 ulp, pure floating-point rounding, not an algorithm change.
       ntuple: 20 mismatching branches = 12 known -d pLS debug branches + 8 tc_* branches with
       exactly 2/19898 differing entries each, consistent with two TC rows swapping order
       (the TC sidecar set comparison is 100%).
[DONE] J1.4b CUDA bit-identity, 10 evt --allobj -> j1_bit_cuda.out
       CHAIN(mds) 100.0000%, CHAIN(full) 100.0000%  (score drift: ZERO on CUDA)
       TC 99.6734% == the known baseline GPU nondeterminism floor (p26c measured 99.7488% for the
       same binary twice). ntuple 325 branches, dominated by ls_rawIdx / md_rawIdx permutation.
       => the drift is CPU-only, which points at the CPU-targeted phase (P2.6b).
[DONE] J1.5 same-toolchain control, CPU HEAD twice -> j1_ctl_cpu.out
       TC 100%, CHAIN(mds) 100%, CHAIN(full) 100%, score 0 differing.
       ntuple: 12 mismatching branches, EXACTLY the 12 ls_* names that also mismatch in J1.4
       (ls_dAlpha*/ls_dPhi*/ls_rt*/ls_z*) => those are the known -d pLS debug-branch defect.
       => the CPU score drift in J1.4 is a genuine build-to-build difference, not run-to-run noise.
[DONE] J1.5b same-toolchain control, CUDA HEAD twice -> j1_ctl_cuda.out
       TC 99.7236% (this IS the floor), CHAIN(mds) 100%, CHAIN(full) 100%, score 0 differing.
       The P2.5-vs-HEAD CUDA TC number (99.6734%) is therefore at the measured floor.
[DONE] J1.7a BISECT toolchains built (bin_p26a_d, bin_p26b_d, CPU only). Tree back on
       chain_tracking_proto @ 37bf47dd4c8, skip patch popped out of the stash.
[DONE] J1.7b BISECT, CPU, 10 evt, pinned toolchains -> bis_a.out / bis_b.out / bis_c.out
       P2.5  -> P2.6a : TC 100%, CHAIN(mds) 100%, CHAIN(full) 100%, score 0 differing   CLEAN
       P2.6a -> P2.6b : TC 100%, CHAIN(mds) 100%, CHAIN(full) 67.6591%, score 14479 (32.34%)
                        <== THE WHOLE DRIFT IS P2.6b (483a8a62725), and it reproduces the
                            P2.5-vs-HEAD number exactly (67.6591%, same 14479 chains)
       Mechanism: P2.6b replaced the edge/attach MLP with chainLinearBatch (B=16 transposed,
       __restrict__, kBlk=8 unit blocking, `#pragma omp simd` on the lane loop, ChainEdges.h).
       Its comment claims every lane is BIT-IDENTICAL because the per-row operation ORDER is
       unchanged - true for the order, but the codegen change alters FMA contraction, so the
       logit lands 1-4 ulp away. That propagates: logOdds -> edgeSum -> chains.score.
       P2.6b's own commit message claims "CPU/GPU bit-identity 100% (chain+TC)"; that gate is
       one of the vacuous ones (p26b_ref/bin_before holds no liblst_*.so).
       P2.6b -> HEAD  : TC 100%, CHAIN(mds) 100%, CHAIN(full) 100%, score 0 differing   CLEAN
[DONE] J2.1 production no-op proof -> noop.out
       bin_prod_pristine vs bin_prod_skip with LST_CHAIN_SKIP_DOOMED UNSET, CPU 10 evt:
       TC 100%, CHAIN(mds) 100%, CHAIN(full) 100%, all fields 0 differing.
       => the skip patch is a no-op when the variable is unset, so ALL THREE benchmark
          configurations can be measured with the single binary bin_prod_skip.
[DONE] J1.6 300-evt CPU scoreboard, -s 32, P2.5 vs HEAD -> j1_scoreboard.out
       rows total 29, rows moved 6, rows identical 23.  NOT 29/29.
       Every efficiency row is EXACTLY identical, and n_tc is exactly 614277 in both.
       The six that moved, all in the 7th significant digit:
         dup_overall_incut  -0.0002%   fake_overall_incut  -0.0002%
         dup_endcap         -0.0004%   fake_endcap         -0.0004%
         mean_nhitOT        +0.0001%   mean_nhitOT_endcap  +0.0007%
       mean_nhitOT moved by 7.6e-06 on 614277 TCs, i.e. of order FIVE outer-tracker hits over
       300 events. HEAD is marginally cleaner (lower dup + fake). This is the downstream of the
       P2.6b ulp drift, not a selection change.
       DECISION: the drift is real and must be reported, but it cannot affect a WALL-TIME
       measurement, and the benchmark is the maintainer's decision input, so JOB 2 proceeded.

MAINTAINER RULING (2026-08-03, relayed by the coordinator): the 1-4 ulp chains.score drift is
ACCEPTED as-is. The correct wording for the P2.6a/b/c phases is DECISION-IDENTICAL WITH LAST-BIT
FLOAT DRIFT, not "bit-identical". No further forensics; the bisect was already complete when the
ruling arrived and is recorded above (P2.6b, 483a8a62725, is the sole source).

## Toolchain inventory (all self-contained: exe + liblst_* + librooutil)
  bin_p25_d          f41c6abb8a4  -md   P2.5
  bin_p26a_d         28d4d861c00  -mCd  P2.6a (CPU only)
  bin_p26b_d         483a8a62725  -mCd  P2.6b (CPU only)
  bin_head_d         37bf47dd4c8  -md   HEAD / P2.6c
  bin_prod_pristine  37bf47dd4c8  -m    production, unpatched
  bin_prod_skip      37bf47dd4c8+patch -m  production, LST_CHAIN_SKIP_DOOMED capable

## JOB 2 - benchmark (production builds, no -d)
[DONE] J2.0 build pristine production toolchain -> p26d_ref/bin_prod_pristine
[DONE] J2.1 LST_CHAIN_SKIP_DOOMED added (LSTEvent.dev.cc, uncommitted) + bin_prod_skip built; no-op proved above
[DONE] J2.2 stream sweeps: base / hybrid / preview, cpu + cuda, -n 200 -v 1 -w 0, PU200RelVal,
       all three configs from the SINGLE binary bin_prod_skip (proved a no-op when unset).
       NOTE the "-v 1" table tags the run "explicit_cutvalue" - that string is parsed at RUNTIME
       out of standalone/.make.log, which currently holds the LAST build in the tree (the bisect
       -mCd one). The benchmark binary itself is MAKECUTVALUES=false / MAKETARGET=explicit
       (.make.log.1785763270). The tag is a stale label, not the build.

       CPU baseline (chain OFF = production LST today), ms/evt (evt/s):
         s=1  867.80 (1.15)   s=4  228.20 (4.38)   s=16  61.70 (16.21)
         s=32  32.70 (30.58)  s=64  43.60 (22.94)
       CPU hybrid (chain ON, both algorithms), ms/evt (evt/s):
         s=1  909.70 (1.10)   s=4  241.20 (4.15)   s=16  65.20 (15.34)
         s=32  34.60 (28.90)  s=64  23.00 (43.48)
       *** s=64 IS NOT TRUSTWORTHY at -n 200: hybrid (23.0) came out FASTER than baseline (43.6),
       which is impossible because hybrid runs a strict superset of the baseline work. 200 events
       over 64 streams is ~3 events per thread, so the wall-clock is dominated by thread start-up
       and load imbalance. Use s=32 as the meaningful high-stream point. ***
       *** PREVIEW FINDING (s=1 CPU, 890.30 ms/evt): T5/T4/pT5 stages go to 0.00 as intended, but
       pT3 goes 47.2 -> 161.1 ms/evt. This is NOT an artifact. PixelTriplet.h:721 skips any pLS with
       partOfPT5 set and :770 skips any T3 with partOfPT5 set. With the pT5 builder deleted those
       flags are never set, so the pT3 stage has to evaluate every pLS x T3 pair that pT5 would have
       claimed first. pT3's present cost is SUBSIDISED by pT5 running ahead of it. A naive P2.7
       deletion therefore hands ~114 ms/evt back to pT3 on CPU at s=1. ***
       PREVIEW PHYSICS IS INVALID (carried pLS set changes). WALL TIME ONLY.

       CPU COMPLETE, ms/evt (evt/s), PU200RelVal, -n 200 -v 1 -w 0, production bin_prod_skip:
         s     baseline          hybrid            preview(INVALID physics)
         1     867.80 (1.15)     909.70 (1.10)     890.30 (1.12)
         4     228.20 (4.38)     241.20 (4.15)     237.70 (4.21)
        16      61.70 (16.21)     65.20 (15.34)     63.10 (15.85)
        32      32.70 (30.58)     34.60 (28.90)     33.40 (29.94)
        64      43.60 (22.94)     23.00 (43.48)     22.10 (45.25)   <- base was the OUTLIER
[DONE] J2.2d CPU RECHECK of s=32 and s=64, all three configs (cpu_recheck.out).
       s=32 reproduced to 0.1 ms everywhere (32.6/34.5/33.4 vs 32.7/34.6/33.4).
       s=64 base came back at 21.9, so the 43.60 of the first sweep was the outlier - NOT the
       hybrid number.
       FINAL CPU table, best-of-two, ms/evt (evt/s), and ratio to baseline:
         s     baseline          hybrid            preview(INVALID)   hyb/base  prev/base
         1     867.80 (1.15)     909.70 (1.10)     890.30 (1.12)        1.048      1.026
         4     228.20 (4.38)     241.20 (4.15)     237.70 (4.21)        1.057      1.041
        16      61.70 (16.21)     65.20 (15.34)     63.10 (15.85)       1.057      1.023
        32      32.60 (30.67)     34.50 (28.99)     33.40 (29.94)       1.058      1.025
        64      21.90 (45.66)     23.00 (43.48)     22.10 (45.25)       1.050      1.009
       => on CPU the chain algorithm is SLOWER than production LST at EVERY stream count, both
          today (+5.0 to +5.8%) and after the P2.7 deletion (+0.9 to +4.1%). It is never faster.
[DONE] J2.2b CUDA sweeps, ms/evt (evt/s):
         s     baseline          hybrid            preview(INVALID physics)
         1      4.90 (204.1)     10.40 ( 96.2)      7.30 (137.0)
         2      3.20 (312.5)      6.20 (161.3)      4.30 (232.6)
         4      2.40 (416.7)      4.00 (250.0)      2.80 (357.1)
         6      2.30 (434.8)      6.90 (144.9)*     2.30 (434.8)
         8      2.10 (476.2)      3.10 (322.6)      2.20 (454.5)
       * hybrid s=6 is an outlier (worse than both s=4 and s=8); GPU runs at -n 200 last ~2 s so
         they are noisy. A repeat sweep was run for error bars.
[DONE] J2.2c CUDA REPEAT sweep, best-of-two (run1 in sweep_cuda_*_run1.log, run2 in sweep_cuda_*.log).
       s=1,2,8 reproduced to 0.0-0.1 ms in every config; s=4 and s=6 each threw one outlier, so
       best-of-two is the honest estimator. Resulting monotone table, ms/evt:
         s     base   hybrid  preview   hyb/base  prev/base
         1     4.90    10.40     7.20      2.12       1.47
         2     3.20     6.20     4.30      1.94       1.34
         4     2.40     3.90     2.80      1.62       1.17
         6     2.30     3.50     2.30      1.52       1.00
         8     2.10     3.10     2.10      1.48       1.00
       => on GPU the hybrid is 1.5-2.1x slower than production LST; the post-deletion PREVIEW
          reaches PARITY at s=6 and s=8. Caveat: base itself flattens at 2.1-2.3 ms there, so the
          parity is partly the host/input side saturating, not the chain becoming free.
[DONE] J2.3 memory table, PRODUCTION build, 10 evt, -v 2, MB/evt (means)
         collection            base    hybrid   preview
         ChainEdges            0.00      1.56      1.56
         ChainIncidence        0.00      3.35      3.35
         ChainNodes            0.00      4.02      4.02
         Chains                0.00      1.35      1.35
         Hits                 10.03     10.03     10.03
         MiniDoublets          7.45      7.45      7.45
         PixelQuintuplets      2.10      2.10      2.10 (still allocated, only the kernels skip)
         PixelSegments         1.26      1.26      1.26
         PixelTriplets         0.50      0.50      0.50
         Quadruplets           1.28      1.28      0.10
         Quintuplets          10.24     10.24      0.10
         Ranges                1.10      1.10      1.10
         Segments             21.50     21.50     21.50
         TrackCandidates       0.42      1.14      1.13
         Triplets             23.52     23.52     23.52
         TOTAL                79.40     90.40     79.07
       chain collections          +10.28 MB/evt
       TrackCandidates growth      +0.72 MB/evt
       P2.7 deletion set (T5+T4+pT5) -13.62 MB/evt
       P2.7 NET                    -2.62 MB/evt   => memory still goes NEGATIVE on production
       builds (p26c measured -4.18 on -d builds; -d inflates the deleted collections more than
       the chain ones, which is why the production margin is smaller but still negative).

## ALL MILESTONES COMPLETE (2026-08-03)
Working tree: branch chain_tracking_proto, now at d2bb51cb0e6 (the maintainer committed their
ruling into PLAN_lst_redesign_t3_onward.md on top of 37bf47dd4c8 while this agent was running;
that commit touches only the plan doc, no source, so every benchmark binary here is still the
37bf47dd4c8 source state). ONE uncommitted file,
RecoTracker/LSTCore/src/alpaka/LSTEvent.dev.cc, holding the timing-only LST_CHAIN_SKIP_DOOMED
switch. Nothing was committed and nothing was pushed. prototype/ untouched.
