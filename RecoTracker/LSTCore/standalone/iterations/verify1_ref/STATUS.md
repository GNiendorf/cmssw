# VERIFIER-1 STATUS (adversarial verification of A01, A06, A11)

Scratch dir: verify1_ref/. Tools written here (independent re-implementations, NOT
compare_ab.py imports):
* v1_metrics.py  -- all headline/region/band/length metrics straight from a hists.root
* v1_bands.py    -- displacement-band NUMERATOR TRACK COUNTS (so "bit-identical" claims
                    can be checked as integers, not as 5-decimal rates)
* v1_run.sh      -- frozen-line runner, configurable BIN, output into verify1_ref/
* launch1.sh     -- launches the six re-runs

## M0 conventions established
* Scoreboard `eff`/`dup`/`fake` = compare_ab's *_overall_incut (from the _eta hists), NOT
  the _pt sums. Confirmed against LST300 = .80988/.05179/.04476.
* LST references: 300 = rebase_ref/rb_base300_hists.root; 977 = fin_ref/fin_base977_hists.root
  (eff .80987 dup .05138 fake .04538 nh 10.1498/10.0094/3.5567). Both re-derived.
* Assembled baselines: 300 = fin_ref/r_FINBASE (.80992/.06230/.05551, nTC 618793);
  977 = fin_ref/r_W_X4 (.80905/.06184/.05607, nhB 9.8006 nhT 9.8837 nhE 3.5572, dupB .04294).

## M1 workspace integrity
* protoA01: only main.cc differs from protoFIN. protoA01b: only main.cc. Added lines
  243 (A01) / 266 (A01b), not the claimed "+273".
* protoA06: binary md5 519b0abc34a28cd1e803d6b9407ef224 == protoFIN's; no differing source
  file. BUT there is a nested 21 MB copy protoA06/protoFIN/ (harmless clutter).
* protoA11: main.cc AND AttachDelivery.cc AND AttachDelivery.h differ from protoFIN.
  a11_ref/protoA11_vs_protoFIN.diff contains ONLY the main.cc hunk (129 lines) -- the
  -T3F implementation (AttachDelivery.cc +14, AttachDelivery.h +8) is NOT in the recorded
  diff. ISSUE.

## M2 artifact integrity
* a06_ref/r_GATE_hists.root MISSING; r_GATE.root is 1.3 MB (normal ~20 MB) and r_GATE.log
  stops at evt 6 -- the GATE artifacts were clobbered by an aborted re-run. GATE is one of
  the four "RECOMMENDED TAGS TO QUOTE". Its r_GATE.json (23:14) survives and DOES read
  .80992/.06230/.05551/nhB 9.80254/nTC 618793 = FINBASE exactly, and the a06_len.py GATE
  row in len_batch1.txt is byte-identical to the FINBASE row. So the gate passed; the raw
  artifact no longer supports re-derivation. ISSUE (recoverable).
* a11_ref: GATE2/GATE3/A30/CM0 have no hists (GATE4, the quoted one, is intact).
* a01_ref: W_R_O4 has no hists (disclosed and explained).

## M3 re-derived scoreboards -- ALL THREE AGENTS' HEADLINE TABLES REPRODUCE
Every headline/region/length number quoted by A01, A06 and A11 that I re-derived from the
raw hists agreed to the last printed digit, on both samples, with the exceptions in M4.

## M4 discrepancies found (details in the final report)
1. A01 "Displaced is bit-for-bit unmoved in every band on both samples" -- FALSE on 977:
   vxy[1,5) 3653 -> 3648 numerator (-5 tracks). True on the 300.
2. A06 "v510/v1030/d15 each move by <= 3 tracks" (stated of the 977) -- FALSE on 977:
   v1030 -12 tracks (2929->2917), d15 -7 (1763->1756); also v15 -8, which is not quoted.
   True on the 300 (-3/-3/-1).
3. A06 inner-arm cost "d15 -.0268 (11.5 tracks on a 430 denominator)" -- the RATE is right,
   the counts are not: d15 denom is 897 and the loss is 24 tracks. (v1030 "16 on 1249" is
   correct.) Error is against their own case, conclusion unaffected.
4. A06 "63.3% of our barrel long rows carry 12+ OT hits vs LST's 81.3%" -- the measured
   f12B values in their own artifacts are .600 (ours) and .789 (LST); .789 is quoted
   correctly later in the same summary.
5. A06 "80 configurations ... n0B swings from 28.7 to 59.7" -- the sweep files hold 90 rows
   and n0B actually spans 17.9 to 122.7. f12 invariance itself CONFIRMED (1 distinct triple
   0.600/0.445/0.480 over all 90).
6. A11 "barrel duplicate rate goes .04393 -> .02940" -- the assembled baseline's 977 dupB
   is .04294, not .04393 (.04393 is the -T3F-only run). True delta .04294 -> .02940.
7. A11 "EVERY displaced band bit-identical" (purist / -T3F-only / +XCT variants) -- FALSE
   at the 1-4 track level: purist 977 v15 -1 v510 -1; -T3F-only 977 v15 +2 v510 -1, 300
   v15 +1; +XCT375 977 v15 -4 v510 -1. Substance (displaced protected) holds.

## M5 re-runs (fresh, my own runner, in verify1_ref/)
* VA11_BEST (protoA11, assembled baseline + -T3F 0.10 -RPSA 5.0 -RPST 6 -a 6.0, WITHOUT
  the -XCD 2 that every A11 run carried): reproduces a11_ref/r_C2_A60R50 EXACTLY on all 29
  metrics incl. nTC 613601. Confirms the shipped line and confirms -XCD 2 inert.
* ALL SIX re-runs finished and are BIT-IDENTICAL at branch level (cmp_branches.py, 33/33,
  no tolerance):
    VA01_GATE  (protoA01b, baseline flags, NO -XCD 3)      == fin_ref/r_FINBASE
    VA01_BEST  (protoA01b, +-XCO 5, NO -XCD 3)             == a01_ref/r_O_5   [which was
               produced by protoA01 WITH -XCD 3 -- so this one run confirms the shipping
               line, the protoA01b==protoA01 claim, and -XCD 3 inertness at once]
    VA06_GATE  (protoA06, baseline flags)                  == fin_ref/r_FINBASE
               [this restores the evidence a06_ref/r_GATE lost]
    VA06_BEST  (protoA06, +-EXW 0.50 -EXR 4.0 -L 5.0)      == a06_ref/r_H_W50L5
    VA11_GATE  (protoA11, baseline flags)                  == fin_ref/r_FINBASE
    VA11_BEST  (protoA11, +-T3F .10 -RPSA 5 -RPST 6 -a 6, NO -XCD 2) == a11_ref/r_C2_A60R50
  Every recommended configuration and every no-op gate REPRODUCES FROM SCRATCH.

## M6 branch-level checks I ran myself (rebase_ref/cmp_branches.py, no tolerance)
* a01_ref/r_GATEB (protoA01b, new flags off) vs fin_ref/r_FINBASE: 33 IDENTICAL / 0 DIFFER.
* a11_ref/r_GATE4 vs fin_ref/r_FINBASE: 33 IDENTICAL / 0 DIFFER.
* A01 subsumption claim: r_R_O5 (-XC 1 -XCO 5) vs r_T5_O5 (-XC 3 -XCT 5 -XCO 5):
  33 IDENTICAL / 0 DIFFER. CONFIRMED.
* verify1_ref/r_VA11_BEST (my fresh run, -XCD 2 dropped) vs a11_ref/r_C2_A60R50:
  33 IDENTICAL / 0 DIFFER, nTC 613601. CONFIRMED, and -XCD 2 is inert.

## M7 diagnostics re-derived from raw logs (not their tables)
* A01 -XCD 3 "XC reach" block in r_DIAG.log: class-A survivors 0.02 noPair / 0.26
  noDelivTgt / 9.33 / 4.21 / 6.48 / 6.05 = 26.35. So 26.07 have a scored pair, the
  structural floor is 0.28/evt, and the >=4 bucket predicts 6.05 A + 3.89 B; measured
  DIAG->O_4 is A 26.3->20.3 (6.0) and B retire 5.8->9.7 (3.9). EXACT. CONFIRMED.
* A01 "-XCT 4 -> 3.5 pays .00088 eff for .00305 dup (3.5:1)": the point is fin_ref/r_E_A6X35
  (-AT3 6 = the default, -XCT 3.5) = .80904/.05925 vs FINBASE .80992/.06230. CONFIRMED.
* A06 mechanism counters in fin_ref/r_FINBASE.log: trim 241.7/evt, extension 60.1/evt,
  chi2-rejected 2039 = 6.80/evt, uniq/fit-rejected 0. CONFIRMED. E_W50 vs C_A outer
  extension counts 36277 = 36277. CONFIRMED.
* A06 f12 invariance: 90 rows across len_finref_sweep.txt + len_xcref_sweep.txt, exactly
  ONE distinct f12 triple (0.600 0.445 0.480). CONFIRMED (they said 80 rows).
* A11 a11_types.py on r_FINBASE: type-5 93.3/evt fakeFrac .1739, 16.23 of 88.88 fakes;
  on r_F010: 80.8/evt fakeFrac .0615; LST rb_base300 type-5 fakeFrac .0368. CONFIRMED.
* A11 -RPSA purity: F010 -> R_F10R50 changes ONLY the type-8 count (625.2 -> 620.4);
  types 4/5/7/9 counts and fake counts unchanged. CONFIRMED.
* A11 -a monotonicity on v1030 (300): 6.875 .74139 / 6.0 .73899 / 5.5 .73739 / 5.0 .73579
  / 4.0 .73018. CONFIRMED.
* No agent trained anything: no new weight/model file in any workspace (protoA06's 8 hits
  are inside its stray nested protoFIN/ copy and are byte-identical to protoFIN's).

## M8 hidden regressions in bands NOT quoted (977, vs the assembled baseline)
* A01 -XCO 5: effB -.00049 -> .92405, which is BELOW LST's .92430. Their "at no barrel
  efficiency cost (effB .92580 vs LST .92557)" is a 300-evt statement that INVERTS on the
  977. fakB +.00048 (overall fake +.00007 hides it).
* A06 -EXW .5 -EXR 4 -L 5: fakT +.00158 -- the largest regional move anywhere in their
  result and not quoted (only overall fake +.00037 is). dupB +.00009 (overall dup improves).
* A11 -T3F .10 -RPSA 5 -a 6: nhE 3.5572 -> 3.5473, i.e. endcap mean OT length goes from
  +.0005 ABOVE LST to -.0093 BELOW it. The purist variant does the same (-.0082). Not
  quoted; the finish line names track length explicitly. Mechanism is obvious (-T3F
  removes 6-hit pT3-class rows from a 3.55-mean endcap population).
* A01 and A11 move the SAME barrel-duplicate population: dupB delta -.01352 (A01) and
  -.01353 (A11). Almost certainly NOT additive.
