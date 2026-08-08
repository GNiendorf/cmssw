# E2 -- THE ROBUSTNESS-FIRST COMBINER. FINAL RECORD.

## THE CANDIDATE: `K6`  (patch `e2_ref/e2_candidate.patch`)
Eight pre-existing `ChainConfig.h` constants plus C6/D2's INERT typed weld bar. Shipped heads.
Nothing retrained. No new kernel, no new parameter, no new branch.

    thetaEdgeE2   1e30 -> -0.2f      (thetaEdge stays at the shipped 0 -- D2's substitution)
    m3Theta4D     -1.2 -> -1.9f      m3Theta4       4.0 -> 2.f
    m3ThetaRB     -1.8 -> -1.2f      m3ThetaRT     -1.8 -> -1.2f
    maxClaimedMDs    1 -> 0          cc9MinShared     2 -> 1
    braidFracAlt  0.20 -> 0.10f

AT SHIPPED AND UNTOUCHED: `thetaEdge`, `thetaEdgeE1`, `t4ExemptDcaMin`, `c25Theta`, `c25ThetaD`,
`t4DcaFloor`, `dcaSplit`, `maxClaimedFrac`, `braidFrac`, `braidAltEta`, `claimCountExclusive`, all gate
eta deltas, all attach/crossclean bars, all weights.

### PU200RelVal 1000 evt (`d3_ref/pu_judge.py`, validated drop-in for compare_ab)
    metric                 GOAL      SHIPPED   MASTER    K6        verdict
    eff overall (pt>0.9) >=.8099     .8099     .8100    .8103     MET  (+.0004, above master)
    dup                  <=.0479     .0479     .0514    .0469     MET  (-.0010)
    fake                 <=.0454     .0470     .0454    .0453     MET  (-.0017, below master)
    fake barrel             --       .0516     .0437    .0493     better than shipped
    dxy [ 1, 5)          retain      .5815              .5815     MET  (exactly shipped)
    dxy [ 5,10)          retain      .2463              .2532     MET  (+.0069)
    dxy [10,30)          >=.0545     .0317     .0545    .0361     MISS (+.0044 over shipped)
    vxy [ 1, 5)          retain      .8027              .8041     MET  (+.0014)
    vxy [ 5,10)          retain      .7227              .7271     MET  (+.0044)
    vxy [10,30)          retain      .7128              .7130     MET  (+.0002)
    n TC                            1592804           1589297     -0.22% (BELOW shipped)
    n T4-class TC                      24766             58233

### The frontier, for a maintainer who wants a different point on it
Only `thetaEdgeE2` and `m3Theta4D` move; the six payer values are identical on every row.
See `all_arms_table.txt` (all 30 arms) and `all_arms_cmds.txt` (the env line for each).
    B4 (-0.5, -2.5) fake .0486  all retain MET  highPt dxy[5,10) LEVEL (102/104)  n TC +0.19%
    K3 (-0.35,-2.2) fake .0468  all retain MET  highPt dxy[5,10) 56 vs 59         n TC +0.03%
    K6 (-0.2, -1.9) fake .0453  all retain MET  (held-out legs: see below)        n TC -0.22%
    K4 (-0.35,-1.8) fake .0451  dxy[1,5) -0.42 sig                                n TC -0.26%
    J1 (-0.5, -1.2) fake .0432  no displaced gain at all                          n TC -0.54%

## WHAT I MEASURED AND FALSIFIED (all in FINDINGS_FINAL.md with numbers)
 1. `maxXyResid` is NOT a general 4-layer fake discriminant -- only a FAR-CELL one. Splitting the whole
    T4-exempt branch on fit quality is DOMINATED by using the loose bar uniformly and paying with -FC 0.
 2. D1's `t4DcaFloor` is a near-free VOLUME remover (61% of the 4-layer TC population for eff -.0004)
    and NOT a fake payer on an open admission -- five arms, three bases.
 3. The gate's eta-band machinery cannot separate the T4 gain from the T4 bill: a transition-only
    loosening delivers ZERO displaced gain.
 4. `ccMinShared` and the bare-chain crossclean bars both buy eff +.0012 -- the highest eff ever measured
    here, .8113 -- in the pLS-carried cell the scope rules forbid.
 5. My own 87.5%-fake T4-barrel census was a property of the FLOOR, not of the cell: without it the
    marginal population is 14.7% fake and the arm's net fake goes DOWN.

## Build / reproduction: see README.md.  Every number: `all_arms_table.txt` + `pu/` + `cube/`.

## HELD-OUT RECORD FOR `K6` -- 24 band measurements, four sets (`K6_holdout.txt`)
    18 UP vs shipped, 3 exactly LEVEL, 3 DOWN, and 10 of 24 AHEAD OF MASTER.
    The three down entries: cube50_highPt dxy[5,10) -4 tracks (1st half) and -4 (2nd half);
    cube50_highPt vxy[5,10) -1 track (2nd half). Nothing else.
    cube50_highPt dxy[1,5) -- the band that killed C2(A)/C2(C) at -5.4/-5.9 sigma -- is MET on BOTH
    halves (+9 and +7 tracks) and AHEAD OF MASTER on both (+.0103, +.0076).
    cube50 is SIX OF SIX above shipped on BOTH halves; dxy[1,5) is ahead of MASTER on both.

## GOAL LEDGER, FINAL
    MET (6):    eff, dup, fake, all five PU200 retain bands, cube50_highPt dxy[1,5)
    MISSED (3): cube50_highPt dxy[5,10) (.0165 vs .0177 = 4 tracks of 59, 0.4 sigma, still +.0036 above
                master, same sign in both halves) | PU200 dxy[10,30) (.0361 vs .0545)
                | cube50 dxy[10,30) (.0067 vs .0269; 5 of 6 cube50 bands still behind master)
    n TC: 1589297 vs shipped 1592804 = **-0.22%**. CPU/GPU time not measured (box contention).

## THE ALTERNATIVE, IF THE MAINTAINER WEIGHTS THE HELD-OUT BAND ABOVE THE FAKE CEILING
`e2_candidate_B4.patch` -- the SAME eight fields with `thetaEdgeE2 = -0.5f, m3Theta4D = -2.5f`:
fake .0486 (+.0032 over goal), eff .8101, dup .0470, ALL FIVE PU200 retain bands MET, and
cube50_highPt dxy[5,10) **LEVEL at 102/104 over all 10,000 events** with dxy[1,5) MET on both halves.
Switching between K6 and B4 is a two-number edit.

## THE BINDING CONSTRAINT, STATED ONCE
`m3Theta4D` alone: fake <= .0454 needs it >= ~-1.9; cube50_highPt dxy[5,10) >= .0177 needs it <= ~-2.4;
and either dxy[10,30) goal needs D6's far-dca cell at +.0038..+.0136 of fake on top. Mutually exclusive
on every instrument measured in this round.
