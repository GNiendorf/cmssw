# Displaced-efficiency ledger

Running record of every mechanism that SPENDS displaced efficiency, with the measured
price, the loss mechanism, and the recovery lever. Maintained for the later re-evaluation
round ("can we do better") and the head-retrain round. Units: distinct DISP1 sims lost vs
the stated baseline (tool: a08_ref/a08_distinct.py; vxy/dxy band rates double-count sims,
distinct-sims is the honest currency). CHAINFINAL lead over LST was +716 distinct DISP1
sims on the 977; CHAINFINAL2 keeps +639 (spent 77).

## Where the 77 went (977, F3 composition = naive F1+F2 sum, NO overlap savings)

| lever | sims spent (977) | loss mechanism | source record |
|---|---|---|---|
| -a 6.875 -> 5.0 (barrel attach conversion) | ~34 (F1W total incl. -a2/-CCS) | bare chains CONVERTED WITH A WRONG SEED: the chain absorbs a pixel seed that is not its own sim's, and the T5->type7 sim-match is lost. 100% of F1's losses are this class. | f1_ref/STATUS.md (per-point: -a 5.5 = -4, 5.0 = -6, 4.5 = -11 on 300) |
| -CCS 6.0 / -CCS2 5.0 | ~1-6 inside the F1 total | suppressed bare chain was the only row matching a displaced sim (its seeded twin matched a different sim) | f1_ref/STATUS.md M3/M4; bar 4.0 = -13 v1030 sims on 300 (why the bar stays at 6) |
| -MRB -1.2 (barrel exempt fake bar) | 6 (300-evt scan; scales into F2's 45) | tightened admission bar on the DISPLACED-EXEMPT branch removes real large-DCA chains | f2_ref/STATUS.md M4 frontier (-1.5 = 3, -1.35 = 6, -1.0 = 9, -0.8 = 14) |
| -MRT -1.2 (transition exempt bar) | 3 (300 scan) | same, transition | f2_ref/STATUS.md |
| -XCT 4->3.75 / -XCT2 4->3.5 (dedup thresholds) | ~0 (measured INSENSITIVE: -18 on 300 with XCT off/3.75/3.5 alike) | n/a - the threshold knobs do not carry the spend | f3_ref/STATUS.md descent |
| F2 remainder (XCT2<=3.5 population + bar tails) | rest of F2's 45 (38 T5 + 9 pLS covers) | exempt-branch T5/pLS covers | f2_ref final report |

Earlier round (CHAINFINAL itself): synthesis spent 14 of the then-730 lead, all from
-a 6.875->6.0 wrong-seed conversions (synth_ref/STATUS.md; LOST 8/GAINED 22 at DISP1).

## Dial-back levers (measured prices, all still runtime flags)

- -a 5.0 -> 5.5: recovers ~6-7 sims, costs dupB +.0020, eff -.00009 (f1/f3)
- -CCS2 5.0 -> 6.0: recovers ~2-6 sims, costs dupT +.0015, no eff change (f3)
- -MRB -1.2 -> -1.5: recovers ~3 sims, costs fakB +.0026 (f2 frontier)
- -MRT -1.2 -> -1.65: recovers ~3 sims, costs fakT +.0046 (f2 frontier)
- NEVER re-tighten via -M4B: 18 sims for fakB -.0069 - dominated, the whole br1 fake
  price IS the displaced win (A07 exempt-branch proof, a07_ref)

## The structural recovery path (queued, not dropped)

The dominant loss class is WRONG-SEED CONVERSION - a head-quality failure, not a
mechanism failure: the attach head picks the wrong pLS for a displaced chain because it
was trained on prompt-dominated chain pairs and is blind to target quality / eta-
miscalibrated (a03/a05/a11 evidence). The queued head retrain (with target-quality
features + eta normalization + displaced-enriched training from the cube sample round)
should directly reduce wrong-seed picks, which recovers displaced sims AND barrel dup
simultaneously - the two open reservations are the SAME defect. Re-derive every
threshold in this ledger after any retrain.

Also queued: the dxy[10,30) band deficit (-.0225 vs LST) predates all of this (inherited
at CHAINFINAL, untouched by 15+7 agents, tracks never formed upstream - cube-round
territory, a08_ref).
