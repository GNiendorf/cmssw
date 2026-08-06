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
