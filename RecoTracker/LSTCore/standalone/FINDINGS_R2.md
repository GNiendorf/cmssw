# Shared findings log - ROUND 2 (agents R1 R2 R3 R4 R5)

Read-write for all five agents. This is the round's only shared channel.

--------------------------------------------------------------------------------------------
## HOW THIS ROUND IS STRUCTURED (read before anything else)

Last round four agents independently converged on the same mechanism (a trained bare-seed
retirement head) and three of the four results were therefore worth nothing on top of each
other. This round the directions are ASSIGNED, because we now know what we want looked at:

  R1  THE ATTACH HEAD - harvest LST's pT3/pT5 matching features (radius pull, the three
      chi-squareds) plus |eta| and pT into the head we already have; TC-level labels; WP-table
      calibration in eta/pT. The flagship.
  R2  DELETION / SIMPLIFICATION - apply the truncated-residual insight to -RPSA and -T3F, and
      find machinery that can be REMOVED at zero or positive cost. Target: one head, one cut.
  R3  FAKE RATE, barrel + transition (.0551 / .0592 vs LST .0437 / .0454) - the half of the gap
      nobody touched last round.
  R4  THE SEED-ADMISSION UNIVERSE - -ZP8 (84% of barrel duplicate rows are its post-deletion
      additions), the -RD / -RDT seed dedup family, pLS quality. Which seeds are candidates
      at all, before attach.
  R5  CHAIN CONSTRUCTION UPSTREAM - weld / gate / K9 claim / K10 assembly. Do we build shorter
      and more duplicated chains than LST builds T5s (mean nhitOT 6.435 vs master 6.52), and
      does that drive both the barrel dup and the barrel fake excess?

Work your own direction. If your work needs a change inside someone else's, say so in this file
with the measurement that shows why, rather than making it yourself - a change measured on top
of another agent's uncommitted tree cannot be composed with anything.

ARCHITECTURE CONSTRAINT (maintainer, hard): the target end state is ONE network that answers
"is this pLS and this OT object the same track", plus one cut on it. We currently have that
network (the attach head). A SECOND trained component is not acceptable as a "win" unless it
REMOVES more machinery than it adds - and last round's measurement of exactly that idea (a
trained retirement head) came in at ~.002 dupB over the free window fix, which is NOT enough.
Improving the head we already have (features, labels, training distribution) IS the preferred
direction. Do not spend the round training a second MLP.

SCOPE RULE (maintainer, 2026-08-06, hard): a change whose benefit would accrue to LST MASTER as
much as to us is NOT a deliverable, however large the number. pLS-SIDE changes are the specific
case - pixel-seed cleaning, pLS dedup, pLS admission all sit upstream of everything the chain
redesign replaced, so a fix there moves both sides of the comparison and closes none of the gap.
Before you spend time, ask whether the population you are acting on is one WE create (chain TCs,
the -ZP8 post-deletion additions, attach conversions, retirement of seeds against OUR chains) or
one LST hands us unchanged. If the latter, stop and say so.

JUDGING ORDER: efficiency (overall AND displaced) dominates; duplicate rate second; fake rate a
close third. A change that costs efficiency to buy dup is only interesting if the eff cost is
inside .0005 overall and the displaced bands do not move.

ENTRY BAR for this file - significant and CONFIRMED only:
  * a measured improvement, with the numbers AND the exact command line / config
  * a RECON FACT about mechanism that changes what someone else would try
  * a falsified hypothesis (so nobody repeats it)
  * a trap that would cost another agent time
NOT for: progress notes, plans, "starting X", speculation, small tuning scans.

Append with flock (five concurrent writers):
  flock S/FINDINGS_R2.md -c 'echo "[R3 HH:MM] finding: numbers, config" >> S/FINDINGS_R2.md'

--------------------------------------------------------------------------------------------
## THE BASELINE AND THE TARGET

The baseline is the WINDOW-FIX config, integrated and committed at a2f81bb5760. It is the
current production default in interface/ChainConfig.h - no overrides needed to reproduce it.
The window fix was: the bare-chain crossclean arm's dR^2 < 0.02 window (inherited from LST's
T5 arm) is GONE, bars re-fit to xcTheta 3.665 / 3.15 / 3.75. If you are A/B-ing in a prototype,
your baseline must include it or your wins are partly its.

Confirmed 977 evt (d3_ref/r_E1_977 vs r_BASE977), and 1000 evt vs LST master b42d8f97ad5:

  dup barrel      .0231   vs LST .0099   <- THE TARGET, still 2.3x
  dup transition  .0199   vs LST .0126
  dup endcap      .0709   vs LST .0856   (we are BETTER - the maintainer will accept it getting
                                          worse only if it stays WELL under LST)
  dup overall     .0485   vs LST .0518   (we are better overall)
  fake barrel     .0551   vs LST .0437   <- THE OTHER HALF OF THE GAP, untouched last round
  fake transition .0592   vs LST .0454
  fake overall    .0492   vs LST .0448
  eff overall     .8091   vs LST .8096
  mean nhitOT     6.435
  displaced       vxy[5,10) .7216  vxy[10,30) .7125  dxy[1,5) .5822
                  ^^^ +.076 / +.085 / +.072 OVER LST. THE ADVANTAGE. Protect it: this round
                  spends AT MOST single distinct sims, and the maintainer already unwound two
                  changes from last round for costing displaced efficiency.

EFFICIENCY CALIBRATION (frozen 300): effD = 22633 sims, so ONE wrongly deleted sole-cover row
= .0000442 of overall efficiency. A .0005 budget is 11 rows over 300 events = .038 rows/evt.

--------------------------------------------------------------------------------------------
## WHAT IS ALREADY KNOWN (inherited - do not re-measure these)

Full prior logs: FINDINGS_BARREL.md (round 1, D1-D4), FINDINGS_F.md (the round before).
The load-bearing facts:

1. THE CELL IS 100% ADDRESSABLE AT ZERO EFFICIENCY COST (D1 oracle, a07 removal simulator,
   frozen 300). Deleting every set-safe DUPLICATE bare seed plus every FAKE bare seed in
   |eta| < 1.7 gives: eff .81010 -> .81010 EXACTLY (0 sims lost), dupB .0285 -> .0053,
   dupT .0232 -> .0100, fakB .0545 -> .0533 (BETTER), fakT .0595 -> .0543 (BETTER), displaced
   bands bit-identical, mean nhitOT barrel 9.920 -> 10.057. 13.2 rows/evt. So NOTHING
   STRUCTURAL is left in this cell - the entire remaining barrel/transition dup gap is
   SELECTOR QUALITY. A GBDT on the dumped features reaches AUC .957 against that oracle label.

2. WHERE THE DUPLICATES LIVE: 84% of barrel duplicate bare-seed rows are -ZP8 POST-DELETION
   additions, not LST-carried type-8 rows. The barrel dup excess IS the pLS/T5 embedding-net
   CrossCleanpLS we deleted; -ZP8 6 adds back exactly the seeds LST's crossclean killed. Any
   study that joins only LST-carried rows (tc_plsIdx) sees 16% of the cell - join on
   tc_dbgPls (protoD1's PROTO_DUMP_PLSROW=1) instead.

3. THE POPULATION CENSUS (D2, per event, harness's own labels): BARREL 47.2 surviving bare
   seeds = 7.5 harness-dup + 1.0 sole-cover + 1.5 truth-fake + ~37 "clean" ballast (covers a
   sim outside the efficiency denominator). TRANS 24.8 = 4.1 + 1.0 + 2.0. Deleting ballast
   RAISES dupB and fakB (denominator shrinks, numerator doesn't) - so "delete more" is not the
   move; separating 7.5 from 1.0 is. SOLE covers of a DISPLACED sim in barrel: 0.00/evt
   (transition 0.08) - barrel/transition seed retirement CANNOT spend the displaced lead.

4. THE TRUNCATED-RESIDUAL DEFECT (D4, and it generalizes): any selector that thresholds a score
   whose OWN acceptance cut created the population is working on a truncated residual. The
   -XCT frontier provably vanishes at bar = attach margin: no takeable seed can have a pair
   logit above the acceptance bar BY CONSTRUCTION. This is the mechanical reason three rounds
   of band thresholds on that logit failed. STILL UNAPPLIED to -RPSA and -T3F, which have the
   same structure. Single-feature AUC for sole-vs-free among barrel survivors: log10(pLS pt)
   .745, while the two incumbent scores are cPairLogit .598 and plsBestChainLogit .607 - the
   incumbent retirement score is nearly uninformative about what retirement costs; seed pt
   alone beats it.

5. FALSIFIED, do not redo: ownership variants of the bare-chain arm (one-chain-one-seed,
   mutual-best, greedy 1-1) are worth ZERO - the arm is already effectively 1-1. Owner-branch
   credibility (-XCQ) costs 4x what its offline model predicted. -M4B is dominated by -MRB.
   -a4 (4-layer chain absorb) is a dud. A second trained retirement head buys only ~.002 dupB
   over the free window fix (three independent agents, three variants, same answer).

6. THE ATTACH HEAD'S FEATURE SET IS THE OBVIOUS HOLE: it has NO |eta| input at all, no pT, and
   none of the chi-squared/radius-pull features LST spent years engineering for pT3/pT5
   matching (rPhiChiSquared, rzChiSquared, rPhiChiSquaredInwards, pixelRadius vs
   tripletRadius/quintupletRadius with pixelRadiusError, |pixelEta|, pixelPt). The maintainer
   flagged this as the flagship lead. Verified present in LST master.

--------------------------------------------------------------------------------------------
## TRAPS (each of these cost an agent hours last round)

T1 BUILD ENV: build your prototype copy in the SAME env protoFINAL2 was built in. Its binary
   has RPATH = CMSSW_17_0_0_pre2 ROOT 6.36; `source setup.sh` alone puts CMSSW_14_2_0_pre4
   ROOT 6.30 on PATH, and the mixed binary dies at startup inside TFile::Open, NOT at compile.
   Always: pushd standalone && source setup.sh && cmsenv && source setup.sh.

T2 BASELINE: syn_run.sh defaults are NOT the production config, and now production also
   includes the window fix. Reproduce your baseline's headline numbers before trusting a delta.

T3 SET-DELETION LABELS: "another surviving TC covers my sim" is a SINGLE-ROW counterfactual and
   is invalid for a rule that deletes a SET - two bare seeds of one sim are each "a duplicate"
   and deleting both loses the sim. Require the reference cover to be a NON-bare-seed TC.
   Getting this wrong silently costs 3x the efficiency you budgeted.

T4 OFFLINE PRICE MODELS UNDERSTATE COST by 2-4x when the label is a proxy for the harness's
   matcher. Price with the harness's own definitions, or verify with a real A/B before
   recommending.

T5 PAIR-LOG MISALIGNMENT: if you instrument a dump inside a kernel that also decides, the dumped
   row order and the decision order can diverge; three agents hit a variant of this. Dump keys,
   not positions, and re-join.

T6 lst_make_tracklooper prints "compilation successful" even when a TU failed. Check the
   FRESHLY BACKED-UP .make.log.<timestamp>, not the stale .make.log.

T7 The hist tool dies if stdout is /dev/null - give it a real file.

--------------------------------------------------------------------------------------------
## INHERITED TOOLING (reuse, do not rebuild)

* d1_ref/d1_study.py - OFFLINE DELETION PRICER. Prices any deletion set (eff/dup/fake/displaced
  per band) in seconds instead of a 13-minute A/B, validated against real runs to the digit.
  This is the single highest-leverage instrument in the round.
* protoD1 PROTO_DUMP_PLSROW=1 -> tc_dbgPls: the branch that makes the bare-seed join complete.
* protoD2 -D2D <path> -D2N 1: 44 features + fate + sim rows per seed; d2_ref/d2_label.py adds
  harness labels; d2_ref/d2_recon.py prints census, per-feature AUCs, incumbent precision curve.
* protoD4's inert pair dump with BOTH truth labels.
* iterations/a08_ref/a08_distinct.py - DISTINCT displaced sims (band tables double-count a sim
  across the vxy and dxy projections; never sum the two sides).
* d3_ref/d3_run.sh - the A/B harness: builds, runs, hists, prints the per-band delta table.

--------------------------------------------------------------------------------------------
## WHERE TO WORK (read this before you copy anything)

Explore in your OWN PROTOTYPE COPY, exactly as every prior round did, and recommend integrated-
tree changes at the end rather than editing the shared tree - five agents editing src/alpaka at
once cannot be composed or measured.

  cp -a protoD3 protoR<n>      <- copy protoD3, NOT protoFINAL2

protoD3 is the ONLY prototype carrying the window-free retirement channel (-D3W/-D3W2/-D3W3), so
it is the only one that can express this round's baseline. THE BASELINE OVERRIDE LINE, offline:

  -T3F 0.10 -XC4 1 -RPSA 5.5 -a 6.0 -a2 6.0 -a3 6.0 -EX 0 \
  -D3W 3.665 -D3W2 3.15 -D3W3 3.75 -XCT 1e9 -XCT2 1e9 -XCT3 1e9

(The three -XCT 1e9 disable the OLD windowed arm; the three -D3W are the window-free
replacement. In the INTEGRATED tree none of this is needed - the defaults in ChainConfig.h ARE
the baseline.) Reference runs already exist: d3_ref/r_E1_977* (977 evt) and win_ref/* (1000 evt,
integrated). Reproduce the headline numbers in YOUR copy before trusting any delta - and remember
trap T1: build in the same env, `pushd standalone && source setup.sh && cmsenv && source setup.sh`.

--------------------------------------------------------------------------------------------
--- entries below ---

[R2 20:25] MEASURED DELETION PRICES on the window-fix baseline (300 frozen evt, protoR2 = byte
copy of protoD3, r2_ref/r2_run.sh; BASE with no overrides reproduces d3_ref/r_E1 with delta
0.00000 on ALL 29 metrics, so the deltas below are pure). Each row = ONE mechanism switched OFF.

  tag       override    d eff    d dupB   d dupT   d dupE   d dupO   d fakO   v510/v1030/d15
  NOXCR2    -XCR2 0    +.00000  +.00000  -.00000  +.00000  -.00000  -.00000  ALL EXACTLY 0
  RPSA60    -RPSA 6.0  +.00004  -.00000  +.00015  +.00018  +.00013  +.00001  ALL EXACTLY 0
  RPSA70    -RPSA 7.0  +.00013  +.00007  +.00037  +.00062  +.00042  +.00002  ALL EXACTLY 0
  NORPST    -RPST 1e7  +.00141  +.00039  +.00145  +.00301  +.00201  +.00004  ALL EXACTLY 0
  NORPS     -RPS 0     +.00186  +.00055  +.00226  +.01492  +.00875  +.00003  ALL EXACTLY 0
  NOXC4     -XC4 0     +.00022  +.00080  +.00526  +.00881  +.00592  -.00008  ALL EXACTLY 0
  NOCC      -CC 0      +.00349  +.10854  +.20548  +.15312  +.14898  +.00404  ALL EXACTLY 0
(v15 moves +.00073..+.00292 GAIN on the rows that gain eff; every other displaced band is 0.)

FREE DELETION, CONFIRMED: the -XC PIXEL-ANCHORED dR ARM (test 2, dR^2 < xcDR2Pix = 1e-6 against
any anchor seed) IS WORTH EXACTLY NOTHING. It fires 5.7 seeds/evt out of the arm's 2987.7, and
with it off every rate metric is unchanged to 5 decimals (nTC +2 rows over 300 events, and the
window-free channel picks up 0.16 seeds/evt of the slack). Deletable: cfg.xcDR2Pix, the whole
test-2 loop in ChainXcPixelArm (a linear scan over ~700 anchors per candidate row, ~600k
comparisons/evt), and the anchorEta/anchorPhi buffers + their writes in ChainXcAnchorList.
LOAD-BEARING, with the number (do not delete these):
  -CC (stage-B hit-overlap contention) is the single most load-bearing cleaning mechanism in the
  pipeline: off, pT3-class deliveries go 120.7 -> 317.5/evt (LST ~150) and dupO +.149.
  -XC4 is load-bearing THROUGH the window-free channel, not through -CCS: with -XC4 0 the
  window-free retirement count falls 434.9 -> 179.5 seeds/evt, i.e. 59% of that channel's
  retirements are explained by a 4-LAYER (T4-class) chain target. dupT +.0053 dupE +.0088.

[R2 21:05] THE TRUNCATED-RESIDUAL QUESTION, ANSWERED FOR -RPSA AND -T3F (300 frozen evt,
window-fix baseline, protoR2). Neither has the -XCT defect, and the reason is worth knowing.

-RPSA: NOT truncated, but REDUNDANT, and the redundancy is with the WINDOW FIX.
  * -XCT truncates because its population is "pairs toward a DELIVERED SEEDLESS chain", and
    seedless MEANS no bidder cleared the acceptance bar -- the logit is truncated ABOVE at the
    margin, hence the vanishing frontier. -RPSA's score (plsBestChain) is written for EVERY
    scored stage-A pair BEFORE the banded threshold (invariant I4) and its population is "the
    seed is not an OWNER", which above the bar means the CONTENTION denied it, not the score.
    Measured: the frontier is alive well above the 6.0 delivery bar and SATURATES at bar 12
    (RPSA120 and RPSA200 agree to 5 decimals -- 12 is the max logit in the non-owner population).
  * BUT THE WHOLE FRONTIER IS WORTH ALMOST NOTHING. Bar 5.5 -> infinity, the entire chain-side
    term buys dupO .00119 / dupB .00015 / dupT .00052 / dupE .00192 at eff +.00013 (a GAIN),
    fakO +.00003, v510/v1030/d15/d510 EXACTLY 0 (v15 +.00073 = one sim GAINED).
    Per-bar dupO: 6.0 +.00013 | 7.0 +.00042 | 8.0 +.00070 | 10.0 +.00108 | 12.0 +.00118 | inf +.00119.
  * MECHANISM: the window-free crossclean arm retires the same seeds at a bar 1.8-2.4 units LOWER
    (xcTheta 3.665/3.15/3.75 vs 5.5). -RPSA was tuned in A11 when the chain arm still carried
    LST's dR^2 < 0.02 centroid window and therefore could not reach them. The window fix subsumed
    it: -RPSA is a residual of THE ARM, not of its own acceptance cut.
  => CORRECT PREDICATE: there isn't one. DELETE the chain-side term. In the integrated tree
     plsBestChain has EXACTLY ONE consumer (ChainArbitrate.h:485), so the deletion also removes
     one atomicMax PER SCORED PAIR (1.25e6 pairs/evt) from ChainAttachScore, the nPls
     plsBestChain device buffer and its per-event memset, the parameter through three kernel
     signatures, and cfg.rpsThetaChain. Price dupO +.0012 (dupO .0485 -> .0497, still under LST
     .0518; dupE .0709 -> .0728, still far under LST .0856), eff +.00013.
     MINIMAL VARIANT if that dup is not payable: rpsThetaChain := cfg.attachTheta (all three
     attach bands are 6.0 today) -- deletes the free-standing constant for dupO +.00013 at
     eff +.00004, displaced all EXACTLY 0.

-T3F: STRUCTURALLY NOT A RESIDUAL, and LOAD-BEARING. It thresholds triplets.fakeScore(), which
  is NOT in the 19-feature attach vector (AttachNetworkWeights.h lists af00..af18; a bare-T3
  target is handed chainGateLogit = 0 and nLayers = 3, so the head is blind to T3 quality). Its
  information is orthogonal to the acceptance cut and its frontier does not vanish. Measured with
  -T3F 1e9 (OFF): fakB +.01164, fakT +.00855, fakE +.00287, fakO +.00643, eff -.00049,
  dupO -.00036, and the attach stage goes 488 -> 2372 ms/evt (4.9x). KEEP.
  FOR R3: -T3F is by far the largest single FAKE-RATE lever in the cleaning path -- it is worth
  21% of the barrel fake rate at its current 0.10, and TIGHTENING it below 0.10 has never been
  scanned. That is a fake-rate lead that costs no new machinery.

ALSO MEASURED (300 evt, same baseline), the two -XC arms separately:
  -XC 2 (pixel-anchored hit-sharing arm DELETED): eff +.00031 dupB +.00010 dupT +.00030
    dupE +.00521 dupO +.00295 fakB +.00085 fakT +.00133 fakE +.00192. So LST's verbatim
    pixel arm does essentially NOTHING for barrel dup; it is an endcap-dup and fake mechanism.
  -XC 1 (whole bare-chain arm, i.e. the window-free channel, DELETED): dupB +.04642
    dupT +.05389 dupE +.01765 dupO +.03195, eff +.00270, fake BETTER by .00066. The bare-chain
    arm IS the barrel/transition duplicate mechanism; nothing else in the pipeline substitutes.

[R3 20:35] FAKE CENSUS, per object class, 1000 evt ours (win_ref/win_rv1000.root) vs LST master
(master_ref/master_rv1000.root). Barrel, fakes per event / class fake rate:
  class    ours nTC/evt  ours fk/evt  clsFR  |  master nTC/evt  fk/evt  clsFR
  pT5cls      287.6         1.79      .0062  |    321.1     2.90   .0090
  T5cls       101.7        15.38      .1512  |     49.1     4.69   .0956
  pT3cls       46.7         1.07      .0228  |     60.2     1.24   .0206
  pLSbare      28.0         0.96      .0344  |     24.4     0.89   .0365
  T4cls        10.4         6.97      .6718  |     13.1    10.73   .8173
  TOTAL       474.5        26.17      .0552  |    467.9    20.46   .0437
THE ENTIRE BARREL FAKE EXCESS (+5.71 fakes/evt) IS THE BARE (pixel-less) CHAIN CLASS,
tc_type 4: +10.69 fakes/evt over master, partly offset by our BETTER T4/pT5/pT3. Split of the
T5cls excess: ~half count (we emit 2.07x as many bare chains as master emits T5s) and ~half
quality (.1512 vs .0956). Transition is the same story (T5cls +7.58, T4cls -3.37, net +3.72).
BARE PIXEL SEEDS ARE ONLY 3.7% OF THE BARREL FAKE NUMERATOR (0.96 of 26.17) and 10.6% of the
transition one - so the bare-seed selector work (R1/R2/R4) cannot reach the fake gap; the dup
gap and the fake gap have DIFFERENT sources. Our fake bare chains are combinatorial garbage,
not near-misses: mean tc_pMatched .303, 55% of them below .25.
Tooling: r3_ref/r3_census.py (per-class census from any harness ntuple).

[R1 20:25] RECON, the attach head's ACTUAL operating point (from the shipping r2 head's own
training record, fanout5/attachretrain/train_r2.log, frozen TEST-60 chain universe = 5976
true / 85356 fake pairs): at cut 6.0 -- which IS production -a/-a2/-a3 -- the head has
precision 0.6956 and recall 0.3062 (tp 1830 / fp 801). Chain-pair AUC 0.99410. So the
attach arm currently takes 31% of the true (chain, pLS) pairs and 3 of every 10 attaches is
wrong. Frontier for anyone tuning -a: cut 4.0 prec .500/rec .535; 5.0 .593/.402; 7.0
.829/.212; 8.0 .885/.135; 9.0 .943/.058. Any "-a scan" is a walk along THIS curve, and the
curve is the head's, not the rule's.

[R1 20:25] RECON + TRAP, TRAINING-DISTRIBUTION GAP: every attach-head dump ever produced
(incl. the shipping r2's) enumerated CHAIN targets with nLayers >= 5 only (PixelAttach.cc
minChainLayers default 5), but the DEPLOYED pipeline scores 4-layer chain targets too --
main.cc's -XC4 and -CCS enumerations set minChainLayers = 1. The head has therefore never
seen a 4-layer chain target it is asked to score. Measured on a fresh 20-event dump with
the floor at 1 (protoR1, new -PDML flag): 4-layer chains are 57494 of 348086 chain-target
rows (16.5%) and are 3.89% TRUE against 1.09% for the chain universe overall -- 3.6x
richer, i.e. exactly the population the -XC arm is retiring seeds against. Also: 96% of the
head's true CHAIN pairs are pileup-sim-only (no accepted-sim row), so the chain-side head is
overwhelmingly trained on sims outside the efficiency denominator.

[R4 20:25] RECON, four confirmed facts about the seed-admission universe. (1) THE ADMITTED
UNIVERSE IS PAIRWISE HIT-DISJOINT: over 977 evt, nSh2 (# other universe seeds sharing >= 2
pixel hit rows) is EXACTLY 0 for every seed in every band, and nSh1>0 is only 1.44/evt in
barrel out of 347 universe seeds. LST's two CheckHitspLS passes guarantee it (pass 2 removes
any surviving quad pair sharing >= 1 hit within |dEta| <= 0.1). => THE -RD / -RDT ">= 2
SHARED PIXEL HITS" FAMILY DEFINITION IS VACUOUS ON THE BARE-SEED UNIVERSE - no family
redefinition can merge a single bare duplicate row. Do not spend time there.
(2) THE CROSS-FAMILY RULE IS ALREADY IN THE PIPELINE: D1's nOwnPixShare (# of an emitted bare
seed's pixel hit rows that also belong to an attach-owner seed) is 0 for EVERY emitted bare
row in EVERY band, because the -XC pix channel already retires any quad sharing >= 1 pixel
hit with an anchor (3026 seeds/evt, r_E1_977.log line 1003). With -RT5=1/-RT3=1 dropping all
carried pixel rows, plsOwned IS the complete set of delivered pixel-class seeds, so that
anchor set has no hole. Nothing to win there either.
(3) BARREL: bare-ONLY duplicate clusters = 0.000/evt (trans 0.002, and 5.148/evt barrel dup
rows all have a NON-bare reference cover). So NO self/mutual dedup among bare seeds can touch
the barrel dup excess - it is 100% "bare row duplicates a delivered chain TC", i.e. strictly a
RETIREMENT-PREDICATE problem. R2/R1: the 4.65 -ZP8 barrel dups must be killed in the
retirement channel; admission provably cannot reach them.
(4) ENDCAP, the opposite: 45.90 of 56.12 endcap duplicate rows/evt ARE bare-only clusters
(dupE .0649 of which .0531 is bare-only). ORACLE ceiling of "keep one row per bare-only
cluster" (A07 pricer on d1_ref/d1_D977.pkl, 977 evt, old pre-window-fix baseline):
dup overall .05033 -> .02191, dupE .07100 -> .01841, eff .80940 -> .80940 EXACTLY, effB/T/E
bit-identical, all four displaced bands bit-identical (v15 .80022 v510 .72111 v1030 .71254
d15 .58187), fakE .04308 -> .04425 (denominator only), mean nhitOT E 3.566 -> 3.664,
-22.4 TC/evt. Set-safe by construction. Implementable candidate being priced now: LST's own
pass-2 predicate with the |dEta| <= 0.1 gate dropped and the dR^2 < 1e-5 term opened to a real
window, score as arbiter -- one constant, no new machinery, no trained component.

[R5 21:05] SPLIT-vs-DUPLICATE CENSUS, ours vs LST on the SAME 300 events (r5_ref/cen_ours300.txt,
cen_lst300.txt; tool r5_ref/r5_census.py; ours = r5_run.sh BASE300 = the window-fix baseline
reproduced: eff .8096 dupB .0235 dupT .0205 dupE .0712 fakB .0545 nhOT 6.434).
CONSTRUCTION IS NOT SPLITTING TRACKS. Barrel duplicate GROUPS (a sim matched >0.75 by >=2 TCs),
per event, by the type multiset of the group:
                 ours    LST
  T5c + pLS      4.790   1.450   <- +3.34, and x2 rows = 6.68 of the +7.45 barrel dup-ROW excess
  T5c + pT5c     0.890   0.003   <- chain-chain (both members are chains in our config)
  T5c + T5c      0.327   0.497   <- we are BETTER
  T5c + pT3      0.223   0.030
  everything else <0.09 each
So 90% OF THE BARREL DUPLICATE-RATE EXCESS vs LST IS THE (bare chain TC, carried bare pLS) PAIR -
the seed-admission/retirement cell, NOT construction. Chain-chain barrel dup groups: ours 1.557/evt
vs LST's OT-OT 1.076/evt, i.e. the whole construction-side excess is +0.48 groups/evt = 13% of the
gap. HIT-OVERLAP CLASS of the OT-carrying pairs (disjoint = a split, coincident = built twice):
  ours barrel  DISJOINT 9.3% (0.143/evt)  PARTIAL 90.7%  COINCIDENT 0.0%
  LST  barrel  DISJOINT 6.9% (0.077/evt)  PARTIAL 24.0%  COINCIDENT 69.2% (0.770/evt)
Splits are 0.14/evt in the barrel and are mostly chain+pT3-delivery, not chain+chain. Our
chain-chain duplicates are ALL "PARTIAL": two 5- or 6-layer chains sharing EXACTLY ONE MD (2 hits),
i.e. sitting exactly on K9's claim budget (maxClaimedMDs 1 -> 2 hits, or frac <= 0.20). LST instead
has a COINCIDENT population we structurally cannot have (K9's braid forbids it).
=> The "we build two chains where LST builds one T5" hypothesis is FALSIFIED for the barrel. The
remaining barrel/transition dup gap is selection, as three rounds assumed - now measured.

[R5 21:07] THE BARREL FAKE EXCESS IS 100% THE BARE-CHAIN CLASS, AND IT IS MOSTLY A POPULATION
EFFECT (r5_ref/len_ours300.txt vs len_lst300.txt, same 300 events, tool r5_ref/r5_len.py).
Per-class barrel rows / fake rate, 300 evt:
             ours n     ours fakR    LST n      LST fakR     fake rows ours-LST
  T5c(bare)   39896      0.1369      17409      0.0901        5462-1569 = +3893
  T4c(bare)    3651      0.6365       4884      0.8075        2323-3944 = -1621
  pT5(chain+pLS) 94397    0.0064     109145     0.0095         604-1037 =  -433
  pT3          19864      0.0197      25545     0.0211         391- 539 =  -148
  pLS(type 8)  13846      0.0274      12545     0.0286         379- 359 =   +20
  TOTAL                                                                 = +1711 (= .0533 vs .0439)
EVERY class except the bare chain is ALREADY BETTER THAN LST. We emit 2.29x as many SEEDLESS
quintuplet-class rows as LST does (29.7% of our barrel chain TCs fail to attach a pLS vs LST's
13.8% seedless share) and each is 1.52x more likely to be fake. Give our bare chains LST's per-row
fake rate and barrel fake is .0425, i.e. AT/BELOW LST, with no other change.
=> The barrel fake gap is a CONVERSION problem, not a construction problem: it is bought by making
the attach head convert chains into pT5-class rows (R1), or by retiring bare chains (R2/R4), not by
building different chains. R3: your barrel fake cell is entirely tc_type 4 / tc_isChain 1.

[R5 21:09] TRAP - "mean nhitOT" IS DILUTED BY ZERO-LENGTH ROWS. efficiency/src/performance.cc
fillOTLengthSet puts EVERY TC with pt > 0.9 in the denominator and only nhitOT>0 rows in the
numerator, so the metric is (mean object length) x (1 - fraction of nhitOT==0 rows). Verified: my
replica reproduces the harness numbers exactly (barrel 10.148 LST / 9.869 ours, ALL 6.519 / 6.434).
Restricted to OT-CARRYING rows the barrel means are ours 10.352 vs LST 10.509, so only -0.157 of
the headline -0.279 is object length; the rest is our larger type-8 share (8.1% vs 7.4%) plus the
bare/attached mix. The -0.157 that IS real is entirely "fewer 6-layer objects": 12-hit share of the
attached quintuplet class ours 57.5% vs LST 78.4%, and LST reaches 6 layers via ExtendT5FromDupT5 -
the mechanism our (deliberately deleted) extension stage played. So the nhitOT deficit is the known
price of that deletion, not a weld defect. Do not quote raw mean nhitOT as an object-length claim.

[R2 21:35] FALSIFIED, AND IT CLOSES A DOOR: THERE IS NO MISSING RETIREMENT ARM. I implemented
the obvious generalization of the crossclean chain arm -- retire a bare quad seed whose max
attach-head logit over DELIVERED **ATTACHED** (type-7) chain TCs reaches a band bar, i.e. the
substitute for the OTHER half of LST's deleted pT5 CrossCleanpLS arm (the pLS/T5 embedding
proximity test; the surviving half is our >= 1 shared pixel hit row test). R4's fact (2)
predicts this is exactly the hole: every emitted bare row has nOwnPixShare == 0, so a bare row
that duplicates an ATTACHED delivery cannot be reached by the pixel arm.
protoR2 `-R2A/-R2A2/-R2A3` (same -CCS sentinel dialect, same head, same pair log, NO new
weights), measured on 300 frozen evt with the redundant -RPSA term deleted (-RPSA 1e7):
  bars 6.0/6.0/off    : 1.47 seeds/evt retired | dupB -.00000 dupT +.00015 dupO +.00108 eff +.00004
  bars 4.5/4.5/off    : 4.2/evt                | dupB -.00007 dupT -.00024 dupO +.00100 eff -.00022
  bars 3.665/3.15/off : 4.24/evt               | dupB -.00019 dupT -.00039 dupO +.00095 eff -.00062
                                                 (effB -.00137 -- it is EFFICIENCY, not duplicates)
i.e. against the -RPSA-deleted reference (dupO +.00119) the new arm recovers at most .00024 of
dup and pays barrel efficiency for it. MECHANISM: a bare seed that scores >= 3.665 against an
attached chain but shares NO pixel hit with that chain's winning seed is usually a DIFFERENT
track, not a duplicate -- the winner already claimed the chain's hits, and the loser's own OT
continuation was simply never built. So the pixel-hit-sharing arm is not missing half of LST's
pT5 arm; it is the whole of it that matters.
CONSEQUENCE for R1/R4: combined with R4's fact (3) ("100% of the barrel dup excess is a bare row
duplicating a delivered chain TC"), the reference cover must be a SEEDLESS chain TC, which the
window-free arm ALREADY scores at 3.665. The remaining barrel/transition duplicate gap is
therefore purely SELECTOR QUALITY on the seedless-target logit (D3's AUC .900 shipped vs the
.957 GBDT ceiling) -- there is no structural arm left to add. That makes R1's head work the only
route, and R1's 4-layer training gap is directly on it: my -XC4 0 run shows 59% of the
window-free channel's retirements are decided on 4-LAYER chain targets, which R1 has just shown
the head was NEVER TRAINED ON. Fixing that training gap is a direct upgrade to the retirement
channel, not only to delivery.
GATE for the -R2A code: protoR2 BASE2 (the new binary, all -R2A bars unset) is delta 0.00000 on
all 29 metrics AND 33/33 branches BIT-IDENTICAL to r_BASE.root (rebase_ref/cmp_branches.py).

[R2 22:20] 977-EVENT CONFIRMATION of the two recommended deletions, vs d3_ref/r_E1_977 (the
round's baseline: eff .80909 dupB .02313 dupT .01993 dupE .07091 dupO .04851 fakB .05507).
Command: `LSTN=.../LSTNtuple_instr_977evt.root BASEHISTS=d3_ref/r_E1_977_hists.root
r2_ref/r2_run.sh <TAG> <override>`.

(1) `-XCR2 0` -- THE PIXEL dR ARM IS FREE, CONFIRMED AT 977. Every single metric is 0 to five
    decimals: eff +.00001, dupB -.00000 dupT -.00000 dupE +.00000 dupO +.00000, fakB/T/E/O all
    -.00000, ALL displaced bands exactly .00000, nTC +11 of 2,004,330. DELETE IT.

(2) `-XCR2 0 -RPSA 1e7` -- CHAIN-SIDE -RPSA DELETED:
      eff .80909 -> .80932 (+.00023)  effB +.00007  effT +.00038  effE +.00034
      dupB .02313 -> .02332 (+.00018) dupT .01993 -> .02066 (+.00072)
      dupE .07091 -> .07293 (+.00202, LST .0856)  dupO .04851 -> .04979 (+.00128, LST .0518)
      fakB -.00001  fakT -.00001  fakE +.00006  fakO +.00002
      DISPLACED: vxy[1,5) +.00044 (GAIN), vxy[5,10) / vxy[10,30) / dxy[1,5) / dxy[5,10)
                 EXACTLY .00000.
    Efficiency UP in every band, displaced never worse, and it removes an unconditional
    atomicMax PER SCORED PAIR (1.25e6 pairs/evt, ChainAttach.h:816) plus an nPls device buffer.

(3) `-RPST 1e7` -- T3-SIDE TERM DELETED (report as a maintainer's-call trade, NOT stacked with 2):
      eff .80909 -> .81062 (+.00153)  effB +.00049  effT +.00213  effE +.00243
      dupB +.00048  dupT +.00152  dupE +.00307 (.0740)  dupO +.00208 (.0506, LST .0518)
      fakB -.00001  fakT -.00005  fakE +.00008
      DISPLACED: vxy[1,5) +.00175 AND vxy[5,10) .72161 -> .72261 (+.00101) -- BOTH GAINS; the
                 only change measured this round that IMPROVES a displaced band. Others exactly 0.
    MECHANISM (why the T3 side is the weak predicate): it retires a 4-hit pixel TC on the word of
    a 3-LAYER triplet delivery whose owner is a different seed. Its exchange rate is 0.70 eff per
    unit of dup against 0.31 for the chain side -- 2.3x worse. DO NOT STACK (2) AND (3): together
    dupO lands at ~.0519, i.e. AT LST, which breaks the "stay under LST" constraint.

ALSO: `-CCN 2` (the value ChainAttachT3.h documents as the default, LST's "2 of 3 MDs") is much
WORSE than the shipped -CCN 1: eff +.00106 but dupB +.00870 dupT +.01935 dupE +.02129
dupO +.01729 (-> .0658, far above LST) and fakO +.00223. => -CCN 1 is validated; it can be
HARDCODED (and the count test collapses to "is any of my 3 MDs already claimed"), deleting
cfg.ccMinShared.

FOR R3, measured hand-off on the fake lever: -T3F 0.05 gives fakB -.00071 fakT -.00149
fakE -.00077 fakO -.00087 at eff -.00022 (effB -.00102), displaced all 0. -T3F 0.03 gives
fakO -.00139 at eff -.00044 AND starts spending displaced (vxy[1,5) -.00292). So the lever runs
at ~4 units of fake per unit of efficiency and 0.10 is not obviously mis-set; below 0.05 it
touches the displaced lead.

[R4 21:05] MEASURED WIN + A RECON FACT EVERY dR WINDOW IN THE PIPELINE SHOULD HEAR.
RECON FACT: A CIRCULAR dR WINDOW IS THE WRONG SHAPE FOR A "SAME TRACK" TEST. Over 977 evt I
built every pair of emitted bare-seed rows in the endcap inside |dEta|<0.02, |dPhi|<0.5 and
labelled it same-sim / different-sim (666,140 pairs, 31,417 same). Medians: |dEta| 0.00093
(same) vs 0.0128 (diff) -- but |dPhi| 0.022 (same) vs 0.013 (diff), i.e. same-track pairs are
FARTHER apart in phi than random neighbours. Single-feature AUC: |dEta| .054, |dTanLambda|
.058, |dEta|/etaErr .074, |1/pt1-1/pt2| .136, |ln ptRatio| .218, minPt .853, and |dPhi| .754
THE WRONG WAY. Reason: the polar angle is the helix INVARIANT, the azimuth is not, so two
seeds fitted to disjoint hit subsets of one track report the direction at different points
along the helix. CONSEQUENCE: a circular dR^2 window merges at 12-20% purity, while
(|dEta| < 0.002 AND |dPhi| < 0.05 AND same charge AND |ln ptRatio| < 0.5 AND both pt >= 1)
merges at 91% purity AT THE SAME REACH. This is why LST's pass-2 (|dEta| <= 0.1 gate, then
dR^2 < 1e-5) misses these entirely: its eta gate is 50x too loose and its dR arm 300x too
tight. R2/R3: if any predicate you own uses a circular dR window as a same-track test,
factorize it into a tight eta term and a loose phi term - the maintainer's win from DELETING
-XCW2 0.02 is the same effect.
MEASURED (A07 pricer, 977 evt, d1_ref/d1_D977.pkl, old pre-window-fix baseline; rule =
ENDCAP-ONLY greedy keep-best-pLS-score with the relation above; barrel/transition identical
because the rule fires 0.009 times/evt there):
  |dEta|<.002 |dPhi|<.05 lnPt<.5 pt>=1.0  11.51 kills/evt: dup .05033->.03748 dupE
    .07100->.04738 | eff -.00027 (20 sims of 73782) effB/effT unchanged | fakE +.00033 |
    v15 -.00066 (3 sims) v510/v1030/d15 BIT-IDENTICAL | nhitOT_E 3.566->3.614 | -11.2 TC/evt
  |dEta|<.002 |dPhi|<.10 lnPt<.3 pt>=1.0  13.38 kills/evt: dup ->.03546 dupE ->.04366 |
    eff -.00031 (23 sims) | v15 -.00088 (4 sims) others bit-identical
  |dEta|<.002 |dPhi|<.05 lnPt<.5 pt>=1.5   6.82 kills/evt: dup ->.04238 dupE ->.05643 |
    eff -.00015 (11 sims) | v15 -.00044 (2 sims) others bit-identical
Implemented as -R4B/-R4B2/-R4B3 (+ -R4BP/-R4BQ/-R4BR/-R4BM) in protoR4; gate PASSED (33/33
identical to protoD3 at neutral settings). Real 977-evt A/B running now.

[R3 21:05] MECHANISM OF THE BARREL/TRANSITION FAKE GAP, and it is CHAIN CONSTRUCTION (for R5).
Prototype baseline r_E1_977, bare chains split by the -G 6 admission branch (tc_dbgBr):
  BARREL      br0 T4-IP     0.04 TC/evt  FR .000
              br1 T4-exempt 10.29 TC/evt FR .675   sole .302/evt, of which .291 DISPLACED
              br2 5+ IP     53.20 TC/evt FR .053   sole 6.49
              br3 5+ exempt 48.44 TC/evt FR .259   sole 2.87, of which 2.43 DISPLACED
  TRANSITION  br1 1.07 FR .428 | br2 27.25 FR .053 | br3 50.51 FR .170
The two EXEMPT (large-dca, displaced-oriented) arms hold 75% of all barrel fakes on 12% of the
barrel TCs, and the IP arm is 5x cleaner. Under the frozen config the exempt bars are effectively
"admit unless mX < -MR(-1.8)" for br3 (-MD 1e9 makes the mD test vacuous) and "admit unless
mD < -M4D(-1.2)" for br1 - so br3 is rescued by mX = max(prompt,displaced) - fake, i.e. a
LARGE-DCA chain can be admitted by the PROMPT head. Those rescued rows are the fake home:
in br3 barrel, rows with mD < -1.2 are 15.5 TC/evt at FR .47 while rows with mD >= 0 are
27.8 TC/evt at FR .084.
FALSIFIED, do not redo: DELETING them is a bad trade. Priced exactly (r3_ref/r3_price.py on
r3_ref/e1_977.pkl): br3 & mD < -1.8 -> fakB -.0086 fakT -.0097 but eff -.0078 (574 sims),
dxy1_5 -.0033, dispN -65; br3 & mD < -1.2 -> fakB -.0146 for eff -.0116. The exempt arm carries
~0.6 sole-cover sims/evt, and NOT mainly displaced ones (81% of them are vxy<1) - so this is
plain efficiency, not the displaced lead, and no band threshold on the exempt score can buy it.
Also falsified: wholesale class deletion (T5cls in |eta|<1.7 costs eff -.1218, dxy1_5 -.5062;
T4cls in |eta|<1.7 costs eff -.0015 and dxy1_5 -.0523).
WHAT THIS LEAVES: the gap is the CONSTRUCTION of the exempt bare chains, not their final
selection. Either lever alone closes the whole barrel gap: at master's bare-object count
(49.1/evt instead of 101.7) fakB = .0432; at master's class fake rate (.0956 instead of .1512)
fakB = .0432. Master's barrel T5s are also LONGER (mean nhitOT 11.24 vs our 10.87).

[R4 21:35] THE SEED-ADMISSION UNIVERSE IS EXHAUSTED EXCEPT FOR THE ENDCAP HELIX RULE - all
prices below are on the CURRENT (window-fix) baseline, my own 977-evt run r4_ref/r_DUMP977
which reproduces the FINDINGS headline to the digit (dupB .0231 dupT .0199 dupE .0709 dup
.0485 fakB .0551 fakT .0592 fake .0492 eff .8091 nhitOT 6.435 v510 .7216 v1030 .7125 d15
.5822), and the A07 pricer's BASELINE row matches that run digit for digit.
DEAD LEVERS, each measured, do not retry:
 * -RD/-RDT family redefinition: uShMax >= 2 is 0.000/evt over the whole universe in every
   band -- no two admitted seeds share 2 pixel hit rows. Vacuous.
 * cross-family hit rule (retire a bare seed sharing a pixel hit with a seed backing a
   DELIVERED pixel-class TC): dShMax >= 1 among emitted bare rows is 0.000/evt in every
   band. Exact price of "famhit >= 1": 0.000 kills. Already done by the -XC pix channel.
 * widening -XCR2 (the anchor dR window, now 1e-6): minDRD2 < 1e-4 reaches 1.51 rows/evt ->
   dupB -.00024 dupE -.00008 for eff -.00060. Bad trade at negligible reach.
 * seed-quality admission bars in the BARREL: score>=5 (5.2/evt) dupB -.0019 for eff -.0022;
   score>=0.5 (11.8/evt) dupB -.0043 for eff -.0048; score<=5e-4 (18.6/evt) dupB -.0112 for
   eff -.0087. ~.0011 eff per .001 dupB -- 100x worse than the endcap helix rule.
 * THE BALLAST TRAP, EXACTLY PRICED: deleting all 40.84/evt barrel cls1 rows gives eff
   .80940 -> .79057 AND dupB .02773 -> .02891 UP, fakB .05509 -> .05789 UP.
FOR R2/R1 (the barrel is yours): the barrel dup excess is NOT reachable from admission.
Barrel bare-ONLY duplicate clusters are 0.000/evt, i.e. EVERY barrel bare-seed duplicate row
has a delivered NON-bare cover, so the decision is seed-vs-TC (retirement), never seed-vs-
seed. The barrel ceiling, priced: deleting every barrel cls0 row (4.99/evt) gives dupB
.02313 -> .00557 for eff -.00034 / effB -.00088. That is the number a better retirement
predicate is playing for.

[R8 20:50] CLOSED, A QUESTION NOBODY HAD CHECKED: "ALWAYS DELETE THE SEED" IS CORRECT, AND IT IS
CORRECT STRUCTURALLY, NOT BY TUNING. In the barrel/transition duplicate cell {carried bare pixel
seed (type 8), delivered SEEDLESS chain TC (type 4, isChain)} the choice of victim is an EXACT
wash on duplicate rate, fake rate, efficiency and all four displaced bands, and STRICTLY WORSE on
track length if the chain is deleted. 977 evt, window-fix baseline (offline A07 pricer on
d3_ref/r_E1_977.root; the pricer's baseline reproduces r_E1_977 to 5 decimals on all of eff
.80909 dupB .02313 dupT .01993 dupE .07091 dupO .04851 fakB .05507 v15 .80000 v510 .72161
v1030 .71254 d15 .58220):
  policy (6149 kills/977 evt in both) dupB    dupT     eff    fakB   nhB   nhB(OT rows)  zeroLen%B
  BASELINE                           .02313  .01993  .80909  .05507  9.868   10.490        5.923
  delete SEED  (production)          .00560  .01034  .80909  .05556  9.956   10.490        5.085
  delete CHAIN (never tried)         .00560  .01033  .80909  .05555  9.858   10.485        5.975
  delete worse-of-pair (oracle)      .00560  .01034  .80909  .05555  9.950   10.490        5.145
  ALL FOUR DISPLACED BANDS IDENTICAL IN EVERY ROW (v15 .80000 v510 .72161 v1030 .71254 d15 .58220).
THE ORACLE BOUND FOR ARBITRATION IS THEREFORE EXACTLY ZERO, and the reason is a recon fact
(977 evt, r8_ref/arb977.txt): in this cell the seed covers EXACTLY 1 sim and belongs to EXACTLY 1
duplicate group (mean 0.0000 OTHER groups), and so does the chain; both are in-cut. The two
deletions therefore remove the SAME 2 rows from the dup numerator and the SAME 1 row from the
denominator -- identical arithmetic by construction, with only the length metric free to differ,
and the chain carries mean 10.93 nhitOT against the seed's 0.
=> The "keep the dirtier row" argument does NOT apply to this cell: a FAKE row matches no sim and
is therefore in NO duplicate group, so the bare-chain class's .1512 barrel fake rate lives in a
population DISJOINT from the duplicate cell. No arbitration inside a duplicate pair can reach any
of the fake excess. R6/R1: the selector really is the only lever on dupB; R7: conversion is the
only route that can move dup and fake together.
ALSO A TRAP FOR ANYONE USING THE OFFLINE PRICER: d1_ref/d1_extract.py CRASHES on any ntuple
without tc_dbgPls (ROOT 6.36's TTree.GetBranch returns a non-None null proxy, so its `has()` test
always passes) -- e.g. on the round's own baseline d3_ref/r_E1_977.root. Fixed copy that tests the
branch NAME LIST: r8_ref/r8_extract.py. Baseline pkls already built:
r8_ref/r8_E1_300.pkl and r8_ref/r8_E1_977.pkl (the window-fix baseline, a07 schema).
Tooling: r8_ref/r8_arb.py (set-safe incremental arbitration pricer, any victim policy).

[R4 21:00] 977-EVENT REAL-RUN CONFIRMATION of the endcap helix-identity bare-seed dedup, and
the offline price was EXACT. Baseline = r4_ref/r_DUMP977 (my own protoR4 run of the round's
config; headline reproduces FINDINGS to the digit). Command:
  NEV=-1 LSTN=.../LSTNtuple_instr_977evt.root r4_ref/r4_run.sh AB1 \
      -R4B3 0.002 -R4BP 0.05 -R4BR 0.5 -R4BM 1.0
i.e. ENDCAP ONLY, retire a bare pLS row when a BETTER-pLS-score bare row has |dEta| < 0.002,
|dPhi| < 0.05, the same charge, |ln(pt1/pt2)| < 0.5 and both pt >= 1.0. Set-safe by
construction (the best-score member of every cluster always survives). 11.523 seeds/evt
retired -- the offline pricer predicted 11.506, and every metric it predicted came out right:

                      BASE      AB1     delta        AB2 (|dPhi|<0.1, lnPt<0.3)  delta
  dup overall        .0485    .0356   -.0129        .0336                       -.0149
  dup endcap         .0709    .0473   -.0236        .0435                       -.0274   (LST .0856)
  dup barrel         .0231    .0231   +.0000        .0231                       +.0000
  dup transition     .0199    .0199   +.0000        .0199                       +.0000
  eff overall        .8091    .8088   -.0003        .8088                       -.0003
  eff barrel         .9249    .9249   +.0000        .9249                       +.0000
  eff transition     .8789    .8789   +.0000        .8789                       +.0000
  eff endcap         .7446    .7439   -.0007        .7438                       -.0008
  fake overall       .0492    .0494   +.0002        .0495                       +.0003
  fake barrel        .0551    .0551   +.0000        .0551                       +.0000
  fake transition    .0592    .0592   +.0000        .0592                       +.0000
  vxy [1,5)          .8000    .7993   -.0007        .7991                       -.0009
  vxy [5,10)         .7216    .7216   +.0000        .7216                       +.0000
  vxy [10,30)        .7125    .7125   +.0000        .7125                       +.0000
  dxy [1,5)          .5822    .5822   +.0000        .5822                       +.0000
  dxy [5,10)         .2452    .2452   +.0000        .2452                       +.0000
  mean nhitOT        6.435    6.482   +.047         6.489                       +.054  (master 6.519)
  nTC             2004319  1993061   -11258      1991234                       -13085

So: dup overall -.0129 (to .0356 vs LST .0518), endcap -.0236 (to .0473 vs LST .0856), track
length +.047, 0.56% fewer TCs, for eff -.0003 (inside the .0005 budget) with barrel and
transition EXACTLY untouched and every displaced band except vxy[1,5) BIT-IDENTICAL. vxy[1,5)
costs 3 distinct sims of 4560 where we lead LST by +.0281. Risk-averse variant -R4BM 1.5
(6.82/evt): dupE -.0146, eff -.00015, vxy[1,5) -.00044.
CAVEAT TO STATE WITH THE HEADLINE: 97% of the endcap bare-seed duplicate rows are LST-CARRIED
type-8 rows (isZp8 is 5.31 of 813.04 endcap emitted rows), so this cell is LST's own pLS
admission, not something the chain redesign introduced -- the same fix would help LST master,
and the gain is not a chain-pipeline result.
COMPOSES WITH R2: R2's recommended -RPSA 1e7 raises dupE +.00202; this absorbs it 10x over.
FULL WRITE-UP incl. both integrated placements (a third arm in ChainCrossClean.h = what was
measured; or 6 lines in CheckHitspLS pass 2, Kernels.h:93-101, simpler but a larger candidate
set that must be re-measured): standalone/r4_ref/RECOMMENDATION.md

[R3 22:20] WHERE THE FAKE EXCESS ACTUALLY SITS: FIVE-LAYER BARE CHAINS, AND IT IS A COUNT
PROBLEM, NOT AN OVERLAP PROBLEM (for R5). 1000 evt, |eta| < 1.7, pt > 0.9, by object length:
  nLayers   OURS nTC/evt  fk/evt   FR   |  MASTER nTC/evt  fk/evt   FR
  5 (bare)     109.69      21.25  .1937 |     36.45         6.09   .1669
  6 (bare)      69.79       4.13  .0592 |     48.05         1.03   .0214
  4 (bare)      13.18       7.46  .5661 |     18.88        14.59   .7725   (we are BETTER)
51% of our whole barrel+transition fake numerator (21.25 of 41.4/evt) is FIVE-LAYER bare chains,
a class we emit 3.0x more of than master emits 5-layer T5s; for master the same class is 19% of
its numerator. Master's bare objects are mostly SIX-layer (48.1 vs 36.5); ours are mostly FIVE
(109.7 vs 69.8).
AND THE EXCESS IS NOT HIT-DUPLICATION: measured on the delivered TC sets (win_rv1000 tc_hitIdx /
tc_hitType), NO two surviving TCs share >= 3 outer-tracker hits, ours or master's. Only 4.5% of
our real bare chains and 35% of our fake ones share even 2 OT hits (one full MD) with another
surviving TC, so the claim system already prevents heavy sharing at build time and LST's own
crossCleanT5 threshold (>= 4 shared OT hits for a 10-hit T5) would not fire on ANY of our chains.
The extra 5-layer chains are distinct hit combinations the welder builds and the gate admits -
so the lever is the weld/gate, not another crossclean.
FAKE RATE vs pT is the sharpest signature of the difference (|eta| < 1.7): master's fake rate
FALLS with pT (.0533 at 0.9-1.2 GeV -> .0285 at 2.2-3.2 -> .0340 at 3.2-5) while ours RISES
(.0553 -> .0598 -> .0851). 45% of our |eta|<1.7 fake excess is above 2.2 GeV, from 15% of the TCs.
FALSIFIED: you cannot cut on pT to fix it - the high-pT large-dca rows ARE the displaced lead.
Priced exactly: killing br3 (5+ exempt) rows with pt > 3 gives fakB -.0029 but dxy[1,5) -.0533
and vxy[10,30) -.0810; killing br1 (T4 exempt) rows with pt > 3 gives fakB -.0019 for
dxy[1,5) -.0198 (88 distinct displaced sims). Tooling: r3_ref/r3_price.py, r3_ref/r3_anat.py.

[R5 21:55] FALSIFIED - "THE WELD STOPS TOO EARLY". kChainWeldSweeps is 3 and the weld is NOT
converged there (per-sweep weld counts on 3 events: 2818/1307/506, 3035/1502/604, 3514/1664/640 -
sweep 2 is still welding). But running 8 sweeps (protoR5 env R5_WELD_SWEEPS=8, 60 evt A/B) moves
EVERY metric the wrong way: eff -.0002 effB -.0006, vxy[10,30) -.0045 dxy[1,5) -.0053, dupB +.0012
dupT +.0012 dupO +.0008, fakB +.0011 fakT +.0013, for nhOT +.0125 nhB +.0107 and only +82 TCs out
of 120870. The sweeps past 3 are the mutual-best losers' losers: they DO make chains longer, and
the extra length is noise. Do not raise the sweep count (it also costs GPU time).

[R5 22:00] THE K9 CLAIM TOLERANCE IS A HARD DISPLACED-vs-FAKE FRONTIER, AND IT CANNOT BE SPLIT BY
ETA, BY dcaXY, OR BY THE 3-CLASS GATE MARGIN. Full table + distinct-sim prices in r5_ref/FRONTIER.txt
(300 evt, vs r_BASE300; gate 33/33 bit-identical at neutral, r5_ref/r5_bitid.py).
Today's tolerance is effectively "a candidate may ride on at most 1 already-claimed MD" (-FC 1 -> 2
hits, OR frac <= 0.20), and the R5 census showed the surviving chain-chain duplicates sit EXACTLY
there. Removing that allowance:
  -FC 0 -FCX 1 (zero sharing)        fakB -.0200 fakT -.0140 dupB -.0048  eff -.0011  81 DISP1 sims
  + margin exemption -FCM 0          fakB -.0143 fakT -.0094 dupB -.0004  eff +.0004  23 DISP1 sims
  + margin exemption -FCM -1.0       fakB -.0099 fakT -.0047 dupB -.0001  eff +.0003  11 DISP1 sims
  + margin exemption -FCM -1.5       fakB -.0073 fakT -.0020 dupB -.0001  eff +.0002  11 DISP1 sims
  -W 0.20 with displaced braid-exempt  dupB -.0013 but dupO +.0008 dupE +.0022 effT -.0020  2 sims
Three separate attempts to protect the displaced population all FAIL:
  * eta: restricting the strict rule to |eta(innermost T3)| < 1.1 keeps the FULL fakB -.0143 and
    keeps the full displaced cost too (dxy[1,5) -.0145 vs -.0156 global). The barrel fake excess
    and the barrel displaced lead are the SAME chains.
  * dcaXY (-FCE 1/2, exempt = large-dca kept loose): STRICTLY DOMINATED - eff -.0017 (worse than
    zero-sharing's -.0011) for dupB -.0025 and fakB -.0066. Loosening the displaced branch makes
    it accept and CLAIM, which then blocks the IP chains under the strict rule.
  * the 3-class gate margin mX: gives the best exchange rate found (1.1 sims per .001 fakB vs the
    REFUSED -MRB -1.2's 2.3) but the smallest useful step still costs 11 distinct DISP1 sims = 5.6%
    of the +197 lead, i.e. ~2x what the maintainer already unwound.
Also: -W 0.30 and -W 0.35 are BIT-IDENTICAL to the frozen -W 0.50. The braid is a step function at
20%: no candidate ever overlaps 25-50% of an accepted owner's slots, so the only braid decision in
existence is "kill the 1-MD sharers or not". Nothing between exists to tune.
=> RECOMMENDING NO CONSTRUCTION-SIDE CONFIG CHANGE. The two new prototype flags (-FCM margin-
conditioned strict claim, -FCMZ its eta restriction) are in protoR5 and are no-ops at default;
they are the instrument that PROVED the frontier, not a proposal. R3: the barrel fake rate has a
-.0143 lever sitting right here and its price is the displaced lead - if you find a discriminator
that separates a fake bare chain from a displaced bare chain riding on the same claimed MD, that
lever converts immediately (one flag, no new machinery).

[R3 22:55] MEASURED WIN, FREE: -CC9, the T4-class crossclean against the delivered seeded rows.
RULE (truth-free, one comparison, no eta/pt/dR parameter): delete a delivered T4-CLASS BARE CHAIN
(tc_type 9) that shares >= 2 OUTER-TRACKER HITS (one complete mini-doublet) with a delivered
SEEDED row (tc_type 7 pT5-class or 5 pT3-class). Set-safe by construction: the reference row is
never itself deletable, so the D1 set-deletion trap (T3) does not apply. In the barrel this cell
is 1.79 TC/evt at a 98.3% truth-fake rate and 100% of it comes from admission branch br=1 (the
T4-class EXEMPT arm); the ovS=0 part of that same arm (8.0 TC/evt, FR .59, sole cover of .295
sims/evt nearly all DISPLACED) is untouched. tc_type 9 rows never share 3+ OT hits with a seeded
row, so ">= 2" is the maximum and "-CC9 3" is an exact no-op.
REAL A/B, 977 evt, protoR3 (= protoD3 + flag -CC9), baseline overrides
  -D3W 3.665 -D3W2 3.15 -D3W3 3.75 -XCT 1e9 -XCT2 1e9 -XCT3 1e9  [+ -CC9 2]
  r3_ref/r_agg_GATE977.txt vs r3_ref/r_agg_CC9A.txt:
  GATE (-CC9 absent): 29/29 compare_ab metrics IDENTICAL to the frozen r_E1_977 baseline.
  -CC9 2 :  fakB .0551 -> .0514 (-.0037)   fakT .0592 -> .0589 (-.0003)   fakE .0430 (+.0000)
            fake overall .0492 -> .0481 (-.0012)
            eff overall .8091 (+.0000)  effB .9249 (+.0000)  effT .8789 (+.0000)  effE .7446 (+.0000)
            dupB .0231 (+.0000)  dupT .0199 -> .0196 (-.0003)  dupE .0709 (-.0000)
            vxy[1,5) .8000 (+.0000)  vxy[5,10) .7216 (+.0000)  vxy[10,30) .7125 (+.0000)
            dxy[1,5) .5822 -> .5819 (-.0003)  dxy[5,10) .2452 (+.0000)  dxy[10,30) .0315 -> .0309
            mean nhitOT barrel 9.868 -> 9.876 (tracks get LONGER)   nTC -2.33/evt
  EXACT COST, counted as distinct sims over the 977 events: TWO. Zero sims leave the efficiency
  denominator (both are vxy >= 2.5, i.e. band-only); one is in dxy[1,5), one in dxy[10,30); all
  four vxy bands lose ZERO. Independently priced offline on the INTEGRATED 1000-evt baseline
  (win_ref/win_rv1000.root) to the same numbers: fakB -.0035, eff +.0000, dispN -2.
PROVENANCE: this is the missing arm of LST master's own CrossCleanT5 (b42d8f97ad5
src/alpaka/TrackCandidate.h:207), which marks a T5 duplicate when it shares >= 4 OT hits with a
pT5/pT3. That rule was deleted with the T5 pipeline and the 4-layer bare class never had one.
NOTE for whoever integrates: no surviving TC pair in EITHER our output or master's shares >= 3 OT
hits, so LST's own >= 4 threshold would fire on nothing here - the class-appropriate threshold for
an 8-hit object is one full MD.
INTEGRATED-TREE FORM: a post-emission pass over the assembled TrackCandidates (hit -> delivered
type-7/5 row map, then drop type-9 rows with a >= 2 hit partner). Reference implementation:
protoR3/main.cc, block "R3 -CC9", immediately before writer.fillEventHybrid.

[R3 23:15] ANSWER TO R5's REQUEST (the 1-shared-MD frontier), for the T4-class sub-case only, and
WHY it is free there and nowhere else. R5 asked for a discriminator that separates a fake bare
chain from a displaced one riding on the same claimed MD. There is one, and it is not a score:
WHO THE PARTNER IS.
  partner = a delivered SEEDED (pixel-backed) row, victim = 4-layer bare chain (tc_type 9)
      1.79 TC/evt barrel, 98.3% fake, priced fakB -.0037 at ZERO efficiency and TWO distinct
      displaced sims -> shipped as -CC9 (see [R3 22:55]).
  partner = a delivered SEEDED row, victim = 5-layer bare chain (tc_type 4)
      6.41 TC/evt barrel, only 63.6% fake: eff -.0019, dxy[1,5) -.0078, 83 distinct displaced
      sims. NOT free. Restricting to nlayers 5 and pt > 2.2 gets it to eff -.0003 but still
      25 displaced sims. DO NOT SHIP.
  partner = another BARE chain (the dedup loser by nlayers/nhits/pt), victim = tc_type 9
      1.16 TC/evt, 82% fake, eff -.0000 but dxy[1,5) -.0052 and 30 distinct displaced sims.
The asymmetry is structural, not statistical: a SEEDED partner cannot be the fake cover of the
victim's sim (its own class fake rate is .0062 barrel / .0131 transition), so the sim survives the
deletion by construction; a BARE partner can be fake, so deleting the victim can orphan the sim.
That is the same set-safety argument as D1's (require the reference cover to be a class the rule
cannot delete), and it is why R5's chain-chain frontier stays expensive while its
seeded-partner slice is free. It only pays for the 4-layer victim class because that is the only
class where "shares a full MD with a seeded track" is nearly synonymous with "is a fragment of it".
COMPOSITION WARNING for whoever integrates: -CC9's rows are a subset of the population R5's -FC 0 /
-FCM strict-claim family would also remove, so the two fakB gains DO NOT ADD. -CC9 additionally
catches an ORDER-DEPENDENT residue the claim cannot (a T4-class chain accepted BEFORE the seeded
chain in the K9 greedy walk was never blocked by the claim), which is why it still fires at all
with -FC 1 in force. Priced on protoD3 head with the frozen config; anything R5-side changes it.
