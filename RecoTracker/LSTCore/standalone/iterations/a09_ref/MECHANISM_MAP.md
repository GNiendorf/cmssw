# THE SEED-RETIREMENT MECHANISM MAP (A09)

What follows is read out of `protoFIN/main.cc` -- structure, not measurement. The measured
numbers live in `STATUS.md` and `a09_scoreboard.txt`.

## The universe

The bare-seed universe is `isQuad && pLS_isDupAlgPass2 == 0` (POSTDELP2), 1569.2 seeds per
event on the frozen 300. It reaches the output through TWO channels, which is the first
thing that makes the mechanism count hard to see:

  (a) CARRIED type-8 rows -- LST's own admitted pLS TCs, already in `ev.tc_*`. 860.8/evt.
      Retired by setting `m16RowSuppressed[row]`.
  (b) `-ZP8 6` ADDITIONS -- the 708.4/evt seeds LST's (deleted) CrossCleanpLS used to kill,
      re-synthesised as type-8 rows so the line is scored in the universe that actually
      exists after the deletion.

The two channels apply the SAME verdicts, in two different pieces of code
(`m16RefreshSupp()` at main.cc ~3387 and the `-ZP8` loop at ~4944). The `-XCD` truth
partition carried a THIRD copy of the predicate. Three copies of one rule is why the
`-AT3` coupling described below stayed invisible for two rounds.

## The five mechanisms, in pipeline order

| # | mechanism | flag | anchor | what it asks |
|---|-----------|------|--------|--------------|
| I   | already carries a row | -- | -- | channel (b) only: does this seed already have a surviving row? |
| II  | CheckHitspLS pass 1+2 | `-ZP8 6` | another SEED | LST's own `pLS_isDupAlgPass2`. This DEFINES the universe rather than acting on it. Production code we keep; nothing of ours depends on it beyond the universe. |
| III | attach consumption | none (always on) | -- | did a pT5-class or pT3-class delivery consume this seed? |
| IV  | `-RPS` predicate | `-RPS` | NONE | `plsBestChainLogit >= -a` OR `plsBestT3Logit >= -AT3`. A pure SCORE test: no geometry, and no requirement that anything was delivered. |
| V   | ported CrossCleanpLS | `-XC` | a DELIVERED TC | pixel-anchored arm: shares a pixel hit row with a CONSUMED seed, or is within dR^2 < 1e-6 of one. bare-chain arm: within dR^2 < 0.02 of a SEEDLESS chain TC and that pair's attach logit >= `-XCT`. |

## Two mechanisms that RELEASE seeds rather than retire them
| mechanism | flag | effect on the seed universe |
|-----------|------|------------------------------|
| seed-family dedup of the attach owners | `-RD` / `-RDT` | revokes an attachment, so the seed RETURNS to the bare universe and then faces IV and V like any other |
| pT3-class hit-overlap contention | `-CC` (+ `-CCR`) | revokes a pT3-class delivery; `-CCR 1` returns the seed, `-CCR 2` additionally erases the evidence that IV keys on |

## Ordering effects: there is exactly one, and it is not between IV and V
The pipeline order is III -> IV -> V. It does NOT matter for the verdict, because V's ANCHOR
set is `plsOwned` only -- the arms never anchor on a seed that IV retired -- so IV and V
commute. Only the FATE BOOKKEEPING is order-dependent (a seed both would retire is filed
under whichever ran first), which is why the `-XCD` fate columns must never be read as
mechanism marginals; that is precisely what `-UM` was added to fix.
The order that DOES matter is `-RD` and `-CC` (release) BEFORE IV and V (retire): a
revocation that happens after the retirement decision would destroy the seed outright. That
is the whole content of the `-CCR` knob.

## The four structural facts that decide what can be deleted

1. **`-RPS` IS PURELY OUTPUT-SIDE.** Its half of `m16RefreshSupp` is gated on `ty == 8`, and
   nothing downstream reads type-8 suppression: the pre-claim owner list, the `-EX`
   claimed-hit map and the `-CC` pre-claim all walk type 7 / type 5 outer-tracker hits only,
   and a type-8 row has none. So `-RPS` changes the emitted bare-seed rows and NOTHING else.
   Every `-RPS` A/B in this round is therefore a clean, isolated comparison.

2. **`-RPS` IS THE ONLY MECHANISM WITH NO ANCHOR.** It fires on a score that was recorded for
   EVERY scored pair (`AttachDelivery.cc:80` and `:131` update the per-seed best regardless
   of ownership or margin). A seed is retired because a high-scoring pair EXISTED -- not
   because the object that pair describes was delivered. Every arm of `-XC`, by contrast,
   requires a delivered object to point at.

3. **THE `-XC` BARE-CHAIN ARM ANCHORS ONLY ON SEEDLESS CHAIN TCs** (`outTCs[j].type != 7`,
   `outTCChain[j] >= 0`). A seed whose track was delivered by a DIFFERENT seed as a type-7
   or type-5 row can be retired only by the pixel-anchored arm, i.e. only on a shared pixel
   hit row or dR^2 < 1e-6. That is the structural hole `-RPS` is currently filling, and it
   is a hole LST has too -- its CrossCleanpLS pT5/pT3 arms use exactly those two tests.

4. **`-CCR 2` EXISTS ONLY TO UNDO MECHANISM IV.** When `-CC` revokes a pT3-class delivery,
   `-CCR 1` releases `plsOwned` but leaves `plsBestT3Logit`, so IV keeps the seed retired and
   the revocation destroys the seed outright. `-CCR 2` erases the score by hand. Worth
   +.0044 of efficiency -- i.e. that much of the assembled baseline's efficiency is bought
   by hand-patching a mechanism that should not have fired.

## The unification this suggests

Condition mechanism IV on the same thing every other mechanism is conditioned on: **the
target was actually DELIVERED, and to a different seed.**

    -RPS 3 : retire the seed iff some pair (seed, target) it scored above the target's class
             margin has that target delivered to a DIFFERENT seed.

This is an ownership lookup -- one pass over the pair log the delivery already wrote, one
array read per pair. No pairwise candidate loop, no proximity criterion, so it satisfies the
NEW-mechanism rule. Three consequences follow by construction, before any measurement:

* `-CCR` becomes inert. A revoked pT3-class delivery has `t3Pls == -1`, so its seed is
  released automatically. The knob and the hand-patch both disappear.
* `-AT3` stops being a seed-dedup knob. The only reason it was one is that IV read
  `plsBestT3Logit`; the structural form reads the delivered-owner array instead.
* The "chain scored high but attached to nobody" case, which IV used to retire on the score
  alone, falls through to the `-XC` bare-chain arm -- which handles exactly that case, with
  the maintainer-blessed geometry and its own threshold. The double mechanism collapses into
  one.
