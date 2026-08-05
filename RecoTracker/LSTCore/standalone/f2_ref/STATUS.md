# F2 -- BAND WORKING POINTS + FAKE BARS (continuation of B01 + B03)

Artifact dir: `standalone/f2_ref`. Workspaces: `standalone/protoB01` (has -RPSA2/-RPSA3),
`standalone/protoB03` (band-split exempt bars -MRB/-MRT/-M4B/-M4T, was mid-implementation).

## State found on resume (2026-08-05)
- B01 batch2 RAN to completion (10 tags: G1 T35 B375 B20 R50 R45 R40 RT50 RT45 C1,
  ntuples 20MB each in b01_ref) but createPerfNumDenHists core-dumped both attempts ->
  hists 392 bytes, jsons all-None. FIX: regenerate hists+jsons from existing ntuples,
  no re-run needed.
- B01 G1 bit-identity gate (new -RPSA2/3 binary, no overrides) NOT yet checked -> run
  cmp_branches r_G1.root vs synth_ref/r_D1.root.
- B03: M3 code claimed written (main.cc band-split bars); build state + gate unknown.

## Findings so far
- GATE B01 PASSED: r_G1 (protoB01 with -RPSA2/3 code, no band flags) vs r_D1 =
  33 IDENTICAL / 0 DIFFER / 0 MISSING / 0 ADDED.
- TRAP: `createPerfNumDenHists` core-dumps when stdout is redirected to /dev/null;
  redirect to a real file. Also returns exit=1 on success sometimes. This is what
  killed the b01 batch2 hists (twice).
- protoB03 band code COMPLETE (main.cc: -MRB/-MRT/-M4B/-M4T, defaults=global bars,
  applied in hybrid -G 6 site ~line 2885; the 1896/1907 global-bar site is dump-mode
  only, not production). Binary current (make: nothing to be done).
- Runner: f2_ref/f2_run.sh (clone of b01_run.sh; OUTDIR=f2_ref, BIN=protoB03).
- Convention: shared log S/FINDINGS_F.md (replaces DISCOVERIES.md), F1 works -a/-RPSA
  same seeds -- check before composing.

## Milestones
- [x] M1: regenerate batch2 hists/jsons; tabulate; G1 gate (gate PASSED; regen running)
- [x] M2: B03 gate PASSED (r_G3 vs r_D1: 33/33 IDENTICAL)
- [x] M3: b01 batch2 tabulated. Barrel: -XCT beats banded -RPSA at equal eff cost
      (B35 dupB -.00495 @ -.00053 vs R40 -.00375 @ -.00053); RPSA2 4.5 free but tiny
      (-.00027 dupT). XCT+XCT2 additivity EXACT (C1 = B35 + T30). reach.txt eff
      predictions ~2x optimistic (T20 measured -.00137 vs predicted -.00065).
      => barrel lever = -XCT band; RPSA stays global (report: band split buys nothing).
- [x] M4: B03 fake-bar scan round 1 (f2_ref r_M4B*/r_MRB*/r_MRT*/r_M4T*):
      MRB dominates M4B per displaced sim; MRB/MRT GAIN eff. DISP1 sims spent:
      M4Ba 18 / M4Bb 38 / MRBb 6 / MRBc 14 / MRTa 3. -M4T weak (-.00073 @ 3 sims).
      => -MRB/-MRT earn band split; -M4B/-M4T stay global (dropped).
- [x] M5a: F2C done. MRT -1.35 DOMINATES -1.5 (fakT -.00415 vs -.00260, same 3 sims);
      MRT -1.65 displaced-FREE for fakT -.00124. MRB frontier: -1.35/-.00432,
      -1.2/-.00567 (6 sims), -1.0/-.00747 (9 sims). M4B -1.0: -.00251/6 T4 sims --
      still dominated, M4B/M4T stay global. Combos EXACTLY additive.
      CMB2 (XCT 3.75 + XCT2 3.0 + MRB -1.2 + MRT -1.5): eff -.00035, dupB -.00307,
      dupT -.00912, fakB -.00561, fakT -.00228, 10 DISP1 sims (9 T5 + 1 pLS), nh +.004.
      CMB3/CMB4 (deeper dedup): eff -.00071 -- outside the ~.0005 budget after 977 offset.
      T225: dupT -.01284 @ -.00115 (frontier point).
- [x] M5b: F2D done. CMB5 (eff -.00035, fakB -.00561, fakT -.00384, dupB -.00307,
      dupT -.00911, 10 DISP1 sims) beats CMB2; CMB6 dominated (eff -.00040, fakB -.00425,
      same 10 sims). MRT12 (-MRT -1.2) dominates -1.35: fakT -.00583, eff +.00018,
      STILL 3 DISP1 sims (0 at DISP10).
- [x] M5c: F2E done. CMB7 DOMINATES CMB5 (eff -.00031 vs -.00035, fakT -.00553 vs
      -.00384, same 10 DISP1 sims). MRT10 (-1.0): fakT -.00771 alone, 4 sims -- frontier
      point, not shipped (84%-true branch, conservative stop at -1.2).
- [x] M6: 977 CONFIRMED (f2_ref/r_F2W977, wall 781s): vs LST: eff .80922 (-.00065,
      .00015 over the ~.0005 soft gate -- effT cost ran hotter on 977 than 300),
      dup .04972 BELOW LST .05138 (dupB .02754, dupT .01874, dupE .07098 below),
      fake .04676 (+.00138 vs LST; was +.00401 -- 65% of gap closed; fakB .04935,
      fakT .05442, fakE .04309 below), nh 6.501 (+.001, no regression).
      a08 977: -45 net distinct DISP1 sims (38 T5 + 9 pLS lost, 2 gained);
      displaced lead +716 -> +671 (-6%); every tier still far above LST.
      FALLBACK strictly inside eff gate: swap -XCT2 3.0 -> 3.5 (measured additivity:
      est eff -.00043 vs LST, dupT ~.0225, everything else unchanged).
- [x] M7: final report delivered (see agent final message; tables above are the record).

## FRONTIER TABLES (300 evt, deltas vs CHAINFINAL=G0/G1/G3; sims = distinct DISP1 lost)
BARREL DEDUP -XCT (base dupB .03127, LST .00989); rate = dupB per 1e-4 eff
  3.75 -.00272 @ eff -.00027 (10.1) | 3.5 -.00495 @ -.00053 (9.3) | 3.0 -.00870 @
  -.00133 (6.5) | 2.5 -.01179 @ -.00208 (5.7) | 2.0 -.01472 @ -.00309 (4.8)
  banded -RPSA dominated at depth: 5.0 -.00192@-.00018, 4.5 -.00311@-.00031, 4.0 -.00375@-.00053
TRANSITION DEDUP -XCT2 (base dupT .02828, LST .01264)
  3.5 -.00519 @ -.00018 (28.8) | 3.0 -.00890 @ -.00040 (22.2) | 2.5 -.01162 @ -.00075
  (15.5) | 2.25 -.01284 @ -.00115 (11.2) | 2.0 -.01393 @ -.00137 (10.2)
  -RPSA2 4.5: -.00027 @ 0 (free, tiny; not shipped)
BARREL FAKE -MRB (br3 exempt mX bar; base fakB .05447, LST .04249; eff GAINS)
  -1.5 -.00304 +eff.00013 ~3 sims | -1.35 -.00432 +.00013 ~6 | -1.2 -.00567 +.00018
  6 sims | -1.0 -.00747 +.00018 9 sims | -0.8 -.00868 +.00027 14 sims
  -M4B (br1 mD bar) DOMINATED per sim (T4-type sims): -1.0 -.00251/6, -0.6 -.00686/18,
  0.0 -.01069/38, 1.5 -.01281 -- br1 bar STAYS GLOBAL.
TRANSITION FAKE -MRT (base fakT .05986, LST .04454)
  -1.65 -.00124 0 sims | -1.5 -.00260 3 | -1.35 -.00415 3 | -1.2 -.00583 3 |
  -1.0 -.00771 4    -M4T weak (-.00073/~3 sims) -- STAYS GLOBAL.
ENDCAP: all four levers untouched by construction; dupE/fakE stay BELOW LST. Endcap-relax
  remains DEAD (b01 E50/E60).
- [ ] M6: 977 confirmation of single best combo
- [ ] M7: final tables + recommendation
