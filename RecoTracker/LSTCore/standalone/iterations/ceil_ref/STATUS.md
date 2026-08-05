# M22 CEIL STATUS (durable milestone log)

Workspace: standalone/protoCEIL (my build), artifacts standalone/ceil_ref/.
Runner: ceil_ref/ce_run.sh (identical frozen M19/P2.5 flagship line to ta_run.sh /
ga_run.sh / gc_run.sh, my binary + my artifact dir). Offline lab: ceil_ref/ce_lab.py.
Gate: P25BASE eff .81303 | dup .05188 | fake .04636 | nTC 614277 (300 evts PU200RelVal).

THE QUESTION: sibling A's structural ceiling was computed on the UNCONDITIONED candidate
universe with POST-HOC vetoes. Sibling C proposed conditioning the universe AT ENUMERATION
instead. Does that break the ceiling?

## CE0 -- SETUP (DONE)
protoCEIL = cp -r prototype protoCEIL (M20 tree, r2 head resident). Built with the FULL env
sequence `source setup.sh; cmsenv; source setup.sh` (the protoC build trap: setup.sh alone
links CMSSW_14_2_0_pre4 / ROOT 6.30 and dies in TFile::Open).

## CE1 -- CODE ADDED (all flag-gated, all defaults == the frozen behaviour)

 1. `AttachDelivery.h` GeneralAttachParams gains `const std::vector<char>* t3EnumVeto`
    (nullptr = frozen). `AttachDelivery.cc` gaStageT3 zeroes the bare mask on it AFTER
    k8BuildBareT3Mask and BEFORE k8EnumeratePrefilteredPairsGeneral -- 5 lines, one loop.
 2. `main.cc` -CE / -CEN / -CEL: ONE ownership map built once per event, immediately
    before gaStageT3, from everything ALREADY DELIVERED (assembled chain TCs through the
    chains' MD CSR + the carried pixel rows that survived the M16 suppression). That is
    bit-for-bit the -CC -CCP 1 universe at MD granularity; it is available there because
    nothing between that point and the -CC block changes it (stage B pushes no OutTC and
    m16RefreshSupp() runs after both). Per MD: claimed bit + the LAYER MASK of the owner
    holding it (first owner wins, GEN-C -CCL semantics). Veto rule:
    `nSharedMDs >= -CEN` OR (`-CEL 1` AND one owner of a shared MD covers every layer the
    T3 occupies). Ownership map, O(1) per T3, no candidate-vs-candidate comparison, no
    proximity term.
 3. `main.cc` -QD <path>: the GEN-A audit stream, ported and extended. Per event the
    accepted sims (+charge), what the pipeline already delivers in BOTH sim spaces (A/F
    records), the conditioned target-universe size, and per bare-T3 delivery winner its
    logit / pt / eta / every >0.75 sim match (full:accepted:frac) / t3-DNN scores / 3 MD
    rows / 4 pixel-hit rows / pre-claimed unit counts at MD and hit granularity / its own
    layer mask / the owner layer mask of each of its MDs. The last two are new and are
    what let GEN-C's containment rule be replayed offline exactly.

## CE2 -- NO-OP GATE (PASS)
30 evts, frozen flag line, all new flags unset, protoCEIL vs the untouched prototype
binary: 30/30 per-event counter lines BYTE-IDENTICAL (only the ms timing fields differ),
scoreboard identical -- eff .8011 dup .0532 fake .0374 nTC 53716, which is exactly the
prior agents' NOOP2 numbers. Artifacts t_NOOPREF.*, t_NOOPNEW.*.

## CE3 -- THE FIVE AUDIT DUMPS (300 evts each, -AT3 -1e9 -RDT 0 -CC 0)
One low-margin dump per universe carries the WHOLE threshold curve (gaStageT3's per-target
pick is an argmax and its pLS contention keeps the global argmax, so the winner set at
margin theta is exactly {winners with logit >= theta} -- the GEN-A/GEN-C method, verbatim).
  U0  -CE 0                  the UNCONDITIONED universe (== A's and C's)
  C1  -CE 1 -CEN 1           conditioned, any owned MD suppresses
  C2  -CE 1 -CEN 2           conditioned, 2 of 3 owned MDs suppress
  C3  -CE 1 -CEN 3           conditioned, all 3 owned MDs suppress
  CL  -CE 1 -CEN 9 -CEL 1    conditioned, GEN-C layer containment only

## CE4 -- THE DECISIVE C++ A/B (launched alongside the dumps)
Post-hoc veto vs the SAME rule at ENUMERATION, everything else identical (frozen flagship
line + A's finalist dedup stack -RDT 1 -CC 1 -CCG 1 -CCP 1 -CCR 2, r2 head, -AT3 6):
  p_n1  -CCN 1                    post-hoc only            (== M20 d_n1 with -CCR 2)
  e_n1  -CCN 1 -CE 1 -CEN 1       conditioned + post-hoc
  p_n2  -CCN 2                    post-hoc only
  e_n2  -CCN 2 -CE 1 -CEN 2       conditioned + post-hoc

## CE5 -- INTERIM (100 evts of each dump; 300-evt tables follow)

COMPOSITION at theta 6, no dedup of any kind (rows/evt):
  universe          targets/e   rows/e   dupChain  dupEach   fake    NEW
  GEN-C reference        -      1132.7     884.4     50.6    90.8   106.9
  U0 unconditioned     40089     1123.7     894.0     23.8    91.5   114.5
  C1 -CEN 1            17916      219.5      53.1     20.3    41.7   104.4
  C2 -CEN 2            29444      513.6     302.0     21.9    79.9   109.9
  C3 -CEN 3            38414     1032.6     804.7     23.6    90.1   114.2
  CL -CEL 1            19859      332.2     154.0     20.6    51.5   106.0
U0 reproduces GEN-C's 1132.7 = 884.4/50.6/90.8/106.9 decomposition, which validates the
instrument and the classifier. CONDITIONING WORKS AS ADVERTISED ON THE UNIVERSE: -CEN 1
takes it from 1124 to 220 rows/evt (5.1x), redundancy 79.6% -> 33.4%, absolute fakes
91.5 -> 41.7, while NEW rows fall only 114.5 -> 104.4 (-8.8%).

## CE6 -- THE MECHANISM, MEASURED (ce_mech.py, 100 evts, theta 6)
Conditioned rows joined to the unconditioned universe by pixel seed:
  SAME    = same seed, same T3 (the rule changed nothing)
  MOVED   = same seed, DIFFERENT T3 -- the pLS LIBERATION the hypothesis is built on
                                   rows/e     fake      dup      NEW
  C1  SAME                          173.7     27.5     43.5    102.6
  C1  MOVED                          45.8     14.2     29.9      1.7
  C2  MOVED                         207.1     25.3    178.6      3.1
  CL  MOVED                          88.3     15.3     70.9      2.0
THE LIBERATION HAPPENS AND IT IS 96-98% WASTE. Freeing a seed from a redundant T3 gives it
back to another redundant or fake T3 in 44 of every 46 rows, because the redundancy lives
in the SEED (a duplicate pixel seed of an already-delivered track), not in which T3 the
seed happened to pick. An MD-ownership map cannot see that: the replacement T3 really is
unowned.

## CE7 -- THE 300-EVT CEILING TABLE (ceil_ref/table_300evt.txt)

COMPOSITION at theta 6, no dedup of any kind (rows/evt), 300 evts:
  universe          targets/e   rows/e   dupChain  dupEach   fake    NEW   %NEW
  GEN-C reference        -      1132.7     884.4     50.6    90.8   106.9   9.4%
  U0 unconditioned     40427     1132.7     902.0     24.1    90.8   115.8  10.2%
  C1 -CEN 1            17980      220.4      53.1     20.5    41.3   105.5  47.9%
  C2 -CEN 2            29712      518.4     305.2     22.1    80.2   111.0  21.4%
  C3 -CEN 3            38762     1041.4     812.4     23.9    89.5   115.6  11.1%
  CL -CEL 1            19944      333.5     154.0     20.8    51.5   107.2  32.1%
U0 reproduces GEN-C's decomposition to the row (1132.7 both). CONDITIONING SOLVES THE
COMPLEMENTARITY COMPLAINT: -CEN 1 takes the class from 10% new rows to 48% new rows, past
LST's own 151.7-row/evt operating point in redundancy terms, while keeping 91% of the NEW
rows (115.8 -> 105.5) and cutting absolute fakes by 55% (90.8 -> 41.3).

ORACLE CEILING (perfect truth-based selection, no threshold), 300 evts. "maxRealEff" uses
sibling A's own convention, (17642 + 0.9 * labNewSims) / 22784, so the columns are directly
comparable to his A5 table.
  ownership rule       A5 lab  U0 lab   U0 maxReal | COND lab  COND maxReal | gain
  no dedup               1061    1061      .81623  |    --          --      |  --
  veto-on-3              1023    1042      .81548  |   1062       .81627    | +20 / +.0008
  veto-on-2               929     927      .81093  |   1013       .81433    | +86 / +.0034
  layer containment        --     871      .80872  |    938       .81137    | +67 / +.0027
  veto-on-1               826     834      .80726  |    897       .80975    | +63 / +.0025
My U0 column reproduces A5 (1061 exactly, 927 vs 929, 834 vs 826, 1042 vs 1023), which is
what makes the conditioned column comparable.

## CE8 -- THE DECISIVE C++ A/B (300 evts, ceil_ref/ce_board.py)
Everything identical except -CE. Frozen M19/P2.5 flagship line + -RT3 1 -CF 1 -CFC 1
-RDT 1 -CC 1 -CCG 1 -CCP 1 -CCR 2, r2 head (protoCEIL carries no dedicated T3 head).

  tag       rule            -AT3     eff     dup    fake     nTC  cand/e  deliv/e
  P25BASE   (LST's pT3s)      --  .81303  .05188  .04636  614277     --      --
  p_n1      post-hoc N=1       6  .80653  .04793  .05661  614654  1132.7   135.5
  e_n1      COND N=1           6  .80873  .06176  .06010  622188   220.4   155.0
  e_n1L     COND N=1 +layer    6  .80873  .06176  .06010  622188   220.4   155.0
  e_n1_a75  COND N=1         7.5  .80065  .05847  .04975  604049   122.0    86.9
  e_n1_a9   COND N=1           9  .78165  .05588  .04875  586607    37.6    24.0
  p_n2      post-hoc N=2       6  .80925  .07238  .06401  628021  1132.7   181.5
  e_n2      COND N=2           6  .81193  .13542  .06839  653055   518.4   261.9

READINGS
 R1. CONDITIONING BUYS +.0022 OF REAL EFFICIENCY AT N=1 (+.0027 at N=2) AND COSTS +.0138
     OF DUPLICATE RATE (+.0630 at N=2). The lab predicted +.0025 at N=1 from the ceiling
     delta; C++ measured +.0022. The offline laboratory is calibrated.
 R2. POST-HOC N=1 PASSES dup (.04793). CONDITIONED N=1 DOES NOT (.06176). The conditioned
     margin sweep NEVER returns under the gate: at -AT3 9, with only 24 deliveries/evt,
     dup is still .05588 -- WORSE than r_off's .05254 with zero deliveries. So the extra
     duplicates are not the deliveries, they are the carried bare-pLS rows that -RPS stops
     destroying once the head scores 5x fewer pairs (pLS retired 8166 -> 6492 at AT3 6,
     and the effect persists at every margin). Conditioning's efficiency gain and its
     duplicate cost are THE SAME MECHANISM.
 R3. e_n1 (.80873/.06176) DOMINATES p_n2 (.80925/.07238) on duplicate rate at nearly equal
     efficiency, so conditioning DOES fill part of GEN-C's C11 hole (the N-rule family has
     no point between dup .0495 and .0700). It fills it with a point that still fails.
 R4. -CEL 1 IS A STRICT NO-OP UNDER -CEN 1: e_n1L reproduces e_n1 byte-for-byte on all 300
     per-event counter lines. -CEN 1 subsumes layer containment at enumeration exactly as
     GEN-C measured -CCN 1 subsumes -CCL post-hoc.
 R5. ARCHITECTURALLY CONDITIONING IS MUCH CHEAPER. Candidates 1132.7 -> 220.4, pixel-side
     dedup work 666.0 -> 59.4 revocations/evt, OT-side 331.2 -> 5.9. The post-hoc machinery
     stops being the load-bearing stage. (No timing claim: the runs were parallel.)

## CE9 -- VERDICT: CONDITIONING MOVES THE CEILING, IT DOES NOT BREAK IT

  ownership rule      U0 maxRealEff   COND maxRealEff   gain    gate .81303
  no dedup                .81623            --            --    (dup .28, unusable)
  veto-on-3               .81548          .81627        +.0008  (dup .235, unusable)
  veto-on-2               .81093          .81433        +.0034  (dup .135 measured, unusable)
  layer containment       .80872          .81137        +.0027  (dup .066, unusable)
  veto-on-1               .80726          .80975        +.0025  ONLY dup-passing rule
                                                                 -- SHORT BY .0033
NO ownership rule reaches eff >= .81303 at dup <= .052, conditioned or not. The
conditioned oracle at the only dup-passing rule is .80975 WITH PERFECT TRUTH-BASED
SELECTION; the gate is .81303. Conditioning is worth about +.0025 of the .0045-.0090 gap,
i.e. it closes at most a third of the smallest version of it and none of the largest.

WHY, MEASURED (CE6): the liberation conditioning enables is 96% waste. The redundancy that
survives conditioning lives in the PIXEL SEED (a distinct pixel seed of a track a chain
already delivered), not in which T3 the seed picked. An MD-ownership map cannot see it --
the replacement T3 genuinely is unowned -- and the pixel seed-family map cannot see it
either, because a distinct seed does not share 2 pixel hits with the delivered one.

## CE10 -- ARTIFACT INDEX (ceil_ref/)
  STATUS.md            this file
  ce_run.sh            runner (protoCEIL binary, ceil_ref outputs)
  ce_dumps.sh ce_ab.sh ce_front.sh    the three 300-evt batches
  ce_lab.py            offline laboratory (GEN-A methodology + GEN-C's layer rule)
  ce_table.py          the two deliverable tables       -> table_300evt.txt, table_200evt.txt
  ce_mech.py           the pLS-liberation diagnostic
  ce_board.py          scoreboard (ta_board.py + the -CE ledger column)
  aud_{U0,C1,C2,C3,CL}.txt   the five 300-evt audit streams (~250 MB each)
  t_<tag>.{root,log,json,cmd}, t_agg_<tag>.txt   every run
REPRODUCE:
  cd standalone/protoCEIL && (source ../setup.sh; cmsenv; source ../setup.sh) && make -j 12
  bash ceil_ref/ce_run.sh MYTAG -RT3 1 -CF 1 -CFC 1 -RDT 1 -CC 1 -CCG 1 -CCP 1 -CCR 2 \
       -CCN 1 -AT3 6 -CE 1 -CEN 1
All new flags default to the pre-M22 value and P25BASE reproduces byte-identically (CE2).
