# MID-ROUND BRIEF: BARREL DUPLICATE RATE (agents R6 R7 R8, joining a round in progress)

You are joining a five-agent round that has already produced a lot of evidence. The maintainer
added three more agents because the round converged its EVIDENCE onto the barrel duplicate rate
but only one agent (R1) was actually working it. Your job is the barrel duplicate rate.

READ FIRST: standalone/FINDINGS_R2.md (the shared log - the round's baseline, inherited facts,
traps, tooling, and every finding logged so far). This file is the DIGEST of what the round has
established since, so you do not have to re-derive it. Read both.

--------------------------------------------------------------------------------------------
## THE TARGET

  dup barrel      .0231   vs LST master .0097     <- YOUR TARGET, 2.3x
  dup transition  .0200   vs LST .0131            <- same cell, same mechanism, also yours
  dup endcap      .0710   vs LST .0849            (we are BETTER; R4 is working a huge free
                                                   endcap win separately - stay out of it)
  eff overall     .8092   vs LST .8100
  displaced       vxy[5,10) +.0764, vxy[10,30) +.0854, dxy[1,5) +.0715 OVER LST

DISPLACED IS SAFE IN YOUR CELL, MEASURED TWICE: barrel bare seeds are SOLE cover of a displaced
sim 0.00 times per event (transition 0.08), and every cleaning-mechanism price measured this round
came back EXACTLY 0.00000 on all four displaced bands. So barrel duplicate work does not spend the
displaced lead. Report the bands anyway - the maintainer requires it in every headline.

EFFICIENCY BUDGET: effD = 22633 sims on the frozen 300, so ONE wrongly deleted sole-cover row =
.0000442 of overall efficiency. A .0005 budget is 11 rows over 300 events = .038 rows/evt.

ORACLE CEILING (what "solved" looks like): deleting every set-safe duplicate bare seed plus every
fake bare seed in |eta| < 1.7 gives dupB .0285 -> .0053, dupT .0232 -> .0100, at eff EXACTLY
unchanged (0 sims lost), with fake rate and track length improving too. So the cell is 100%
addressable and the entire remaining gap is SELECTOR QUALITY.

--------------------------------------------------------------------------------------------
## WHAT THE ROUND HAS ALREADY ESTABLISHED (do NOT re-derive any of this)

THE CELL, EXACTLY: 100% of the barrel duplicate excess is "a carried bare pixel-seed row (type 8)
duplicates a DELIVERED SEEDLESS CHAIN TC (type 4)". Measured three independent ways:
  * R5's group census: the (bare chain TC, carried bare pLS) type-pair is 4.790 groups/evt for us
    vs LST 1.450 -- x2 rows = 6.68 of the +7.45 barrel duplicate-ROW excess, i.e. 90% of the gap.
    Chain-chain groups are only 13% of the gap.
  * R4: barrel bare-ONLY duplicate clusters = 0.000/evt, and all 5.148/evt barrel duplicate rows
    have a NON-bare reference cover.
  * R1/D1: 84% of the barrel duplicate bare-seed rows are -ZP8 post-deletion additions.

FOUR DEAD ENDS, ALL MEASURED THIS ROUND. Do not spend an hour on any of them:
  1. NO MISSING RETIREMENT ARM. R2 built the obvious one (retire a bare seed whose max attach
     logit over delivered ATTACHED type-7 chain TCs clears a bar - the substitute for the other
     half of LST's deleted pT5 CrossCleanpLS arm). At bars 3.665/3.15 it retires 4.24 seeds/evt
     for dupB -.00019 and eff -.00062 (effB -.00137). MECHANISM: a bare seed scoring high against
     an ATTACHED chain but sharing no pixel hit with that chain's winning seed is usually a
     DIFFERENT track - the winner claimed the hits and the loser's own continuation was never
     built. The reference cover must be a SEEDLESS chain TC, which the window-free arm already
     scores at 3.665.
  2. ADMISSION CANNOT REACH IT. The admitted bare-seed universe is pairwise hit-disjoint (zero
     seeds share >= 2 pixel hit rows - LST's two CheckHitspLS passes guarantee it), so the
     -RD/-RDT ">= 2 shared pixel hits" family definition is VACUOUS here, and every emitted bare
     row already has nOwnPixShare == 0 (the -XC pixel arm retires anything sharing >= 1 pixel hit
     with an anchor, 3026 seeds/evt).
  3. CONSTRUCTION IS NOT SPLITTING TRACKS. Barrel splits (disjoint hit sets covering one sim) are
     0.14/evt. Our chain-chain duplicates are all "PARTIAL": two 5-6 layer chains sharing EXACTLY
     ONE MD, sitting exactly on K9's claim budget. LST has a COINCIDENT duplicate population we
     structurally cannot have.
  4. OWNERSHIP / CONTENTION VARIANTS of the bare-chain arm (one-chain-one-seed, mutual-best,
     greedy 1-1 matching) are worth ZERO - the arm is already effectively 1-1. Owner-branch
     credibility (-XCQ) costs 4x what its offline model predicted.

THE SELECTOR, AND WHY IT IS THE WHOLE PROBLEM: the retirement decision is a threshold on the
attach head's logit for the (seed, seedless chain) pair, banded by |seed eta| (xcTheta 3.665 /
3.15 / 3.75 after this round's window fix). Its discrimination is AUC ~.900 against the oracle
label, while a GBDT on the DUMPED features reaches .957. Also measured: the incumbent scores are
nearly uninformative about what retirement COSTS -- for "is this seed a sole cover", cPairLogit
has AUC .598 and plsBestChainLogit .607, while log10(pLS pt) ALONE has .745.

THE HEAD'S REAL OPERATING POINT (R1): at the production cut 6.0 the attach head has precision
.6956 and recall .3062 on true (chain, pLS) pairs - it takes 31% of the true pairs and 3 of every
10 attaches is wrong. Frontier: cut 4.0 prec .500/rec .535 | 5.0 .593/.402 | 7.0 .829/.212.
THE HEAD'S TRAINING BUG (R1, confirmed independently by R2): every training dump ever produced
enumerated chain targets with nLayers >= 5, but the DEPLOYED pipeline scores 4-layer chain targets
too. The head has never seen one. 4-layer chains are 16.5% of chain-target rows and 3.89% true vs
1.09% for the chain universe overall (3.6x richer) -- and 59% of the window-free retirement
channel's decisions are made on 4-LAYER targets. Also 96% of the head's true chain pairs are
pileup-sim-only, i.e. sims outside the efficiency denominator.

THE CONVERSION FACT (R5): 29.7% of our barrel chain TCs fail to attach a pLS, vs LST's 13.8%
seedless share -- we emit 2.29x as many seedless quintuplet-class rows as LST, and each is 1.52x
more likely to be fake (class fake rate .1369 vs LST .0901). Every seedless chain is a row a bare
seed can then duplicate. Historical caution: lowering the attach margin -a DID buy duplicates but
cost displaced efficiency, and the mechanism was measured - the losses were bare chains converted
with a WRONG SEED (the true pT5-class match lost). So conversion has to be made right-seed-safe,
not just more permissive.

--------------------------------------------------------------------------------------------
## WHO OWNS WHAT RIGHT NOW (do not duplicate)

  R1  the attach head itself: features, labels, training distribution, retrain, WP calibration.
      The 4-layer training gap is R1's to fix. DO NOT retrain the head.
  R2  deletion / simplification of cleaning mechanisms; has finished pricing them.
  R3  barrel/transition fake rate census and fixes.
  R4  the endcap bare-only cluster dedup (a large free win) - stay out of the endcap.
  R5  chain construction; has falsified the split hypothesis.
  R6  YOU, if you are R6: the SELECTOR - close the .900 -> .957 discrimination gap with a
      predicate over already-computable quantities. No new trained component.
  R7  YOU, if you are R7: CONVERSION - make the attach decision RULE convert more of the 29.7%
      seedless barrel chains, right-seed-safely, without touching the head's training.
  R8  YOU, if you are R8: ARBITRATION - when a bare seed and a seedless chain TC duplicate each
      other, we always delete the SEED. Ask whether that is the right choice.

ARCHITECTURE CONSTRAINT (maintainer, hard): the end state is ONE network answering "is this pLS
and this OT object a track", plus one cut. A SECOND trained component is not acceptable unless it
removes more machinery than it adds - three agents built one last round and it bought ~.002 dupB
over a free fix and was refused. Predicates over existing quantities are welcome; new MLPs are not.

--------------------------------------------------------------------------------------------
## WHERE TO WORK, AND HOW TO MEASURE

  cp -a protoD3 protoR<n>        <- protoD3 is the ONLY prototype with the window-free channel
Baseline override line (offline):
  -T3F 0.10 -XC4 1 -RPSA 5.5 -a 6.0 -a2 6.0 -a3 6.0 -EX 0 \
  -D3W 3.665 -D3W2 3.15 -D3W3 3.75 -XCT 1e9 -XCT2 1e9 -XCT3 1e9
Reference runs: d3_ref/r_E1_977* (977 evt), win_ref/* (1000 evt, integrated). In the INTEGRATED
tree the ChainConfig.h defaults ARE the baseline; do not edit the integrated tree - recommend and
it will be ported for you.

* Reproduce the baseline headline numbers in YOUR copy before trusting any delta.
* Gate every code change: neutral setting must reproduce the baseline BIT-IDENTICALLY (33/33
  branches, rebase_ref/cmp_branches.py) AND 0.00000 on all 29 metrics.
* Price deletions offline first with d1_ref/d1_study.py (validated to the digit) or
  iterations/a07_ref/a07_sim.py - seconds instead of a 13-minute A/B.
* Confirm anything you recommend on 977 events, per eta band, dup AND fake AND efficiency, with
  the displaced bands in the headline.
* TRAP: "another surviving TC covers my sim" is a single-row counterfactual and is INVALID for a
  rule that deletes a SET. Require the reference cover to be a NON-bare-seed TC.
* TRAP: build in the same env - pushd standalone && source setup.sh && cmsenv && source setup.sh.
* TRAP: mean nhitOT is diluted by zero-length rows; do not quote it as an object-length claim.
* NEVER put files in /tmp. Everything in standalone/r<n>_ref/.
