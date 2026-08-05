# A09 -- WHAT SHOULD CARRY INTO THE COMBINED CONFIG (draft, numbers pending)

## The claim this round is trying to establish
The assembled baseline runs FIVE seed-retirement mechanisms where the structure only
supports FOUR, and the fifth (`-RPS`) is the only one that fires without an anchor. Because
it fires on `plsBestT3Logit >= -AT3`, it silently makes `-AT3` a seed-dedup knob, which is
the measured reason the Baseline agent could not buy fake rate without paying duplicate
rate. It is also the sole reason `-CCR 2` -- a hand-patch worth +.0044 of efficiency --
exists at all.

## Candidate changes, in increasing order of new code

### (1) `-RPS 2`: delete the bare-T3 term. PURE DELETION, no new mechanism.
    -RPS 1  ->  drop = plsBestChainLogit >= -a || plsBestT3Logit >= -AT3
    -RPS 2  ->  drop = plsBestChainLogit >= -a
Consequences that follow structurally, before any measurement:
  * `-CCR` becomes INERT (its only effect was erasing `plsBestT3Logit`), so the knob and the
    +.0044 hand-patch both disappear from the config;
  * `-AT3` stops touching seed retirement, so it becomes a pure delivery knob.
Conflict surface: none. It is a strictly smaller predicate in code that nothing else reads.

### (2) `-RPS 3` / `-RPS 4`: make the predicate STRUCTURAL instead of deleting it.
Retire the seed only if a target it scored above the class margin was actually DELIVERED,
and to a DIFFERENT seed. An ownership lookup over the pair log the delivery already wrote --
one array read per pair, no pairwise candidate loop, no proximity criterion, so it is
compliant with the NEW-mechanism rule. Same two consequences as (1), plus it keeps the
retirements that (1) drops but that ARE structurally justified.
Conflict surface: needs `GeneralAttach::recordPairs`, which the `-XC` bare-chain arm already
turns on in every configuration anyone is proposing. If a sibling proposes an `-XC` variant
with the bare-chain arm OFF, the pair log has to be requested explicitly.

### (3) Reported but NOT acted on (the port is verbatim by mandate)
The `dR^2 < 1e-6` branch of the ported pixel-anchored arm never uniquely retires a seed --
every seed it catches is also caught by the shared-pixel-hit branch or by something else.
It is measured dead weight inside a maintainer-blessed verbatim port, so it stays. Flagged
here only so that whoever eventually re-derives the port knows it can go.

## What must NOT be carried
* Nothing in this round touches the ported CrossCleanpLS windows, the pT3-class delivery
  path, or anything the displaced performance depends on. Every `-RPS` mode is
  OUTPUT-SIDE ONLY (it is gated on `ty == 8` and no downstream stage reads type-8
  suppression), so it cannot move the displaced numbers even in principle.
* `-XCT` is NOT deletable: the bare-chain arm is the single largest unique retirer of
  duplicate seeds. Any proposal that removes it will pay for it in duplicate rate.
