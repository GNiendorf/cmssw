# R5 - CHAIN CONSTRUCTION UPSTREAM (weld, gate, claim, assembly)

READ FIRST: standalone/FINDINGS_R2.md - the baseline, the inherited facts (do not re-measure
them), the traps, the tooling, and the hard architecture constraint. It also lists the other four
agents' assigned directions so you know what is not yours.

## YOUR DIRECTION

The construction side of the pipeline, which no round has questioned: the T3 graph (E1 shared-MD
and E2 shared-LS edges), the edge MLP, the mutual-best weld, the 3-class chain gate, the K9
hit-claim arbitration, and K10 TC assembly. Everything that decides WHAT OT OBJECTS EXIST, before
any pixel seed is attached to them.

Every previous round accepted the chains as given and worked on selection after the fact. The
reason to look upstream now is that two of our three deficits vs LST master point there:

  mean nhitOT   6.435   vs master 6.52     <- we build SHORTER objects
  fake barrel   .0551   vs LST   .0437     <- short objects are the usual fake source
  dup barrel    .0231   vs LST   .0099

The specific question: when LST builds one T5, do we build one chain, or two chains that each
cover part of the same track? A track split into two shorter chains produces two OT objects, each
of which can win a pixel seed - which is a DUPLICATE that no amount of seed-retirement selector
quality can fix, because both rows are legitimately different objects. Nobody has measured
whether this happens or how often. If it is common, the fix is upstream and it is worth more than
anything downstream of it. If it is rare, that is a genuinely useful negative result: it would
mean the construction side is sound and the whole remaining gap really is selection, which is
what everyone has been assuming without evidence.

Concrete recon to start from: per sim track in the barrel, count how many delivered chain TCs
cover it and how their hits partition the track's hits (disjoint = a split, overlapping = a real
duplicate). Compare the same census on LST master's T5s for the same events. The dumps listed in
the FINDINGS_R2.md tooling section already carry chain membership; the edge and node dumps
(LST_CHAIN_EDGE_DUMP, LST_CHAIN_NODE_DUMP) expose the weld's decisions if you need to see why a
split happened.

Then: if splits are real, is the cause the WELD (mutual-best stopping too early), the EDGE MLP
(scoring a true continuation below the bar), the GATE (rejecting the longer assembly), or K9
(claim arbitration cutting a chain in half)? Each has a different fix and they are distinguishable
from the dumps.

## WHAT IS ALREADY KNOWN THAT BEARS ON YOUR DIRECTION

* D1's oracle (delete every set-safe duplicate bare seed + fake bare seed in |eta| < 1.7) drives
  dupB to .0053 and RAISES mean nhitOT barrel 9.920 -> 10.057. That oracle only touches BARE
  SEEDS, so it bounds how much of the dup cell is bare-seed-shaped and how much could be chain-
  shaped: whatever it does not reach is potentially yours. Read that entry before you start.
* K9's claim arbitration already enforces a sub-budget MD overlap between accepted chains - two
  accepted chains CAN share MDs. That is the mechanism by which a split would survive, and -CCS
  (chain-loser suppression, currently OFF) was built to suppress exactly that class before being
  unwound for costing displaced efficiency. Read why it was unwound before proposing it again.
* The extension stage, which existed to lengthen chains, was measured a physics LIABILITY and was
  deleted at the maintainer's instruction. "Make chains longer by bolting on more" is a refused
  direction; making the RIGHT chain in the first place is not.
* No backend-specific code without explicit approval, and the GPU timing budget is already over
  (chain block 6.9 ms vs master's 2.6 for the same stages) - a construction change that adds
  significant work needs to justify it.

## OUT OF BOUNDS

The attach head's features/labels/training (R1), retirement/deletion predicates (R2), fake-rate
mechanisms as such (R3 - but send them your census), the seed-admission universe and -RD/-RDT
(R4).

## MEASUREMENT PROTOCOL

* Baseline = the window-fix production config (FINDINGS_R2.md). Reproduce its headline numbers
  before trusting a delta.
* Gate every code change: neutral setting must reproduce the baseline BIT-IDENTICALLY (33/33).
* Confirm on 977 events, per eta band, dup AND fake AND efficiency AND mean nhitOT, displaced
  bands in the headline. Construction changes move everything at once - report everything.

## DELIVERABLE

The split-vs-duplicate census with numbers (ours and master's, same events), and whichever fix it
points to, measured. A confirmed "construction is sound, the gap is selection" is a real result -
it would be the first time that assumption has been tested.
