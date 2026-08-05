# VERIFIER-3 STATUS (adversarial verification of A03 / A08 / A13)

Scratch: `/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/verify3_ref/`
Tools written here: `v3_run.sh`, `v3_rederive.py`, `v3_bands.py`, `v3_distinct.py`, `batchA.sh`.
Nothing outside verify3_ref was written to. No git commits.

## M1 -- re-derivation from raw hists (DONE)
`v3_rederive.py` recomputes every metric with the standard tooling's own
`compare_ab.compute_metrics()` straight off the `*_hists.root`, then diffs against the
`.json` each explorer shipped. **22 + 23 files checked, ALL SHIPPED JSONs MATCH** to 1e-9.
No table doctoring anywhere.

Reference values re-derived by me (300 / 977):
  assembled baseline r_FINBASE  .80992 / .06230 / .05551  nTC 618793
  assembled baseline r_W_X4     .80905 / .06184 / .05607  nTC 2033868
  LST rb_base300                .80988 / .05179 / .04476
  LST fin_base977               .80987 / .05138 / .04538

## M2 -- no-op gates, run BY ME with rebase_ref/cmp_branches.py (DONE, ALL PASS)
  FINBASE vs a03_ref/r_V2GATE   33 IDENTICAL / 0 DIFFER / 0 MISSING / 0 ADDED
  FINBASE vs a13_ref/r_GATE2    33 IDENTICAL / 0 DIFFER / 0 MISSING / 0 ADDED
  FINBASE vs a13_ref/r_GATE0    33 IDENTICAL / 0 DIFFER / 0 MISSING / 0 ADDED
  FINBASE vs a08_ref/r_A08GATE  33 IDENTICAL / 0 DIFFER / 0 MISSING / 0 ADDED
  a03 r_V2S6 vs r_V3S6          33 IDENTICAL  (the -ATP-default claim)

## M3 -- source/binary provenance (DONE)
  protoA08 == protoFIN: ZERO source differences, binary md5 519b0abc... identical. CONFIRMED.
  protoA03: exactly 5 files / 28 hunks vs protoFIN, no other file differs. CONFIRMED.
  protoA13, protoA13b: main.cc only. CONFIRMED.

## M4 -- independent distinct-displaced counter (DONE) -> verify3_ref/distinct977.txt
Written from performance.cc, NOT copied from a08_distinct.py. Reproduces A08 exactly:
DISP1 LST 7782 / proto 8512 (+730, 377 lost / 1107 gained, nSim 16369);
DISP10 2956 / 3423 (+467). -MR -1.0 = -55 net DISP1, -45 net DISP10. -M4D -1.6 = +45.
Also confirms performance.cc:1186 vtx_perp < 2.5 cap on the headline denominator.

## M5 -- ISSUE FOUND (A08, reporting): vxy[30,60) denominator mislabelled
Summary text says "vxy[30,60) ... LARGEST displaced denominator of all (5721 sims, bigger
than v1030's 4098) ... .0863 vs .0685". My independent count: **vxy[30,60) holds 1545
sims, not 5721**; 5721 is the vxy>=30 / DISP30 count, of which 4176 sit beyond 60 cm where
BOTH algorithms reconstruct exactly zero. The quoted rates .0863/.0685 are 494/5721 and
392/5721, i.e. the vxy>=30 rates, not the [30,60) rates (which are .3197 / .2537).
The +102 track delta and the +4.07 sigma ARE correct, and a08_ref/a08_977pass.txt itself
carries the CORRECT denominator 1545. Defect is in the summary text only.

## M6 -- 300-evt reproduction runs (batchA.sh, IN FLIGHT)
A03GATE, A03BEST(-ATS 1.0 -XCT 3.75), A13GATE, A13BEST(-RPSA 5.5), A08GATE -- all through
my own v3_run.sh into verify3_ref.

## M7 -- MY OWN 300-EVT RERUNS: EXACT REPRODUCTION (DONE)
Run from scratch through verify3_ref/v3_run.sh into verify3_ref (~865 s each, 5 parallel):
  r_A03GATE  == r_FINBASE   on all 29 metrics AND nTC 618793, 33/33 branches IDENTICAL
  r_A13GATE  == r_FINBASE   likewise
  r_A08GATE  == r_FINBASE   likewise
  r_A03BEST  == a03_ref/r_XS10C375  all metrics + nTC 617718, 33/33 branches IDENTICAL
  r_A13BEST  == a13_ref/r_C_RPSA55  all metrics + nTC 617338, 33/33 branches IDENTICAL
Every explorer's best configuration and no-op gate reproduces BIT-IDENTICALLY.

## M8 -- MECHANISM CROSS-CHECKS from the output trees (verify3_ref/compose300.txt)
A13 "every removed row is a zero-OT-hit bare type-8 row": CONFIRMED exactly. FINBASE ->
C_RPSA55 changes pLS/0 890.19 -> 885.34 per event and leaves all EIGHT other
(tc_type, nhitOT) classes identical (T5/10 233.26, T5/12 185.12, T5/14 8.53, pT3/6 135.41,
pT5/10 261.66, pT5/12 296.27, pT5/14 18.00, T4/8 34.20). Total delta = the pLS delta.
A13 "-DC 2 -DCT t == -RPSA t": r_DC2_6 is identical to r_C_RPSA6 on all 29 metrics AND on
nTC (617813 = 617813). CONFIRMED.
A03 "delivered pT3-class rows fall 135.4 -> ~128/evt": measured 135.41 -> 126.63 (LST
151.48 at 300). CONFIRMED (their "~128" is 126.6).

## M9 -- ISSUES (final list)
A08-1  vxy[30,60) denominator mislabelled in the summary (5721 is vxy>=30; [30,60) is 1545).
A08-2  "dup deficit is 100% a prompt phenomenon" contradicted by their own
       displaced_dup_977.txt: vxy[1,5) is +.1372, LARGER than prompt's +.0978.
A08-3  "3-25x better from vxy 5 cm outward": weakest band vxy[5,10) is 1.75x.
A13-1  -XC 1 -RPSA 4.5 simplification variant: dupT +.0073 and nhT -0.039 at 977, unreported.
A13-2  "transition efficiency stays AT LST" is 300-only; at 977 effT .87958 vs LST .88004.
A03-1  endcap dup rises .08069 -> .08305 vs the assembled baseline; framed only against LST.
A03-2  the 300-evt -PWE test crosses binaries (v1 vs v2); same-binary test is 60 evts only.
No process left running; all pollers cleaned up.
