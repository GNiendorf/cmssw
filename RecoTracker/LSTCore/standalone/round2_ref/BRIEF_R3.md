# R3 - FAKE RATE, BARREL AND TRANSITION

READ FIRST: standalone/FINDINGS_R2.md - the baseline, the inherited facts (do not re-measure
them), the traps, the tooling, and the hard architecture constraint. It also lists the other four
agents' assigned directions so you know what is not yours.

## YOUR DIRECTION

The fake rate is the half of the gap vs LST master that NOBODY worked last round:

  fake barrel      .0551   vs LST .0437     <- yours
  fake transition  .0592   vs LST .0454     <- yours
  fake endcap      .0430   vs LST .0463     (we are BETTER - protect it)
  fake overall     .0492   vs LST .0448

Start with RECON, not with cuts. Nobody in this project has yet characterized WHAT our barrel
fakes are: which object class produces them (bare seed / bare-T3 pT3-class / chain / seeded
chain), how many hits they have, whether they are near-miss (mostly one sim, a few stray hits) or
genuine combinatorial garbage, and how that composition differs from LST's fakes at the same
eta. That census is the deliverable even if the fix is somebody else's layer - and it is cheap:
the output ntuple already carries per-TC sim matching, and the tooling section of FINDINGS_R2.md
lists the dumps that make the per-class join complete.

The reason recon comes first: the two obvious levers are already known to be double-edged.
The band fake bars (-MRB / -MRT) DID buy fakB -.0057 / fakT -.0026 last round, and were UNWOUND
by the maintainer because they cost displaced efficiency (6 and 3 distinct displaced sims). They
are still in the code, OFF, and re-enabling them is not a new result - it is a trade that was
already refused. If you want to go there you need a version that does not touch displaced tracks,
and you need to show the displaced bands to prove it.

## WHAT IS ALREADY KNOWN THAT BEARS ON FAKES

* D1's oracle deletion (every set-safe duplicate bare seed + every fake bare seed, |eta| < 1.7)
  improved fakes as well as dups: fakB .0545 -> .0533, fakT .0595 -> .0543, at ZERO efficiency
  cost. So part of your gap is reachable by the same selector quality R1/R2/R4 are chasing -
  which means your independent contribution is the part that ISN'T bare seeds. Find out how big
  that part is early, so you are not duplicating their win.
* D2's census: of 47.2 barrel bare-seed survivors per event, 1.5 are truth-fake; transition 2.0
  of 24.8. Endcap 36.2 of 829.9. Compare those against the per-class fake counts you measure and
  you will know immediately whether bare seeds are your problem or a sideshow.
* Deleting "clean ballast" rows (a sim outside the efficiency denominator) RAISES both fakB and
  dupB - the denominator shrinks with no numerator change. Any mass-deletion idea must be checked
  against this or it will look like a regression for a reason that has nothing to do with quality.
* Our mean track length is SHORTER than master's (nhitOT 6.435 vs 6.52). Short objects are the
  usual fake source, and R5 owns why they are short - if your census says fakes concentrate in the
  shortest class, write that in the shared log for R5 rather than chasing construction yourself.

## OUT OF BOUNDS

The attach head's features/labels/training (R1), retirement/deletion predicates as a general
project (R2 - though a fake-motivated predicate that you measure is yours to propose), the seed
admission universe and -RD/-RDT (R4), chain construction (R5).

## MEASUREMENT PROTOCOL

* Baseline = the window-fix production config (FINDINGS_R2.md). Reproduce its headline numbers
  before trusting a delta.
* Gate every code change: neutral setting must reproduce the baseline BIT-IDENTICALLY (33/33).
* Confirm on 977 events, per eta band, fake AND dup AND efficiency. Fake wins that quietly cost
  dup or efficiency will be rejected.
* Displaced bands in the headline, always.

## DELIVERABLE

The barrel/transition fake census (what they are, per class, with numbers), plus whatever fix it
points to, measured. A rigorous census with a clear statement of what the fakes actually are is a
result on its own - it is the thing this project has never had.
