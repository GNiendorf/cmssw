# VERIFIER-5 STATUS

Assigned claims: A05 (`-AT3F 0.1`), A10 (`-a 5.0 -F 0.10`), A15 (`-XC4 1`).

## M0 - integrity of workspaces (DONE)
- md5 protoFIN/bin/chainproto = 519b0abc34a28cd1e803d6b9407ef224
- md5 protoA10/bin/chainproto = 519b0abc34a28cd1e803d6b9407ef224  -> A10 byte-identical CONFIRMED
- `diff -rq protoA10 protoFIN -x '*.o' -x bin -x '*.d'` EMPTY -> zero source edits CONFIRMED
- md5 protoA15/bin/chainproto = bababf39b51bca0436c693bd82e8a0dc  -> matches A15's stated md5
- protoA15 differs from protoFIN in exactly main.cc / PixelAttach.cc / PixelAttach.h (as stated)
- protoA05 differs from protoFIN in exactly main.cc / AttachDelivery.cc / AttachDelivery.h (as stated)
- protoA05b differs from protoA05 in main.cc only; protoA15b from protoA15 in main.cc only
- NOTE: A10 claims md5 "identical to protoFIN and protoXC". protoXC md5 = e8380a9f788cb345284ab83c5e4f531d,
  which is NOT equal. protoFIN != protoXC by design (fin_ref/protoFIN_vs_protoXC.diff exists).
  The protoFIN half of the claim is right; the protoXC half is wrong. Cosmetic - all A10 runs used protoA10.
- prototype/compare_ab.py mtime 2026-08-01 (predates every agent round) -> shared tooling untampered.
- Frozen flag prefixes ANCHOR/CTL/STACK/FLAGSHIP/M19/CFF/POSTDELP2 are byte-identical across
  fin_run.sh, a05_run.sh, a10_run.sh, a15_run.sh. My own verify5_ref/v5_run.sh transcribes the
  same prefix (diffed -> IDENTICAL) and writes only into verify5_ref.

## M1 - baselines established (DONE)
- rebase_ref/rb_base300_hists.root is `mode=identity` = LST at 300. Reproduces
  eff .80988 / dup .05179 / fake .04476 / nhitOT 10.14984/10.00937/3.56213. Matches the brief.
- fin_ref/fin_base977_hists.root is `mode=identity` = LST at 977:
  eff .80987 / dup .05138 / fake .04538 / nhitOT 10.14984/10.00937/3.55665.
- 977 assembled baseline = fin_ref/r_W_X4 (flags `-XC 3 -T3E 1 -CC 1 -CCN 1 -CCR 2 -XCD 2 -AT3 6 -XCT 4`).
  The two extra flags are INERT: `-AT3` default is 6.0f (protoFIN/AttachDelivery.h:60) so `-AT3 6`
  restates the default, and the `-XCD` block (protoFIN/main.cc:4996-5060) only increments
  xcTruth[cls][fate] counters. So the 977 baseline is a legitimate stand-in for the assembled line.
- 300 assembled baseline = fin_ref/r_FINBASE_ND (flags exactly `-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2`).

## M2 - 977 headline re-derivation from stored hists (DONE)
All three agents' 977 headline numbers reproduce EXACTLY from the raw hists with the
untouched shared tooling. See v5_977.txt.

## M3 - independent re-runs (IN PROGRESS)
6 x 300-evt launched from verify5_ref/v5_run.sh: A05GATE A05BEST A10GATE A10BEST A15GATE A15BEST.

## M4 - stored no-op gates re-verified BY ME (DONE)
rebase_ref/cmp_branches.py (raw bytes, no tolerance) vs fin_ref/r_FINBASE_ND.root:
  a05_ref/a_GATE (pinned pre-edit bin) 33 IDENTICAL / 0 DIFFER / 0 MISSING
  a05_ref/a_GATE2 (protoA05)           33 / 0 / 0
  a05_ref/a_GATE3 (protoA05 post-RP3)  33 / 0 / 0
  a05_ref/a_GATE4 (protoA05b)          33 / 0 / 0
  a10_ref/r_GATE  (protoA10)           33 / 0 / 0
  a15_ref/r_A15GATE, r_A15G2 (protoA15) 33 / 0 / 0 each
All four A05 gates and both A15 gates CONFIRMED. A10's gate is trivially guaranteed
(binary identical) but confirms the runner prefix.

## M5 - A10 negative results re-verified BY ME (DONE)
  -DD 3 / -DD 4 / -DD 5   : 33/33 IDENTICAL vs r_GATE -> BIT-IDENTICAL, claim CONFIRMED
  -W 0.30                 : 33/33 IDENTICAL -> "bit-identical down to 0.30" CONFIRMED
  -W 0.20                 : 20 DIFFER -> 0.30 really is the boundary
  -FC 0                   : 20 DIFFER (not claimed inert)

## M6 - A05 negative results / bug re-verified BY ME (DONE)
  -CCK 3 NULL: BASE .80992/.06230/.05551/618793 vs CCK3 .80984/.06230/.05553/618811
               = -.00008 eff / .00000 dup / +.00002 fake / +18 rows. CLAIM EXACT.
  -AT3R 1    : F50 .81054/.05219 vs F50R .80904/.05218 = -.00150 eff at -.00001 fake.
               CLAIM "costs .0015 efficiency at identical fake" EXACT.
  -AT3D      : D00 .81054/.06265/.05198 vs F50 .81054/.06263/.05219
               = .00000 / +.00002 / -.00021. CLAIM ".00002/.00002/.0002" EXACT
               (note: the matched point is -AT3F 0.5, NOT 0.1 - A05 never said 0.1).
  -AT3T 7    : fake -.00329, eff -.00194, TRANSITION eff -.00891. CLAIM EXACT.
  -AT3Z 5    : fake +.00413, endcap eff -.00100. CLAIM EXACT (dup delta is .00025 vs the
               ".0004" quoted - the only loose figure found in A05's negatives).
  -RP3 BUG   : a_RP3A7 (pre-fix binary, 22:45 < rebuild 22:51) == fin_ref/r_E_A7X4 on
               eff/dup/fake AND nTC 613782 EXACTLY -> the no-op bug was REAL.
               Post-fix a_RP3_5 and a_RP3_7 both DIFFER from base -> two-sided. CONFIRMED.
  ORTHOGONALITY: -XCT 4->3.5 costs .00088 eff / buys .00305 dup with -AT3F off
               (BASE vs fin_ref/r_E_A6X35) and .00093 / .00312 at -AT3F 0.1
               (F10 vs F10X35). CLAIM EXACT.

## M7 - A15 decomposition + anatomy re-verified BY ME (DONE)
  Their classifier's headline dup/fake reproduce my compare_ab numbers exactly
  (LST .05179, baseline .06230/.05551, post-attack .05668).
  chT4 class: nTC 7801 BEFORE and 7801 AFTER (identical), dupRate .21318 -> .04282. EXACT.
  LST T4 dupRate .12908 -> "our T4 dups now better than LST's" CONFIRMED.
  PLS+PLS|E 27.13/evt (ours) vs 27.61/evt (LST) -> inherited floor CONFIRMED.
  Anatomy from the raw r_ANAT.log: class-A survivors 19.7/evt; NO in-window pair 3.31/evt
  16.8%; logit 3-thr 6.16/evt 31.2%; "best logit >= thr (BUG)" bin = 0.00/evt 0.0%;
  4-layer-anchored 0.83/evt. EVERY anatomy figure CONFIRMED.

## M8 - A05 class ledger re-verified BY ME (DONE)
  GATE attachT3 fake .1387, rows 135.41/evt -> 18.78 fake rows/evt
  LST  LSTpT3   fake .0348, rows 151.48/evt ->  5.27 fake rows/evt
  ratio 3.99x. "FOUR TIMES DIRTIER, 13.9% vs 3.5%, 18.8 vs 5.3 rows/evt" EXACT.
  uniqSim/evt 3.66 (F10) vs LST pT3 5.36. EXACT.

## M9 - baseline-provenance concern RAISED AND CLEARED
  fin_ref/r_W_X4 (the 977 assembled baseline all three compare against) finished 21:08:19,
  but protoFIN/bin/chainproto has mtime 21:11:17 and protoFIN/main.cc 20:23:38 - i.e. the
  977 baseline was produced by a PRE-EDIT protoFIN binary, 53 s before main.cc was edited.
  CLEARED: fin_ref/STATUS.md M3 records both edits (the -XCD 2 diagnostic and one reporting
  printf) and re-gated each (GATE2, GATE3, FINBASE_ND) at 33/33 branches IDENTICAL. I
  independently read the -XCD block (protoFIN/main.cc:4996-5060): it writes only
  xcTruth[cls][fate]. The pre/post-edit binaries are behaviourally identical on this line,
  so r_W_X4 is a valid 977 baseline. No protoFIN file is newer than its own binary, and
  protoA10/main.cc carries protoFIN's mtime to the nanosecond (cp -a), so protoFIN was NOT
  touched by A05/A10/A15.

## M10 - no training by anyone (DONE)
  No .pt / .onnx / weight / model file differs between protoFIN and any of protoA05,
  protoA05b, protoA10, protoA15, protoA15b, and none exists in a05_ref/a10_ref/a15_ref.
  Verification item (5) is N/A for all three, consistent with all three claims.

## M11 - INDEPENDENT RE-RUNS: THE DECISIVE TEST (DONE)
6 x 300 evt through MY OWN v5_run.sh, my own output dir, their binaries.

NO-OP GATES (flags off must reproduce the assembled baseline):
  v_A05GATE / v_A10GATE / v_A15GATE vs fin_ref/r_FINBASE_ND
    -> every metric identical to 5 dp, nTC 618793, and
       cmp_branches.py 33 IDENTICAL / 0 DIFFER / 0 MISSING each.
  (my A15 gate used an EXPLICIT `-XC4 0`, which also exercises the parse path.)

RECOMMENDED CONFIGURATIONS re-run from scratch vs their stored runs:
  v_A05BEST (-AT3F 0.1)      vs a05_ref/a_F10       : 33 IDENTICAL / 0 DIFFER
  v_A10BEST (-a 5.0 -F 0.10) vs a10_ref/r_AJ_50_F10 : 33 IDENTICAL / 0 DIFFER
  v_A15BEST (-XC4 1)         vs a15_ref/r_X4        : 33 IDENTICAL / 0 DIFFER
  BIT-FOR-BIT REPRODUCTION. Scoreboard in v5_300_rerun.txt matches their tables exactly.

## M12 - HIDDEN-BAND SWEEP (every band they did not quote)
Checked at 977 AND 300: eff overall, vxy x4, dxy x4, eff/dup/fake x3 regions,
nhitOT x3 regions, nTC. Findings in the final report. Material items:
  - A15's ALTERNATIVE `-XC4 1 -XCT 4.5` regresses barrel nhitOT 9.80064 -> 9.75745
    (-0.043) and transition 9.88373 -> 9.86331 (-0.020) at 977. UNDISCLOSED; track
    length is a named finish-line criterion. Cost belongs to the -XCT lever (baseline
    at -XCT 4.5 alone is 9.75198), not to -XC4. A15's MAIN recommendation is clean
    (all three regions up).
  - A10's 977 cost list omits vxy[1,5): .80110 -> .79978 = -.00132 (~6 tracks).
    It is exactly zero at 300, where they did their track accounting.
  - A10's "endcap efficiency ... unchanged from the baseline" is FALSE at 977:
    .74436 -> .74385 = -.00051; gap to LST widens -.00222 -> -.00273.
  - A15's efficiency cost at 300 is entirely TRANSITION (-.00077); barrel and endcap
    are unchanged to 5 dp.
  - BASELINE-INHERITED, none of the three moved it: dxy[10,30) is .03151 vs LST .05402
    at 977 (.03073 vs .05674 at 300) - a displaced band ~40% WORSE than LST.

## M13 - VERDICT: ISSUES
All three CENTRAL results reproduce bit-exactly; no recommendation is overturned.
Issues are inaccurate statements within the claims, listed in the final report.
Nothing of mine is left running.
