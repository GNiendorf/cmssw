# A09 VERDICT (draft -- numbers filled as batches 2 and 3 land)

## What the angle asked and what it found

ASKED: several seed-retirement mechanisms overlap (`-RPS`, CheckHitspLS 1+2, the ported
`-XC` arms, the pT3-stage contention). Map the overlaps, then simplify: fewer mechanisms,
same or better scoreboard.

FOUND (structure): the universe is retired by FIVE mechanisms in two code paths with THREE
copies of one predicate. Only one of them, `-RPS`, fires without an anchor -- on a score
that was recorded for every scored pair whether or not anything was delivered. That single
fact explains two open items the Baseline agent left: why `-AT3` cannot buy fake rate
without paying duplicate rate, and why `-CCR 2` (a hand-patch worth +.0044 of efficiency)
has to exist.

FOUND (measurement, all 300 evts): the overlaps are large but nothing is free. Every
deletion buys efficiency and pays duplicate rate, and the honest comparison is the exchange
rate against the baseline's own `-XCT` curve.

## The three things that are certain regardless of the frontier outcome

1. **`-CCR` IS PROVABLY REDUNDANT ONCE THE BARE-T3 TERM GOES.** `A_R0` vs `A_R0CCR1` is
   33/33 branches IDENTICAL under `rebase_ref/cmp_branches.py`. The knob and the +.0044 of
   efficiency attributed to it are an artefact of the score-only predicate, not a mechanism.

2. **`-AT3` DECOUPLES.** With the bare-T3 term deleted, moving `-AT3` 6 -> 7 leaves the seed
   side EXACTLY unchanged (`RPSblock` 154.0 -> 154.0, class-A survivors 29.8 -> 29.8) while
   the delivery volume still falls 120.0 -> 90.7 rows/evt. Under the shipped predicate the
   same move shifts both. `-AT3` becomes a pure delivery knob.

3. **THE FATE COLUMNS OF `-XCD` ARE NOT MECHANISM MARGINALS.** They are order-dependent
   bookkeeping. `-UM` measures the real marginals, and they are very different: `-RPS` fires
   on ~200 class-A seeds/evt of which it is the SOLE retirer of ~15.

## Everything is flag-gated and the defaults are bit-identical
`protoA09/bin/chainproto_a09`, md5 fc636849f3bffa4f159e426af3d79408. `A_NOOP` (the
pre-edit binary) == `FINBASE` 33/33; `G2` (the post-edit binary at the same flags) is the
gate for everything measured on the new binary.
