# E3 -- the adversarial combiner. Deliverables, contract tables and verdict.

## What to take

    e3_ref/e3_candidate.patch          THE CANDIDATE (E3-F). 202 lines, 3 files, against d950e4315be.
                                       `git apply --check` VERIFIED CLEAN at d950e4315be.
                                       NOT inert -- it is the shipping configuration, as the round asks
                                       (the change is unconditional, so inertness does not apply).
    e3_ref/e3_instrument_state.patch   what I MEASURED with: d950 + D6's typed-weld/far-cell/env patch
                                       + D1's floor (default changed to 0 so it is inert) + C3's twelve
                                       arbitration knobs hand-transcribed into the same override
                                       function. **This is the only binary in the round on which the
                                       weld axis, the gate axis, the post-gate axis and D1's floor can
                                       all be moved at once.** Measurement only -- do not ship.
    e3_ref/comp.py, e3_ref/tctypes.py  NEW: TC-collection composition by (class, nlayers) and by class,
                                       with fake/dup rate and mean nhitOT. Nobody had looked past the
                                       aggregate rates.
    e3_ref/rpu.sh, rcube.sh, time.sh   drivers (rpu/rcube auto-retry the ntuple-writer segfault at
                                       -s 4 then -s 2; time.sh is the interleaved CPU timing harness).
    e3_ref/meas/                       every ntuple, judge file and census behind every number below.

## E3-F: the ten constants

    thetaEdgeE2       inherit -> -0.5     (thetaEdge stays at the SHIPPED 0 -- D2's typed substitution;
                                           thetaEdgeE1 stays inheriting, loosening E1 is destructive)
    m3Theta4             4.0  ->  0.5
    m3Theta4D           -1.2  -> -2.5
    m3ThetaRB           -1.8  -> -1.2
    m3ThetaRT           -1.8  -> -1.2
    t4ExemptDcaMin       0.0  ->  1.5
    c25Theta             0.0  ->  2.0
    c25ThetaD           -2.0  -> -2.0      **UNCHANGED -- deliberately. See the verdict.**
    claimCountExclusive false -> true
    braidFrac            0.5  ->  0.2
    cc9MinShared          2   ->  1
Shipped heads. No new weights. No new kernel. No new clause. The only code the patch adds is the
per-edge-family weld bar (D4's `d4_typed_bar_inert.patch`, verbatim and inert on its own).

Env line that reproduces it on the instrument build with no rebuild:

    LST_CHAIN_E2=-0.5 LST_CHAIN_M4=0.5 LST_CHAIN_M4D=-2.5 LST_CHAIN_MRB=-1.2 LST_CHAIN_MRT=-1.2 \
    LST_CHAIN_Z=1.5 LST_CHAIN_C25=2.0 LST_CHAIN_FCX=1 LST_CHAIN_W=0.2 LST_CHAIN_CC9=1

## Baseline verification (mandatory, done before any A/B)
 * PU200RelVal 1000 evt, no env: every contract metric equal to `win_ref/all4_rv1000_hists.root` to 4 dp
   (eff .8099 dup .0479 fake .0470 dxy .8361/.5815/.2463/.0317 vxy .8027/.7227/.7128).
 * cube50 5000 evt, no env: all six bands AND every T5cl/pT5cl/pLS/T4cl count identical to
   `cube_ref/cube50_ours.root` to the unit.
 * 0 `error:` in `.make.log.1786168650`; lst_cpu md5 27e1199a329c40b80bcc028bba323330.

## THE FULL CONTRACT

### 1. PU200RelVal 1000 evt, CPU -p 0.8 -s 8
    metric                 GOAL      SHIPPED  MASTER   E3-F     delta    verdict
    eff overall (pt>0.9) >=.8099      .8099    .8100   .8100    +.0001   MET
    dup                  <=.0479      .0479    .0514   .0459    -.0020   MET
    fake                 <=.0454      .0470    .0454   .0452    -.0018   MET, below master's
    dxy [ 1, 5)           retain      .5815      --    .5724    -.0091   SHORT 0.66 sig
    dxy [ 5,10)           retain      .2463      --    .2622    +.0159   MET
    dxy [10,30)          >=.0545      .0317    .0545   .0393    +.0076   short of the aspiration
    vxy [ 1, 5)           retain      .8027      --    .8048    +.0021   MET
    vxy [ 5,10)           retain      .7227      --    .7241    +.0014   MET
    vxy [10,30)           retain      .7128      --    .7058    -.0070   SHORT 0.53 sig
    n TC (pt>0.9)                   1592804          1587859    -0.31%
### 2. cube50 5000 evt -- all six above shipped, dxy[1,5) AHEAD of master
    band        denom  MASTER     SHIPPED    E3-F        vs SHIP   vs MASTER
    vxy [ 1, 5)  166  .3253( 54) .2952( 49) .3133( 52)  +.0181    -.0120 (ns)
    vxy [ 5,10)  631  .2345(148) .2108(133) .2235(141)  +.0127    -.0111 (ns)
    vxy [10,30) 5521  .0639(353) .0389(215) .0418(231)  +.0029    -.0221 (4.8 sig)
    dxy [ 1, 5) 2009  .1195(240) .1220(245) .1284(258)  +.0064    +.0090 AHEAD
    dxy [ 5,10) 2598  .0778(202) .0616(160) .0689(179)  +.0073    -.0089 (ns)
    dxy [10,30) 9353  .0269(252) .0056( 52) .0090( 84)  +.0034    -.0180 (9 sig)
### 3. cube50_highPt 5000 evt -- all six above shipped, five of six above master
    vxy [ 1, 5)  227  .2115( 48) .2423( 55) .2731( 62)  +.0308
    vxy [ 5,10)  834  .1031( 86) .1211(101) .1247(104)  +.0036
    vxy [10,30) 7434  .0180(134) .0182(135) .0190(141)  +.0008
    dxy [ 1, 5) 2706  .0710(192) .0780(211) .0850(230)  +.0070   GOAL >=.0780 MET
    dxy [ 5,10) 3327  .0129( 43) .0177( 59) .0183( 61)  +.0006   GOAL >=.0177 MET
    dxy [10,30)12586  .0011( 14) .0002(  2) .0003(  4)  +.0002
### 4. cube50 SECOND HALF (-n -1 --nsplit_jobs 2 --job_index 1) -- all six above shipped, gains LARGER
###    out of sample than in sample
    vxy [ 1, 5)  132  .4318( 57) .4318( 57) .4470( 59)  +.0152
    vxy [ 5,10)  543  .2376(129) .2081(113) .2210(120)  +.0129
    vxy [10,30) 5418  .0670(363) .0432(234) .0493(267)  +.0061   (first half +.0029)
    dxy [ 1, 5) 1899  .1195(227) .1216(231) .1316(250)  +.0100   AHEAD of master +.0121
    dxy [ 5,10) 2576  .0807(208) .0637(164) .0738(190)  +.0101   (first half +.0073)
    dxy [10,30) 9113  .0281(256) .0068( 62) .0113(103)  +.0045   (first half +.0034)
### 5. Composition and CPU time -- see FINDINGS_FINAL [E3 06:20] and [E3 07:40].

--------------------------------------------------------------------------------------------
# UPDATE: TWO PATCHES, AND `e3_candidate_Q.patch` IS THE ONE I RECOMMEND

    e3_ref/e3_candidate.patch     E3-F. eff .8100 / dup .0459 / fake .0452. Meets eff, dup, fake AND
                                  both protected held-out goals. 202 lines. BUILD-VERIFIED end to end
                                  (35/35 PU fields + both cube samples reproduce the env arm exactly;
                                  lst_cpu md5 0dae206652b2b6348cd5c3e981d3d427).
    e3_ref/e3_candidate_Q.patch   **E3-Q. eff .8100 / dup .0454 / fake .0459.** 187 lines. Same
                                  end-to-end verification (35/35 + both cube samples;
                                  lst_cpu md5 9b47efadbfb4811c73b786b19ef5a7a7). Misses the fake goal
                                  by .0005 and gives up only 0.33 / 0.11 sigma of two PU200 bands
                                  against F's 0.66 / 0.53. **Under the project's own priority order
                                  (efficiency >> dup > fake) Q dominates F on both higher-priority
                                  axes.** Nine existing constants + D4's inert typed weld bar.

Q's constants (all pre-existing `ChainConfig.h` fields):
    thetaEdgeE2  inherit -> -0.5      m3Theta4       4.0 -> 2.0
    m3Theta4D      -1.2  -> -2.5      m3ThetaRB/RT  -1.8 -> -1.2
    c25Theta        0.0  ->  2.0      **c25ThetaD stays at the SHIPPED -2.0**
    maxClaimedMDs     1  ->  0        braidFrac      0.5 -> 0.2
    braidFracAlt   0.20  ->  0.10     cc9MinShared     2 -> 1
NOT TOUCHED, on purpose: `thetaEdge`, `thetaEdgeE1`, `t4ExemptDcaMin`, `c25ThetaD`, `claimCountExclusive`,
`t4DcaFloor`, `dcaSplit`/`dcaSplit2`/`t4FarMaxResid`, every eta-band delta, every weight header.
Env line: LST_CHAIN_E2=-0.5 LST_CHAIN_M4=2.0 LST_CHAIN_M4D=-2.5 LST_CHAIN_MRB=-1.2 LST_CHAIN_MRT=-1.2 \
          LST_CHAIN_FC=0 LST_CHAIN_WE=0.10 LST_CHAIN_CC9=1 LST_CHAIN_C25=2.0 LST_CHAIN_W=0.2

## Q's full contract (all four samples). Full precision from the BAKED build, zero env set.
  PU200RelVal 1000 evt : eff **.8099626** (>= .8099 MET) | dup **.0454347** (MET by .0025)
     fake **.0458573** (MISSES .0454 by .00046) | fake barrel .0513358 (shipped .0515533)
     dxy .5769981 (-.0045, 0.33 sig) / .2621648 (+.0159 MET) / .0392654 (+.0076)
     vxy .8039341 (MET) / .7256158 (MET) / .7113451 (-.0015, 0.11 sig)
     n TC 1590018 = -0.17% BELOW shipped | n T4-class 66247 (shipped 24766)
  cube50 5000        : .3133 / .2235 / .0431 / **.1344** / .0689 / .0090  -- all six above shipped;
                       dxy[1,5) is +.0149 AHEAD of master's .1195
  cube50_highPt 5000 : .2643 / .1259 / .0195 / **.0865** / **.0177** / .0003 -- all six at or above
                       shipped; BOTH protected goals MET
  cube50 2nd half    : .4394 / .2210 / .0511 / **.1359** / .0738 / .0113 -- all six above shipped_H2;
                       dxy[1,5) AHEAD of master_H2 by +.0164
  CPU time (measured for the F sibling, interleaved, 3 pairs at s=1 + 2 at s=8): chain block
                       +1.2 ms/evt (+0.9%), total +0.09% to +0.26% == inside shipped's own 6.0 ms
                       run-to-run spread. GPU NOT measured.

## GOALS NOT MET BY EITHER PATCH, STATED PLAINLY
  PU200 dxy[10,30) .0393 vs the .0545 goal, and cube50 dxy[10,30) .0090 vs .0269. D6 priced those two
  at PU200 fake +.0460 and +.0136; the fake budget is -.0018. **They are not reachable inside the fake
  ceiling and neither patch claims them.** cube50 vxy[10,30) (-.0208) and dxy[10,30) (-.0180) remain
  significant deficits against master.
