# F3 -- COMPOSER/VERIFIER (barrel-dup round final agent)

Artifact dir: standalone/f3_ref. Merged tree: standalone/protoFINAL2.

## Plan
1. MERGE protoFINAL + B02 delta (AttachDelivery.h/.cc + main.cc: a-bands/-a4/-CCS) +
   B03 delta (main.cc only: -MRB/-MRT/-M4B/-M4T). DONE: 3-way git merge-file CLEAN,
   line count exact (5798+157+40=5995). Built OK (bin/chainproto).
2. Gates: F3G0 (defaults = CHAINFINAL bit-identity vs synth_ref/r_D1.root, need 33/33);
   F3F1R (F1 line, bit-compare vs synth_ref/r_F1B4.root);
   F3F2R (F2 line, bit-compare vs f2_ref/r_CMB7.root).
3. F3C0 = full stacked line on 300; measure subadditivity vs F1B4+CMB7 deltas.
4. Short descent (<=12 runs): -XCT / -XCT2-vs-CCS2 retune; eff gate ~.0005; displaced
   spend via a08_distinct at every candidate.
5. 977 record: final + one alternate. Plots tag finishline2. Verdict per band.

## State
- [x] M1 merge + build (protoFINAL2; ordering constraints preserved: -a2/-a3/-a4 pre-scan
      before getopt "a:"; -CCS2/-CCS3/-CCS before -CC; -MRB/-MRT/-M4B/-M4T before -MR)
- [x] M2 GATES ALL PASSED at the strongest level (cmp_branches, 33/33 IDENTICAL, 0 ADDED):
      F3G0 (defaults) == r_D1 CHAINFINAL bit-exact; F3F1R == r_F1B4 BIT-EXACT;
      F3F2R == f2_ref/r_CMB7 BIT-EXACT. Merged tree reproduces both contributors byte-for-byte.
- [x] M3a F3C0 full stack measured (300, dCF): eff -.00013 (L+.00053 on 300), dupB -.00750
      (naive -.00750 EXACT), dupT -.01480 (naive -.01492, subadd only .00012), dupE -.00081,
      fakB -.00619 (naive -.00620), fakT -.00661 (naive -.00667), nh +.0043, nhB +.032.
      COMPOSITION IS ESSENTIALLY EXACTLY ADDITIVE. dupT abs .01348 = LST+.00084 (AT LST).
      dupB abs .02378 = 2.40x LST (was 3.16x). Displaced: DISP1 net -18 (19L/1G) = naive
      F1(-8)+F2(-10) EXACT, no overlap savings. DISP30 -4.
      => binding constraints: 977 eff gate (977 runs ~.0003 hotter than 300) + displaced spend
      (977 naive -79).
      Descent plan drafted (batch B after F3C0 lands, 4 parallel):
      F3D1 = stack w/ -XCT2 3.5 (eff recovery ~+.00022, F2 fallback)
      F3D2 = stack w/ -XCT 4 (drop barrel seed-retire; conversion+CCS carry barrel)
      F3D3 = stack w/ -CCS2 6.0 (dial back CCS2 where XCT2 overlaps)
      F3D4 = stack w/ -a 5.5 (dial back conversion; displaced saver)
      Expected naive-sum stack deltas vs CF (300): eff -.00018 dupB -.00750 dupT -.01492
      fakB -.00620 fakT -.00667 dupE -.00081; DISP1 sims F1 -8 + F2 -10 = -18 naive.
- [x] M3b DESCENT (6 runs, all dCF on 300):
      F3D1 (XCT2 3.5): eff +.00004 dupB -.00742 dupT -.01114 | D2 (XCT 4=off): eff +.00018
      dupB -.00480 (XCT 3.75 still buys -.0027 dupB for .00031 eff even after conversion) |
      D3 (CCS2 6): eff -.00013 (NO eff gain), dupT +.00154 fakT +.00055 worse = DOMINATED,
      keep CCS2 5.0 | D4 (a 5.5): eff -.00022 WORSE (conversion at 5.0 GAINS eff) = dominated
      except as displaced saver | D5 (XCT 3.5): eff -.00049 dupB -.00975 | D6 (XCT 3.5+XCT2
      3.5): eff -.00031 dupB -.00967 dupT -.01118 (additivity exact again).
      Displaced (a08 300): C0/D1/D2 all -18 DISP1, D5 -19 -> spend INSENSITIVE to XCT knobs,
      carried by -a/-MRB/-MRT/XCT2<=3.5 population.
      977 offset estimate (from F1W/F2W977): eff dCF runs ~-.00033 hotter on 977.
      => WINNER F3W977 = stack with -XCT2 3.5 (est -.0004 vs LST, inside gate)
      => ALTERNATE F3A977 = full stack (XCT2 3.0; dupT AT LST, eff est -.00055 vs LST,
         marginally over soft gate -- same flag situation the maintainer flagged on F2W).
- [x] M4 977 RECORDS (synth_ref/r_F3W977, r_F3A977; both wall ~797s):
      WINNER F3W977 (-XCT2 3.5): eff .80957 (L-.00030, INSIDE gate), dup .04812 BELOW LST,
      dupB .02314 (2.38x LST, was 3.15x), dupT .01690 (1.29x, was 2.14x), dupE .07099 BELOW,
      fak .04637 (L+.00099, 75% of CF gap closed), fakB .04869 (L+.00504), fakT .05313
      (L+.00772), fakE BELOW. nh +.0008 nhB +.030 nhT +.025 vs CF (no length regression).
      Displaced: DISP1 lead +716 -> +639 (spent 77 = naive F1+F2 sum, no overlap savings;
      81 lost/4 gained); DISP5 +542, DISP10 +400, DISP30 +93 (was +101).
      ALTERNATE F3A977 (-XCT2 3.0): eff .80929 (L-.00058, .00008 over soft gate),
      dupT .01318 (L+.00010 = AT LST PARITY), effT L-.00198, displaced -79; rest identical.
- [x] M5 plots: performance/finishline2_chainp-PU200_chainp-PU200/mtv/var/ (LST vs
      ChainFinal2=F3W977 on 977). Decisive TC_duplrate_etazoom eyeballed: barrel/transition
      visibly dropped toward LST, endcap peaks below LST.
- [x] M6 final report delivered as agent final message (this file mirrors the record).

## FINAL RECOMMENDATION
Binary: standalone/protoFINAL2/bin/chainproto (merged tree, all gates bit-exact).
CONFIG (winner): -T3F 0.10 -XC4 1 -RPSA 5.5 -EXR 4.0 -a 5.0 -a2 5.0 -a3 6.0
  -CCS 6.0 -CCS2 5.0 -XCT 3.75 -XCT2 3.5 -MRB -1.2 -MRT -1.2
ALTERNATE: same with -XCT2 3.0 (one line: dupT to LST parity for eff -.00058 vs LST).
Dial-backs if displaced 77 is over budget: -a 5.5 (~6-7 sims back, dupB +.0020,
eff -.00009), -CCS2 6.0 (~2-6 sims, dupT +.0015, no eff gain). -XCT knobs have NO
displaced effect (measured D1/D2/D5 all -18 +- 1 on 300).

## Key refs
- CHAINFINAL 300 = synth_ref/r_D1 (ROOT+json); 977 = synth_ref/r_W_D1.
- F1 300 winner r_F1B4, 977 r_F1W (synth_ref). F2 300 winner f2_ref/r_CMB7, 977 r_F2W977.
- LST hists: fin_ref/fin_base977_hists.root, rebase_ref/rb_base300_hists.root.
- Runner: synth_ref/syn_run.sh (BIN=protoFINAL2/bin/chainproto override).
- cmp: rebase_ref/cmp_branches.py ref new. Displaced: a08_ref/a08_distinct.py.
- TRAPS: createPerfNumDenHists stdout must go to a real file; may exit 1 on success.
