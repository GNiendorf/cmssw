# R2 - DELETION AND SIMPLIFICATION (toward one head, one cut)

READ FIRST: standalone/FINDINGS_R2.md - the baseline, the inherited facts (do not re-measure
them), the traps, the tooling, and the hard architecture constraint. It also lists the other four
agents' assigned directions so you know what is not yours.

## YOUR DIRECTION

Two halves, and they are the same job:

(a) THE TRUNCATED-RESIDUAL DEFECT, applied where it has not been. D4 proved that the -XCT
    retirement selector was thresholding the truncated residual of the attach acceptance cut: no
    takeable seed can have a pair logit above the acceptance bar BY CONSTRUCTION, so the frontier
    provably vanishes at bar = margin. That is why three rounds of band-threshold work on it went
    nowhere, and it is why dropping the window (this round's new baseline) worked instead.
    -RPSA and -T3F have the SAME STRUCTURE and have never been examined for it. Find out whether
    they do, and if so what the correct predicate is.

(b) REMOVAL. The maintainer's requirement is that this algorithm end up SIMPLER than LST, and the
    stated end state is one head answering "is this pLS and this OT object a track", one cut, and
    nothing else. Every mechanism in the cleaning/retirement path is a candidate for deletion.
    For each one you examine, measure what deleting it actually costs on the current baseline -
    several of these were tuned in earlier rounds against configs that no longer exist, and some
    may now be worth nothing or be actively harmful (the extension stage was exactly that: it was
    measured a physics LIABILITY and was ripped out).

A deletion that is free, or that costs less than it saves in complexity, is a FIRST-CLASS RESULT
this round - report it with the same weight as a dup improvement.

## OUT OF BOUNDS

The attach head's features/labels/training (R1), the fake-rate mechanisms (R3), the seed
admission universe and -RD/-RDT (R4), chain construction (R5). You may propose that another
agent's mechanism be deleted - measure it and write it in the shared log rather than editing
their layer.

## WHAT IS ALREADY KNOWN ABOUT YOUR TARGETS

* The window fix (this round's baseline) already took the free win out of the -XC bare-chain arm
  by DELETING a condition. Expect the same shape of answer elsewhere: the wins here have been
  removals, not additions.
* Ownership variants of the bare-chain arm are worth ZERO (falsified three ways). -XCQ owner
  credibility costs 4x its offline prediction. Do not redo either.
* D1's oracle says the barrel/transition bare-seed cell is 100% addressable at zero efficiency
  cost, so if a predicate is losing efficiency to buy dup, the predicate is wrong, not the trade.
* A trained retirement head buys only ~.002 dupB over the free window fix (three agents, three
  variants, same answer) - and the maintainer has ruled a second network out. If your conclusion
  is "this needs a new network", the answer is no; find the predicate instead.

## MEASUREMENT PROTOCOL

* Baseline = the window-fix production config (FINDINGS_R2.md). Reproduce its headline numbers
  before trusting a delta.
* Gate every code change: with the change disabled/neutral the binary must reproduce the baseline
  BIT-IDENTICALLY (33/33 branch comparison). State the gate result.
* Price candidate deletions with d1_ref/d1_study.py before spending a 13-minute A/B.
* Confirm anything you recommend on 977 events, per eta band, dup AND fake. Scalars alone will be
  rejected.
* Displaced bands: report them. The maintainer unwound two changes from last round for costing
  displaced efficiency - a dup win that moves the displaced bands is not a win.

## DELIVERABLE

Either a corrected predicate (with the mechanism explained, not just a tuned number), or a list
of mechanisms that can be deleted with the measured price of each, or both. If a mechanism turns
out to be load-bearing, say so with the number - a confirmed "this one is doing real work" is
worth having.
