# R4 - THE SEED-ADMISSION UNIVERSE (which seeds are candidates at all)

READ FIRST: standalone/FINDINGS_R2.md - the baseline, the inherited facts (do not re-measure
them), the traps, the tooling, and the hard architecture constraint. It also lists the other four
agents' assigned directions so you know what is not yours.

## YOUR DIRECTION

Everything upstream of the attach decision that determines WHICH pixel seeds exist as candidates
and as output rows: the -ZP8 post-deletion bare-pLS universe, the -RD / -RDT seed-family dedup,
and pLS quality/duplication generally.

Your direction exists because of the single sharpest recon fact of last round:

  84% of the BARREL duplicate bare-seed rows are -ZP8 POST-DELETION ADDITIONS, not LST-carried
  type-8 rows. Barrel type-8 rows 48.19/evt = 36.72 carried + 11.46 -ZP8; barrel type-8
  DUPLICATES 5.53/evt = 0.88 carried + 4.65 -ZP8.

-ZP8 6 defines the post-deletion bare-pLS universe as (isQuad && pLS_isDupAlgPass2 == 0). That
rule adds back exactly the seeds LST's own pLS/T5 embedding CrossCleanpLS killed - and our
substitute for that crossclean does not re-kill them. So the barrel duplicate excess vs LST is,
mechanically, an ADMISSION rule, and everyone else's work is trying to clean up after it.

The question nobody has asked: is the -ZP8 predicate itself right? It was chosen as a
dependency-audit convenience (it reproduces a specific LST state), not because anything measured
it. Alternatives exist in the same family (which pass of the pLS duplicate cleaning to trust,
whether isQuad is the right gate, whether the seed's own quality score belongs in the admission
test at all). Measure them.

Second target: the -RD / -RDT seed-family dedup, which calls two pLS the same seed when they share
>= 2 pixel hit rows. That is LST's definition, not a measured one. If a substantial share of our
barrel duplicate rows are seeds that a slightly different family definition would have merged,
that is a cheap and principled win in the right place - upstream, where one rule removes work for
everything downstream instead of adding a rule that deletes rows later.

## WHAT IS ALREADY KNOWN THAT BEARS ON YOUR DIRECTION

* Join on tc_dbgPls (protoD1's PROTO_DUMP_PLSROW=1), NOT tc_plsIdx - the latter sees only the 16%
  of the cell that is LST-carried, which is exactly the wrong 16% for you.
* D1's oracle: the barrel/transition bare-seed cell is 100% addressable at ZERO efficiency cost,
  and taking it improves fake rate and track length too. If your admission change is right, it
  should approach that oracle rather than trade against it.
* D2's census: barrel 47.2 survivors/evt = 7.5 harness-dup + 1.0 sole-cover + 1.5 fake + ~37
  ballast. Deleting BALLAST raises dupB and fakB (denominator shrinks, numerator does not) - an
  admission rule that removes ballast will look like a regression for a reason unrelated to
  quality. Check this before concluding anything.
* SOLE covers of a displaced sim in the barrel: 0.00/evt (transition 0.08). Barrel/transition
  seed admission cannot spend the displaced lead - but the ENDCAP has 0.44/evt, so an admission
  rule applied globally CAN. Band your changes or measure the endcap explicitly.
* Set-deletion labels: "another surviving TC covers my sim" is a single-row counterfactual and is
  invalid for a rule that changes a whole population's admission. Require the reference cover to
  be a NON-bare-seed TC (trap T3 in FINDINGS_R2.md); getting it wrong understates the cost 3x.

## OUT OF BOUNDS

The attach head's features/labels/training (R1), the general retirement/deletion predicates
-RPSA / -T3F / -XC (R2), fake-rate mechanisms (R3), chain construction (R5).

## MEASUREMENT PROTOCOL

* Baseline = the window-fix production config (FINDINGS_R2.md). Reproduce its headline numbers
  before trusting a delta - and note the window fix changed the -XC arm, which interacts with
  admission: the seeds it retires are drawn from your universe.
* Gate every code change: neutral setting must reproduce the baseline BIT-IDENTICALLY (33/33).
* Price with d1_ref/d1_study.py before spending a 13-minute A/B.
* Confirm on 977 events, per eta band, dup AND fake AND efficiency, displaced bands in the
  headline.

## DELIVERABLE

Either a better admission predicate, measured, or a rigorous statement of why the current one is
already correct and where the 4.65 -ZP8 barrel duplicates per event have to be killed instead.
Both are useful; a guess about either is not.
